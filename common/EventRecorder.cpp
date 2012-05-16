/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "common/EventRecorder.h"
#include "common/debug-channels.h"
#include "backends/timer/sdl/sdl-timer.h"
#include "backends/mixer/sdl/sdl-mixer.h"
#include "common/bufferedstream.h"
#include "common/config-manager.h"
#include "common/random.h"
#include "common/savefile.h"
#include "common/textconsole.h"

namespace Common {

DECLARE_SINGLETON(EventRecorder);

#define RECORD_VERSION 1

uint32 readTime(ReadStream *inFile) {
	uint32 d = inFile->readByte();
	if (d == 0xff) {
		d = inFile->readUint32LE();
	}

	return d;
}

void writeTime(WriteStream *outFile, uint32 d) {
		//Simple RLE compression
	if (d >= 0xff) {
		outFile->writeByte(0xff);
		outFile->writeUint32LE(d);
	} else {
		outFile->writeByte(d);
	}
}

void EventRecorder::readEvent(RecorderEvent &event) {
	if (_recordMode != kRecorderPlayback) {
		return;
	}
	_recordCount++;
	warning("recordCount = %d", _recordCount);
	event.type = (EventType)_playbackFile->readUint32LE();
	switch (event.type) {	
	case EVENT_TIMER:
		event.time = _playbackFile->readUint32LE();
		break;
	case EVENT_DELAY:
		event.time = _playbackFile->readUint32LE();
		break;
	case EVENT_KEYDOWN:
	case EVENT_KEYUP:
		event.time = _playbackFile->readUint32LE();
		event.kbd.keycode = (KeyCode)_playbackFile->readSint32LE();
		event.kbd.ascii = _playbackFile->readUint16LE();
		event.kbd.flags = _playbackFile->readByte();
		break;
	case EVENT_AUDIO:
		event.time = _playbackFile->readUint32LE();
		event.count = _playbackFile->readUint32LE();
		break;
	case EVENT_MOUSEMOVE:
	case EVENT_LBUTTONDOWN:
	case EVENT_LBUTTONUP:
	case EVENT_RBUTTONDOWN:
	case EVENT_RBUTTONUP:
	case EVENT_WHEELUP:
	case EVENT_WHEELDOWN:
	case EVENT_MBUTTONDOWN:
	case EVENT_MBUTTONUP:
		event.time = _playbackFile->readUint32LE();
		event.mouse.x = _playbackFile->readSint16LE();
		event.mouse.y = _playbackFile->readSint16LE();
		break;
	default:
		event.time = _playbackFile->readUint32LE();
		break;
	}
}

void EventRecorder::writeEvent(const RecorderEvent &event) {
	if (_recordMode != kRecorderRecord) {
		return;
	}
	_recordCount++;
	_tmpRecordFile.writeUint32LE((uint32)event.type);
	switch (event.type) {
	case EVENT_TIMER:
		_tmpRecordFile.writeUint32LE(event.time);
		break;
	case EVENT_DELAY:
		_tmpRecordFile.writeUint32LE(event.count);
		break;
	case EVENT_KEYDOWN:
	case EVENT_KEYUP:
		_tmpRecordFile.writeUint32LE(event.time);
		_tmpRecordFile.writeSint32LE(event.kbd.keycode);
		_tmpRecordFile.writeUint16LE(event.kbd.ascii);
		_tmpRecordFile.writeByte(event.kbd.flags);
		break;
	case EVENT_AUDIO:
		_tmpRecordFile.writeUint32LE(event.time);
		_tmpRecordFile.writeUint32LE(event.count);
		break;
	case EVENT_MOUSEMOVE:
	case EVENT_LBUTTONDOWN:
	case EVENT_LBUTTONUP:
	case EVENT_RBUTTONDOWN:
	case EVENT_RBUTTONUP:
	case EVENT_WHEELUP:
	case EVENT_WHEELDOWN:
	case EVENT_MBUTTONDOWN:
	case EVENT_MBUTTONUP:
		_tmpRecordFile.writeUint32LE(event.time);
		_tmpRecordFile.writeSint16LE(event.mouse.x);
		_tmpRecordFile.writeSint16LE(event.mouse.y);
		break;
	default:
		_tmpRecordFile.writeUint32LE(event.time);
		break;
	}
	if (_recordCount == kMaxBufferedRecords) {
		dumpRecordsToFile();
		_recordCount = 0;
	}
}

void EventRecorder::dumpRecordsToFile() {
	if (!_headerDumped) {
		dumpHeaderToFile();
		_headerDumped = true;
	}
	_recordFile->write(_recordBuffer, _tmpRecordFile.pos());
	_tmpRecordFile.seek(0);
}

void EventRecorder::dumpHeaderToFile() {
	writeFormatId();
	writeVersion();
	writeHeader();
	writeGameHash();
	writeRandomRecords();
	_recordFile->writeUint32LE(MKTAG('R','C','D','S'));
	_recordFile->writeUint32LE(0);
}

EventRecorder::EventRecorder() : _tmpRecordFile(_recordBuffer, kRecordBuffSize) {
	_timeMutex = g_system->createMutex();
	_recorderMutex = g_system->createMutex();
	_recordMode = kPassthrough;
}

EventRecorder::~EventRecorder() {
	g_system->deleteMutex(_timeMutex);
	g_system->deleteMutex(_recorderMutex);
}

void EventRecorder::init() {
	_fakeMixerManager = new NullSdlMixerManager();
	_fakeMixerManager->init();
	_fakeMixerManager->suspendAudio();
	DebugMan.addDebugChannel(kDebugLevelEventRec, "EventRec", "Event recorder debug level"); 
}

void EventRecorder::deinit() {
	debugC(3, kDebugLevelEventRec, "EventRecorder: deinit");

	g_system->getEventManager()->getEventDispatcher()->unregisterSource(this);
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);

	g_system->lockMutex(_timeMutex);
	g_system->lockMutex(_recorderMutex);
	_recordMode = kPassthrough;
	if (_playbackFile != NULL) {
		delete _playbackFile;
	}

	if (_recordFile != NULL) {
		dumpRecordsToFile();
		_recordFile->finalize();
		delete _recordFile;
	}
	g_system->unlockMutex(_timeMutex);
	g_system->unlockMutex(_recorderMutex);
}

bool EventRecorder::delayMillis(uint msecs, bool logged) {
	if (_recordMode == kRecorderRecord)	{
		RecorderEvent delayEvent;
		delayEvent.type = EVENT_DELAY;
		delayEvent.time = _fakeTimer;
		delayEvent.count = msecs;
		writeEvent(delayEvent);
		g_system->delayMillis(msecs);
	}
	if (_recordMode == kRecorderPlayback) {
		if (_nextEvent.type == EVENT_DELAY) {
			g_system->delayMillis(_nextEvent.time);
			if (!logged) {
				g_system->delayMillis(_nextEvent.time);
			}
			getNextEvent();
			return true;
		} else {
			return true;
		}
	}
	return false;
}

void EventRecorder::processMillis(uint32 &millis, bool logging = false) {
	if (_recordMode == kPassthrough) {
		return;
	}
 	_fakeMixerManager->update();
	if (_recordMode == kRecorderRecord) {
		StackLock lock(_recorderMutex);
		uint32 _millisDelay;
		_millisDelay = millis - _lastMillis;
		_lastMillis = millis;
		_fakeTimer += _millisDelay;
		RecorderEvent timerEvent;
		timerEvent.type = EVENT_TIMER;
		timerEvent.time = _fakeTimer;
		writeEvent(timerEvent);
	}

	if (_recordMode == kRecorderPlayback) {
		uint32 audioTime = 0;
		StackLock lock(_recorderMutex);
		if (_nextEvent.type == EVENT_TIMER) {
			if (audioTime != 0) {
				debugC(3, kDebugLevelEventRec, "AudioEventTime = %d, TimerTime = %d",audioTime,_nextEvent.time);
			}
			_fakeTimer = _nextEvent.time;
			getNextEvent();
		}
		else {
			millis = _fakeTimer;
		}
		millis = _fakeTimer;
	}
}


void EventRecorder::checkForKeyCode(const Event &event) {
	if (event.type == EVENT_KEYDOWN) {
		if ((event.kbd.ascii == '-')) {
			decreaseEngineSpeed();
		}
		if ((event.kbd.ascii == '+')) {
			increaseEngineSpeed();
		}
		if ((event.kbd.ascii == '*')) {
			togglePause();
		}
	}
}

bool EventRecorder::notifyEvent(const Event &ev) {
	StackLock lock(_recorderMutex);
	checkForKeyCode(ev);
	if (_recordMode != kRecorderRecord)
		return false;
	if ((ev.type == EVENT_LBUTTONDOWN) || (ev.type == EVENT_LBUTTONUP)) {
		debugC(3, kDebugLevelEventRec, "%d, %d, %d, %d, %d", ev.type, _fakeTimer, _fakeTimer, ev.mouse.x, ev.mouse.y);
	}
	RecorderEvent e;
	memcpy(&e, &ev, sizeof(ev));
	e.time = _fakeTimer;
	writeEvent(e);
	return false;
}

bool EventRecorder::notifyPoll() {
	return false;
}

bool EventRecorder::pollEvent(Event &ev) {
	if (_recordMode != kRecorderPlayback)
		return false;
	StackLock lock(_recorderMutex);

	if (_nextEvent.type ==  EVENT_INVALID) {
		return false;
	}

	if ((_nextEvent.type == EVENT_TIMER) || (_nextEvent.type == EVENT_DELAY) || (_nextEvent.type == EVENT_AUDIO)) {
		return false;
	}

	if (_nextEvent.time > _fakeTimer) {
		return false;
	}
	if ((_nextEvent.type == EVENT_LBUTTONDOWN) || (_nextEvent.type == EVENT_LBUTTONUP)) {
		debugC(3, kDebugLevelEventRec, "%d, %d, %d, %d, %d", _nextEvent.type, _nextEvent.time, _fakeTimer, _nextEvent.mouse.x, _nextEvent.mouse.y);
	}
	
	switch (_nextEvent.type) {
	case EVENT_MOUSEMOVE:
	case EVENT_LBUTTONDOWN:
	case EVENT_LBUTTONUP:
	case EVENT_RBUTTONDOWN:
	case EVENT_RBUTTONUP:
	case EVENT_WHEELUP:
	case EVENT_WHEELDOWN:
		g_system->warpMouse(_nextEvent.mouse.x, _nextEvent.mouse.y);
		break;
	default:
		break;
	}
	ev = _nextEvent;
	getNextEvent();
	return true;
}


void EventRecorder::getNextEvent() {
	if(!_playbackFile->eos()) {		
		readEvent(_nextEvent);		
	}
}

void EventRecorder::writeNextEventsChunk() {
/*	Event event;
	_recordFile->writeUint16LE(_eventsQueue.size());
	while (!_eventsQueue.empty()) {
		event = _eventsQueue.pop();
		writeEvent(event);
	}*/
}

void EventRecorder::decreaseEngineSpeed() {
	if (_engineSpeedMultiplier != 1) {
		_engineSpeedMultiplier = _engineSpeedMultiplier / 2;
	}
	debugC(3, kDebugLevelEventRec, "Decrease speed: %d", _engineSpeedMultiplier);
}

void EventRecorder::increaseEngineSpeed() {
	if (_engineSpeedMultiplier != 8) {
		_engineSpeedMultiplier = _engineSpeedMultiplier * 2;
	}
	debugC(3, kDebugLevelEventRec, "Increase speed: %d", _engineSpeedMultiplier);
}

void EventRecorder::togglePause() {
	switch (_recordMode) {
	case kRecorderPlayback:	
		_recordMode = kRecorderPlaybackPause;
		debugC(3, kDebugLevelEventRec, "Pause");
		break;
	case kRecorderPlaybackPause:
		_recordMode = kRecorderPlayback;
		debugC(3, kDebugLevelEventRec, "Resume");
		break;
	}
}

uint32 EventRecorder::getMillis(bool logging) {
	uint32 millis = g_system->getMillis();
	processMillis(millis);
	if (!logging) {
		return millis;
	}
	return millis;
}

bool EventRecorder::processAudio(uint32 &samples,bool paused) {
	if ((_recordMode == kRecorderRecord)&& !paused)	{	
		StackLock lock(_recorderMutex);
		RecorderEvent audioEvent;
		audioEvent.type =  EVENT_AUDIO;
		audioEvent.time = _fakeTimer;
		audioEvent.count = samples;
		writeEvent(audioEvent);
		return true;
	}
	if (_recordMode == kRecorderPlayback) {

		if ((_nextEvent.type == EVENT_AUDIO) /*&& !paused */) {
			if (_nextEvent.time <= _fakeTimer) {
				getNextEvent();
				return true;
			}
			else {
				samples = 0;
				return false;
			}
		} 
		else {
			samples = 0;
			return false;
		}
	}
	return true;
}

void EventRecorder::RegisterEventSource() {
	g_system->getEventManager()->getEventDispatcher()->registerSource(this, false);
	g_system->getEventManager()->getEventDispatcher()->registerObserver(this, EventManager::kEventRecorderPriority, false, true);
}

uint32 EventRecorder::getRandomSeed(const String &name) {
	_randomNumber = g_system->getMillis();
	if (_recordMode == kRecorderRecord) {
		_randomSourceRecords[name] = _randomNumber;
	} else if (_recordMode == kRecorderPlayback) {
		_randomNumber = _randomSourceRecords[name];
	}
	return _randomNumber;
}

void EventRecorder::init(Common::String gameId, const ADGameDescription* gameDesc) {
	_playbackFile = NULL;
	_recordFile = NULL;
	_recordCount = 0;

	String recordModeString = ConfMan.get("record_mode");
	if (recordModeString.compareToIgnoreCase("record") == 0) {
		_recordMode = kRecorderRecord;
		debugC(3, kDebugLevelEventRec, "EventRecorder: record");
	} else {
		if (recordModeString.compareToIgnoreCase("playback") == 0) {
			_recordMode = kRecorderPlayback;
			debugC(3, kDebugLevelEventRec, "EventRecorder: playback");
		} else {
			_recordMode = kPassthrough;
			debugC(3, kDebugLevelEventRec, "EventRecorder: passthrough");
		}
	}	
	if (!openRecordFile(gameId)) {
		_recordMode = kPassthrough;
		return;
	}

	if (_recordMode == kRecorderRecord) {
		for (const ADGameFileDescription *fileDesc = gameDesc->filesDescriptions; fileDesc->fileName; fileDesc++) {
			_hashRecords[fileDesc->fileName] = fileDesc->md5;
		}
		_tmpRecordFile.seek(0);
	}

	if (_recordMode == kRecorderPlayback) {
		if (!parsePlaybackFile()) {
			_recordMode = kPassthrough;
			return;
		}
		if (!checkGameHash(gameDesc)) {
			_recordMode = kPassthrough;
			return;
		}
		getNextEvent();
	}
	switchMixer();
	_fakeTimer = 0;
	_lastMillis = 0;
	_headerDumped = false;
	_engineSpeedMultiplier = 1;
}

bool EventRecorder::openRecordFile(const String &gameId) {
	Common::String fileName;
	if (gameId.empty()) {
		warning("Game id is undefined. Using default record file name.");
		fileName = "record.bin";
	} else {
		fileName = gameId + ".bin";
	}

	if (_recordMode == kRecorderRecord) {
		_recordFile = wrapBufferedWriteStream(g_system->getSavefileManager()->openForSaving(fileName), 128 * 1024);
	}

	if (_recordMode == kRecorderPlayback) {
		_playbackFile = wrapBufferedSeekableReadStream(g_system->getSavefileManager()->openForLoading(fileName), 128 * 1024, DisposeAfterUse::YES);
		if (!_playbackFile) {
			warning("Cannot open playback file %s. Playback was switched off", fileName.c_str());
			return false;
		}
	}
	return true;
}

bool EventRecorder::checkGameHash(const ADGameDescription* gameDesc) {
	if ((gameDesc == NULL) && (_hashRecords.size() != 0)) {
		warning("Engine doesn't contain description table");
		return false;
	}
	return true;
	for (const ADGameFileDescription *fileDesc = gameDesc->filesDescriptions; fileDesc->fileName; fileDesc++) {
		if (_hashRecords.find(fileDesc->fileName) == _hashRecords.end()) {
			warning("Md5 hash for file %s not found in record file", fileDesc->fileName);
			return false;
		}
		if (_hashRecords[fileDesc->fileName] != fileDesc->md5) {
			warning("Incorrect version of game file %s. Stored md5 is %s. Md5 of loaded game is %s", fileDesc->fileName, _hashRecords[fileDesc->fileName].c_str(), fileDesc->md5);
			return false;
		}
	}
}

Common::String EventRecorder::findMd5ByFileName(const ADGameDescription* gameDesc, const String &fileName) {
	for (const ADGameFileDescription *fileDesc = gameDesc->filesDescriptions; fileDesc->fileName; fileDesc++) {
		if (fileName.equals(fileDesc->fileName)) {
			return fileDesc->md5;
		}
	}
	return String();
}

void EventRecorder::registerMixerManager(SdlMixerManager* mixerManager) {
	_realMixerManager = mixerManager;
}

void EventRecorder::switchMixer() {
	if (_recordMode == kPassthrough) {
		_fakeMixerManager->suspendAudio();
		_realMixerManager->resumeAudio();
	} else {
		_realMixerManager->suspendAudio();
		_fakeMixerManager->resumeAudio();
	}
}

SdlMixerManager* EventRecorder::getMixerManager() {
	if (_recordMode == kPassthrough) {
		return _realMixerManager;
	} else {
		return _fakeMixerManager;
	}
}

Common::String EventRecorder::getAuthor() {
	return "Unknown Author";
}

Common::String EventRecorder::getComment() {
	return "Empty\ncomment";
}

bool EventRecorder::parsePlaybackFile() {
	ChunkHeader nextChunk;
	_playbackParseState = kFileStateCheckFormat;
	nextChunk = readChunk();
	while ((_playbackParseState != kFileStateDone) && (_playbackParseState != kFileStateError)) {
		if (processChunk(nextChunk)) {
			nextChunk = readChunk();
		}
	}
	return _playbackParseState == kFileStateDone;
}

ChunkHeader EventRecorder::readChunk() {
	ChunkHeader result;
	result.id = _playbackFile->readUint32LE();
	result.len = _playbackFile->readUint32LE();
	return result;
}

bool EventRecorder::processChunk(ChunkHeader &nextChunk) {
	switch (_playbackParseState) {
	case kFileStateCheckFormat:
		if ((nextChunk.id == MKTAG('P','B','C','K')) && (nextChunk.len = _playbackFile->size() - 8)) {
			_playbackParseState = kFileStateCheckVersion;
		} else {
			warning("Unknown playback file signature");
			_playbackParseState = kFileStateError;
		}
	break;
	case kFileStateCheckVersion:
		if ((nextChunk.id == MKTAG('V','E','R','S')) && checkPlaybackFileVersion()) {
			_playbackParseState = kFileStateSelectSection;
		} else {
			_recordMode = kPassthrough;
			_playbackParseState = kFileStateError;
		}
	break;
	case kFileStateSelectSection:
		switch (nextChunk.id) {
		case MKTAG('H','E','A','D'): 
			_playbackParseState = kFileStateProcessHeader;
		break;
		case MKTAG('H','A','S','H'): 
			_playbackParseState = kFileStateProcessHash;
		break;
		case MKTAG('R','A','N','D'): 
			_playbackParseState = kFileStateProcessRandom;
		break;
		case MKTAG('R','C','D','S'): 
			_playbackParseState = kFileStateDone;
			return false;
		break;
		default:
			_playbackFile->skip(nextChunk.len);
		break;
		}
	break;
	case kFileStateProcessHeader:
		switch (nextChunk.id) {
		case MKTAG('H','A','U','T'): 
			readAuthor(nextChunk);
		break;
		case MKTAG('H','C','M','T'):
			readComment(nextChunk);
		break;
		default:
			_playbackParseState = kFileStateSelectSection;
			return false;
		break;
		}
	break;
	case kFileStateProcessHash:
		if (nextChunk.id == MKTAG('H','R','C','D')) {
			readHashMap(nextChunk);
		} else {
			_playbackParseState = kFileStateSelectSection;
			return false;
		}
	break;
	case kFileStateProcessRandom:
		if (nextChunk.id == MKTAG('R','R','C','D')) {
			processRndSeedRecord(nextChunk);
		} else {
			_playbackParseState = kFileStateSelectSection;
			return false;
		}
	break;
	}
	return true;
}

bool EventRecorder::checkPlaybackFileVersion() {
	uint32 version;
	version = _playbackFile->readUint32LE();
	if (version != RECORD_VERSION) {
		warning("Incorrect playback file version. Current version is %d", RECORD_VERSION);
		return false;
	}
	return true;
}

void EventRecorder::readAuthor(ChunkHeader chunk) {
	String author = readString(chunk.len);
	warning("Author: %s", author.c_str());
}

void EventRecorder::readComment(ChunkHeader chunk) {
	String comment = readString(chunk.len);
	warning("Comments: %s", comment.c_str());
}

void EventRecorder::processRndSeedRecord(ChunkHeader chunk) {
	String randomSourceName = readString(chunk.len - 4);
	uint32 randomSourceSeed = _playbackFile->readUint32LE();
	_randomSourceRecords[randomSourceName] = randomSourceSeed;
}

void EventRecorder::readHashMap(ChunkHeader chunk) {
	String hashName = readString(chunk.len - 32);
	String hashMd5 = readString(32);
	_hashRecords[hashName] = hashMd5;
}


String EventRecorder::readString(int len) {
	String result;
	char buf[50];
	int readSize = 49;
	while (len > 0)	{
		if (len > 49) {
			readSize = len;
		}
		_playbackFile->read(buf, len);
		buf[len] = 0;
		result += buf;
		len -= readSize;
	}
	return result;
}

void EventRecorder::writeFormatId() {
	_recordFile->writeUint32LE(MKTAG('P','B','C','K'));
	_recordFile->writeUint32LE(0);
}

void EventRecorder::writeVersion() {
	_recordFile->writeUint32LE(MKTAG('V','E','R','S'));
	_recordFile->writeUint32LE(4);
	_recordFile->writeUint32LE(RECORD_VERSION);
}

void EventRecorder::writeHeader() {
	String author = getAuthor();
	String comment = getComment();
	uint32 headerSize = 0;
	if (!author.empty()) {
		headerSize = author.size() + 8;
	}
	if (!comment.empty()) {
		headerSize += comment.size() + 8;
	}
	if (headerSize == 0) {
		return;
	}
	_recordFile->writeUint32LE(MKTAG('H','E','A','D'));
	_recordFile->writeUint32LE(headerSize);
	if (!author.empty()) {
		_recordFile->writeUint32LE(MKTAG('H','A','U','T'));
		_recordFile->writeUint32LE(author.size());
		_recordFile->writeString(author);
	}
	if (!comment.empty()) {
		_recordFile->writeUint32LE(MKTAG('H','C','M','T'));
		_recordFile->writeUint32LE(comment.size());
		_recordFile->writeString(comment);
	}
}

void EventRecorder::writeGameHash() {
	uint32 hashSectionLength = 0;
	for (hashDictionary::iterator i = _hashRecords.begin(); i != _hashRecords.end(); ++i) {
		hashSectionLength = hashSectionLength + i->_key.size() + i->_value.size() + 8;
	}
	if (_hashRecords.size() == 0) {
		return;
	}
	_recordFile->writeUint32LE(MKTAG('H','A','S','H'));
	_recordFile->writeUint32LE(hashSectionLength);
	for (hashDictionary::iterator i = _hashRecords.begin(); i != _hashRecords.end(); ++i) {
		_recordFile->writeUint32LE(MKTAG('H','R','C','D'));
		_recordFile->writeUint32LE(i->_key.size() + i->_value.size());
		_recordFile->writeString(i->_key);
		_recordFile->writeString(i->_value);
	}
}

void EventRecorder::writeRandomRecords() {
	uint32 randomSectionLength = 0;
	for (randomSeedsDictionary::iterator i = _randomSourceRecords.begin(); i != _randomSourceRecords.end(); ++i) {
		randomSectionLength = randomSectionLength + i->_key.size() + 12;
	}
	if (_randomSourceRecords.size() == 0) {
		return;
	}
	_recordFile->writeUint32LE(MKTAG('R','A','N','D'));
	_recordFile->writeUint32LE(randomSectionLength);
	for (randomSeedsDictionary::iterator i = _randomSourceRecords.begin(); i != _randomSourceRecords.end(); ++i) {
		_recordFile->writeUint32LE(MKTAG('R','R','C','D'));
		_recordFile->writeUint32LE(i->_key.size() + 4);
		_recordFile->writeString(i->_key);
		_recordFile->writeUint32LE(i->_value);
	}
}
} // End of namespace Common
