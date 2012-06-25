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
#include "common/md5.h"
#include "common/random.h"
#include "common/savefile.h"
#include "common/textconsole.h"
#include "graphics/thumbnail.h"
#include "graphics/surface.h"
#include "graphics/scaler.h"
namespace Common {

DECLARE_SINGLETON(EventRecorder);

#define RECORD_VERSION 1
#define kDefaultScreenshotPeriod 60000
#define kDefaultBPP 2

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

EventRecorder::EventRecorder() {
	_timeMutex = g_system->createMutex();
	_recorderMutex = g_system->createMutex();
	_recordMode = kPassthrough;
	_timerManager = NULL;
	_screenshotsFile = NULL;
	initialized = false;
}

EventRecorder::~EventRecorder() {
	g_system->deleteMutex(_timeMutex);
	g_system->deleteMutex(_recorderMutex);
	if (_timerManager != NULL) {
		delete _timerManager;
	}
}

void EventRecorder::init() {
	_fakeMixerManager = new NullSdlMixerManager();
	_fakeMixerManager->init();
	_fakeMixerManager->suspendAudio();
	DebugMan.addDebugChannel(kDebugLevelEventRec, "EventRec", "Event recorder debug level"); 
}

void EventRecorder::deinit() {
	initialized = false;
	_recordMode = kPassthrough;
	debugC(3, kDebugLevelEventRec, "EventRecorder: deinit");
	g_system->getEventManager()->getEventDispatcher()->unregisterSource(this);
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);
	g_system->lockMutex(_timeMutex);
	g_system->lockMutex(_recorderMutex);
	_recordMode = kPassthrough;
	if (_screenshotsFile != NULL) {
		_screenshotsFile->finalize();
		delete _screenshotsFile;
	}
	g_system->unlockMutex(_timeMutex);
	g_system->unlockMutex(_recorderMutex);
	switchMixer();
	switchTimerManagers();
}

bool EventRecorder::delayMillis(uint msecs, bool logged) {
	if (_recordMode == kRecorderRecord)	{
		RecorderEvent delayEvent;
		delayEvent.type = EVENT_DELAY;
		delayEvent.time = _fakeTimer;
		delayEvent.count = msecs;
		_playbackFile.writeEvent(delayEvent);
		g_system->delayMillis(msecs);
	}
	if (_recordMode == kRecorderPlayback) {
		if (_nextEvent.type == EVENT_DELAY) {
			g_system->delayMillis(_nextEvent.time);
			if (!logged) {
				g_system->delayMillis(_nextEvent.time);
			}
			_nextEvent = _playbackFile.getNextEvent();
			return true;
		} else {
			return true;
		}
	}
	return false;
}

void EventRecorder::processMillis(uint32 &millis) {
	if (_recordMode == kPassthrough) {
		return;
	}
	if (!initialized) {
		return;
	}
	updateSubsystems();
	if (_recordMode == kRecorderRecord) {
		StackLock lock(_recorderMutex);
		uint32 _millisDelay;
		_millisDelay = millis - _lastMillis;
		_lastMillis = millis;
		_fakeTimer += _millisDelay;
		RecorderEvent timerEvent;
		timerEvent.type = EVENT_TIMER;
		timerEvent.time = _fakeTimer;
		_playbackFile.writeEvent(timerEvent);
		if ((_fakeTimer - _lastScreenshotTime) > _screenshotPeriod) {
			Graphics::Surface screen;
			uint8 md5[16];
			if (grabScreenAndComputeMD5(screen, md5)) {
				_lastScreenshotTime = _fakeTimer;
				_playbackFile.saveScreenShot(screen, md5);
				screen.free();
			}
		}
	}
	if (_recordMode == kRecorderPlayback) {
		uint32 audioTime = 0;
		StackLock lock(_recorderMutex);
		if (_nextEvent.type == EVENT_TIMER) {
			if (audioTime != 0) {
				debugC(3, kDebugLevelEventRec, "AudioEventTime = %d, TimerTime = %d", audioTime, _nextEvent.time);
			}
			_fakeTimer = _nextEvent.time;
			_nextEvent = _playbackFile.getNextEvent();
		}
		else {
			millis = _fakeTimer;
		}
		millis = _fakeTimer;
	}
	_timerManager->handler(_fakeTimer);
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
	if (!initialized) {
		return false;
	}
	if ((ev.type == EVENT_LBUTTONDOWN) || (ev.type == EVENT_LBUTTONUP)) {
		debugC(3, kDebugLevelEventRec, "%d, %d, %d, %d, %d", ev.type, _fakeTimer, _fakeTimer, ev.mouse.x, ev.mouse.y);
	}
	RecorderEvent e;
	memcpy(&e, &ev, sizeof(ev));
	e.time = _fakeTimer;
	_playbackFile.writeEvent(e);
	return false;
}

bool EventRecorder::notifyPoll() {
	return false;
}

bool EventRecorder::pollEvent(Event &ev) {
	if (_recordMode != kRecorderPlayback)
		return false;
	if (!initialized) {
		return false;
	}
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

	_nextEvent.synthetic = true;
	
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
	_nextEvent = _playbackFile.getNextEvent();
	return true;
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


bool EventRecorder::processAudio(uint32 &samples,bool paused) {
	if ((_recordMode == kRecorderRecord)&& !paused)	{	
		if (!initialized) {
			return false;
		}
		StackLock lock(_recorderMutex);
		RecorderEvent audioEvent;
		audioEvent.type =  EVENT_AUDIO;
		audioEvent.time = _fakeTimer;
		audioEvent.count = samples;
		_playbackFile.writeEvent(audioEvent);
		return true;
	}
	if (_recordMode == kRecorderPlayback) {
		if (!initialized) {
			return false;
		}

		if (_nextEvent.type == EVENT_AUDIO) {
			if (_nextEvent.time <= _fakeTimer) {
				_nextEvent = _playbackFile.getNextEvent();
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
	g_system->getEventManager()->getEventDispatcher()->registerMapper(this);
}

uint32 EventRecorder::getRandomSeed(const String &name) {
	uint32 result = g_system->getMillis();
	if (_recordMode == kRecorderRecord) {
		_playbackFile.getHeader().randomSourceRecords[name] = result;
	} else if (_recordMode == kRecorderPlayback) {
		result = _playbackFile.getHeader().randomSourceRecords[name];
	}
	return result;
}

void EventRecorder::init(Common::String recordFileName, RecordMode mode) {
	_fakeTimer = 0;
	_lastMillis = 0;
	_engineSpeedMultiplier = 1;
	_lastScreenshotTime = 0;
	_screenshotsFile = NULL;
	_recordMode = mode;
	g_system->getEventManager()->getEventDispatcher()->registerSource(this, false);
	g_system->getEventManager()->getEventDispatcher()->registerObserver(this, EventManager::kEventRecorderPriority, false, true);
	_screenshotPeriod = ConfMan.getInt("screenshot_period");
	if (_screenshotPeriod == 0) {
		_screenshotPeriod = kDefaultScreenshotPeriod;
	}
	if (!openRecordFile(recordFileName)) {
		deinit();
		return;
	}
	if (_recordMode == kRecorderPlayback) {
		applyPlaybackSettings();
		_nextEvent = _playbackFile.getNextEvent();
	}
	if (_recordMode == kRecorderRecord) {
		getConfig();
	}

	switchMixer();
	switchTimerManagers();
	initialized = true;
}


/**
 * Opens or creates file depend of recording mode.
 *
 *@param id of recording or playing back game
 *@return true in case of success, false in case of error
 *
 */

bool EventRecorder::openRecordFile(const String &fileName) {
	if (_recordMode == kRecorderRecord) {
		return _playbackFile.openWrite(fileName);
	}
	if (_recordMode == kRecorderPlayback) {
		return _playbackFile.openRead(fileName);
	}
	return true;
}

bool EventRecorder::checkGameHash(const ADGameDescription *gameDesc) {
	if ((gameDesc == NULL) && (_playbackFile.getHeader().hashRecords.size() != 0)) {
		warning("Engine doesn't contain description table");
		return false;
	}
	for (const ADGameFileDescription *fileDesc = gameDesc->filesDescriptions; fileDesc->fileName; fileDesc++) {
		if (_playbackFile.getHeader().hashRecords.find(fileDesc->fileName) == _playbackFile.getHeader().hashRecords.end()) {
			warning("MD5 hash for file %s not found in record file", fileDesc->fileName);
			return false;
		}
		if (_playbackFile.getHeader().hashRecords[fileDesc->fileName] != fileDesc->md5) {
			warning("Incorrect version of game file %s. Stored MD5 is %s. MD5 of loaded game is %s", fileDesc->fileName, _playbackFile.getHeader().hashRecords[fileDesc->fileName].c_str(), fileDesc->md5);
			return false;
		}
	}
	return true;
}

Common::String EventRecorder::findMD5ByFileName(const ADGameDescription *gameDesc, const String &fileName) {
	for (const ADGameFileDescription *fileDesc = gameDesc->filesDescriptions; fileDesc->fileName; fileDesc++) {
		if (fileName.equals(fileDesc->fileName)) {
			return fileDesc->md5;
		}
	}
	return String();
}

void EventRecorder::registerMixerManager(SdlMixerManager *mixerManager) {
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

SdlMixerManager *EventRecorder::getMixerManager() {
	if (_recordMode == kPassthrough) {
		return _realMixerManager;
	} else {
		return _fakeMixerManager;
	}
}

void EventRecorder::getConfigFromDomain(ConfigManager::Domain *domain) {
	for (ConfigManager::Domain::iterator entry = domain->begin(); entry!= domain->end(); ++entry) {
		_playbackFile.getHeader().setSettings(entry->_key, entry->_value);
	}
}

void EventRecorder::getConfig() {
	getConfigFromDomain(ConfMan.getDomain(ConfMan.kApplicationDomain));
	getConfigFromDomain(ConfMan.getActiveDomain());
	getConfigFromDomain(ConfMan.getDomain(ConfMan.kTransientDomain));
}


void EventRecorder::applyPlaybackSettings() {
	for (StringMap::iterator i = _playbackFile.getHeader().settingsRecords.begin(); i != _playbackFile.getHeader().settingsRecords.end(); ++i) {
		String currentValue = ConfMan.get(i->_key);
		if (currentValue != i->_value) {
			warning("Config value <%s>: %s -> %s", i->_key.c_str(), i->_value.c_str(), currentValue.c_str());
			ConfMan.set(i->_key, i->_value, ConfMan.kApplicationDomain);
		}
	}
	removeDifferentEntriesInDomain(ConfMan.getDomain(ConfMan.kApplicationDomain));
	removeDifferentEntriesInDomain(ConfMan.getActiveDomain());
	removeDifferentEntriesInDomain(ConfMan.getDomain(ConfMan.kTransientDomain));
}

void EventRecorder::removeDifferentEntriesInDomain(ConfigManager::Domain *domain) {
	for (ConfigManager::Domain::iterator entry = domain->begin(); entry!= domain->end(); ++entry) {
		if (_playbackFile.getHeader().settingsRecords.find(entry->_key) == _playbackFile.getHeader().settingsRecords.end()) {
			warning("Config value <%s>: %s -> (null)", entry->_key.c_str(), entry->_value.c_str());
			domain->erase(entry->_key);
		}
	}
}



bool EventRecorder::grabScreenAndComputeMD5(Graphics::Surface &screen, uint8 md5[16]) {
	if (!createScreenShot(screen)) {
		warning("Can't save screenshot");
		return false;
	}	
	MemoryReadStream bitmapStream((const byte*)screen.pixels, screen.w * screen.h * screen.format.bytesPerPixel);
	computeStreamMD5(bitmapStream, md5);
	return true;
}


/*
void EventRecorder::checkRecordedMD5() {
	uint8 currentMD5[16];
	uint8 savedMD5[16];
	Graphics::Surface screen;
	if (!grabScreenAndComputeMD5(screen, currentMD5)) {
		return;
	}
	_playbackFile->read(savedMD5, 16);
	if (memcmp(savedMD5, currentMD5, 16) != 0) {
		warning("Recorded and current screenshots are different");
	}
	Graphics::saveThumbnail(*_screenshotsFile, screen);
	screen.free();
}*/

DefaultTimerManager *EventRecorder::getTimerManager() {
	return _timerManager;
}

void EventRecorder::registerTimerManager(DefaultTimerManager *timerManager) {
	_timerManager = timerManager;
}

void EventRecorder::switchTimerManagers() {
	delete _timerManager;
	if (_recordMode == kPassthrough) {
		_timerManager = new SdlTimerManager();
	} else {
		_timerManager = new DefaultTimerManager();
	}
}

void EventRecorder::updateSubsystems() {
	if (_recordMode == kPassthrough) {
		return;
	}
	RecordMode oldRecordMode = _recordMode;
	_recordMode = kPassthrough;
	_fakeMixerManager->update();
	_recordMode = oldRecordMode;
}

List<Event> EventRecorder::mapEvent(const Event &ev, EventSource *source) {
	if ((_recordMode == kRecorderPlayback) && (ev.synthetic != true)) {
		return List<Event>();
	} else {
		return DefaultEventMapper::mapEvent(ev, source);
	}
}

void EventRecorder::setGameMd5(const ADGameDescription *gameDesc) {
	for (const ADGameFileDescription *fileDesc = gameDesc->filesDescriptions; fileDesc->fileName; fileDesc++) {
		if (fileDesc->md5 != NULL) {
			_playbackFile.getHeader().hashRecords[fileDesc->fileName] = fileDesc->md5;
		}
	}
}

void EventRecorder::processGameDescription(const ADGameDescription *desc) {
	if (_recordMode == kRecorderRecord) {
		setGameMd5(desc);
	}
	if ((_recordMode == kRecorderPlayback) && !checkGameHash(desc)) {
		deinit();
	}
}

void EventRecorder::deleteRecord(const String& fileName) {
	g_system->getSavefileManager()->removeSavefile(fileName);
}

void EventRecorder::setAuthor(const Common::String &author) {
	_playbackFile.getHeader().author = author;
}

void EventRecorder::setNotes(const Common::String &desc) {
	_playbackFile.getHeader().notes = desc;
}

void EventRecorder::setName(const Common::String &name) {
	_playbackFile.getHeader().name = name;
}


PlaybackFile::PlaybackFile() : _tmpRecordFile(_tmpBuffer, kRecordBuffSize), _tmpPlaybackFile(_tmpBuffer, kRecordBuffSize) {
	_readStream = NULL;
	_writeStream = NULL;
}

PlaybackFile::~PlaybackFile() {
	close();
}

bool PlaybackFile::openWrite(Common::String fileName) {
	close();
	_writeStream = wrapBufferedWriteStream(g_system->getSavefileManager()->openForSaving(fileName), 128 * 1024);
	_headerDumped = false;
	_recordCount = 0;
	if (_writeStream == NULL) {
		return false;
	}
	_mode = kWrite;
	return true;
}

bool PlaybackFile::openRead(Common::String fileName) {
	_eventsSize = 0;
	_readStream = wrapBufferedSeekableReadStream(g_system->getSavefileManager()->openForLoading(fileName), 128 * 1024, DisposeAfterUse::YES);
	if (_readStream == NULL) {
		return false;
	}
	if (!parseHeader()) {
		return false;
	}
	_mode = kRead;
	return true;
}

void PlaybackFile::close() {
	delete _readStream;
	if (_writeStream != NULL) {
		dumpRecordsToFile();
		_writeStream->finalize();
	}
	delete _writeStream;
	_readStream = NULL;
	_writeStream = NULL;
}

bool PlaybackFile::parseHeader() {
	PlaybackFileHeader result;
	ChunkHeader nextChunk;
	_playbackParseState = kFileStateCheckFormat;
	nextChunk = readChunkHeader();
	while ((_playbackParseState != kFileStateDone) && (_playbackParseState != kFileStateError)) {
		if (processChunk(nextChunk)) {
			nextChunk = readChunkHeader();
		}
	}
	return _playbackParseState == kFileStateDone;
}

bool PlaybackFile::checkPlaybackFileVersion() {
	uint32 version;
	version = _readStream->readUint32LE();
	if (version != RECORD_VERSION) {
		warning("Incorrect playback file version. Expected version %d, but got %d.", RECORD_VERSION, version);
		return false;
	}
	return true;
}


Common::String PlaybackFile::readString(int len) {
	String result;
	char buf[50];
	int readSize = 49;
	while (len > 0)	{
		if (len <= 49) {
			readSize = len;
		}
		_readStream->read(buf, readSize);
		buf[readSize] = 0;
		result += buf;
		len -= readSize;
	}
	return result;
}


Common::ChunkHeader PlaybackFile::readChunkHeader() {
	ChunkHeader result;
	result.id = _readStream->readUint32LE();
	result.len = _readStream->readUint32LE();
	return result;
}

bool PlaybackFile::processChunk(ChunkHeader &nextChunk) {
	switch (_playbackParseState) {
	case kFileStateCheckFormat:
		if (nextChunk.id == kFormatIdTag) {
			_playbackParseState = kFileStateCheckVersion;
		} else {
			warning("Unknown playback file signature");
			_playbackParseState = kFileStateError;
		}
		break;
	case kFileStateCheckVersion:
		if ((nextChunk.id == kVersionTag) && checkPlaybackFileVersion()) {
			_playbackParseState = kFileStateSelectSection;
		} else {
			_playbackParseState = kFileStateError;
		}
		break;
	case kFileStateSelectSection:
		switch (nextChunk.id) {
		case kHeaderSectionTag:
			_playbackParseState = kFileStateProcessHeader;
			break;
		case kHashSectionTag:
			_playbackParseState = kFileStateProcessHash;
			break;
		case kRandomSectionTag:
			_playbackParseState = kFileStateProcessRandom;
			break;
		case kEventTag:
		case kScreenShotTag:
			_readStream->seek(-8, SEEK_CUR);
			_playbackParseState = kFileStateDone;
			return false;
		case kSettingsSectionTag:
			_playbackParseState = kFileStateProcessSettings;
			warning("Loading record header");
			break;
		default:
			_readStream->skip(nextChunk.len);
			break;
		}
		break;
	case kFileStateProcessHeader:
		switch (nextChunk.id) {
		case kAuthorTag:
			_header.author = readString(nextChunk.len);
			break;
		case kCommentsTag:
			_header.notes = readString(nextChunk.len);
			break;
		default:
			_playbackParseState = kFileStateSelectSection;
			return false;
		}
		break;
	case kFileStateProcessHash:
		if (nextChunk.id == kHashRecordTag) {
			readHashMap(nextChunk);
		} else {
			_playbackParseState = kFileStateSelectSection;
			return false;
		}
		break;
	case kFileStateProcessRandom:
		if (nextChunk.id == kRandomRecordTag) {
			processRndSeedRecord(nextChunk);
		} else {
			_playbackParseState = kFileStateSelectSection;
			return false;
		}
		break;
	case kFileStateProcessSettings:
		if (nextChunk.id == kSettingsRecordTag) {
			if (!processSettingsRecord(nextChunk)) {
				_playbackParseState = kFileStateError;
				return false;
			}
		} else {
			_playbackParseState = kFileStateSelectSection;
			return false;
		}
		break;
	}
	return true;
}

void PlaybackFile::returnToChunkHeader() {
	_readStream->seek(-8, SEEK_CUR);
}

void PlaybackFile::readHashMap(ChunkHeader chunk) {
	String hashName = readString(chunk.len - 32);
	String hashMd5 = readString(32);
	_header.hashRecords[hashName] = hashMd5;
}

void PlaybackFile::processRndSeedRecord(ChunkHeader chunk) {
	String randomSourceName = readString(chunk.len - 4);
	uint32 randomSourceSeed = _readStream->readUint32LE();
	_header.randomSourceRecords[randomSourceName] = randomSourceSeed;
}

bool PlaybackFile::processSettingsRecord(ChunkHeader chunk) {
	ChunkHeader keyChunk = readChunkHeader();
	if (keyChunk.id != kSettingsRecordKeyTag) {
		warning("Invalid format of settings section");
		return false;
	}
	String key = readString(keyChunk.len);
	ChunkHeader valueChunk = readChunkHeader();
	if (valueChunk.id != kSettingsRecordValueTag) {
		warning("Invalid format of settings section");
		return false;
	}
	String value = readString(valueChunk.len);
	_header.settingsRecords[key] = value;
	return true;
	return false;
}

Common::RecorderEvent PlaybackFile::getNextEvent() {
	assert(_mode == kRead);
	if (isEventsBufferEmpty()) {
		ChunkHeader header;
		header.id = 0;
		while (header.id != kEventTag) {
			header = readChunkHeader();
			if (_readStream->eos()) {
				break;
			}
			switch (header.id) {
			case kEventTag:
				readEventsToBuffer(header.len);
				break;
			case kScreenShotTag:
				_readStream->seek(-4, SEEK_CUR);
				header.len = _readStream->readUint32BE();
				_readStream->skip(header.len-8);
				break;
			case kMD5Tag:
				_readStream->skip(header.len);
				//checkRecordedMD5();
				break;
			default:
				_readStream->skip(header.len);
				break;
			}
		}
	}
	Common::RecorderEvent result;
	readEvent(result);
	return result;
}

bool PlaybackFile::isEventsBufferEmpty() {
	return (uint32)_tmpPlaybackFile.pos() == _eventsSize;
}

void PlaybackFile::readEvent(RecorderEvent& event) {
	event.type = (EventType)_tmpPlaybackFile.readUint32LE();
	switch (event.type) {
	case EVENT_TIMER:
		event.time = _tmpPlaybackFile.readUint32LE();
		break;
	case EVENT_DELAY:
		event.time = _tmpPlaybackFile.readUint32LE();
		break;
	case EVENT_KEYDOWN:
	case EVENT_KEYUP:
		event.time = _tmpPlaybackFile.readUint32LE();
		event.kbd.keycode = (KeyCode)_tmpPlaybackFile.readSint32LE();
		event.kbd.ascii = _tmpPlaybackFile.readUint16LE();
		event.kbd.flags = _tmpPlaybackFile.readByte();
		break;
	case EVENT_AUDIO:
		event.time = _tmpPlaybackFile.readUint32LE();
		event.count = _tmpPlaybackFile.readUint32LE();
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
		event.time = _tmpPlaybackFile.readUint32LE();
		event.mouse.x = _tmpPlaybackFile.readSint16LE();
		event.mouse.y = _tmpPlaybackFile.readSint16LE();
		break;
	default:
		event.time = _tmpPlaybackFile.readUint32LE();
		break;
	}
}

void PlaybackFile::readEventsToBuffer(uint32 size) {
	_readStream->read(_tmpBuffer, size);
	_tmpPlaybackFile.seek(0);
	_eventsSize = size;
}

void PlaybackFile::saveScreenShot(Graphics::Surface &screen, byte md5[16]) {
		dumpRecordsToFile();
		_writeStream->writeUint32LE(kMD5Tag);
		_writeStream->writeUint32LE(16);
		_writeStream->write(md5, 16);
		Graphics::saveThumbnail(*_writeStream, screen);
}

void PlaybackFile::dumpRecordsToFile() {
	if (!_headerDumped) {
		dumpHeaderToFile();
		_headerDumped = true;
	}
	if (_recordCount == 0) {
		return;
	}
	_writeStream->writeUint32LE(kEventTag);
	_writeStream->writeUint32LE(_tmpRecordFile.pos());
	_writeStream->write(_tmpBuffer, _tmpRecordFile.pos());
	_tmpRecordFile.seek(0);
	_recordCount = 0;
}

void PlaybackFile::dumpHeaderToFile() {
	_writeStream->writeUint32LE(kFormatIdTag);
	//NULL  size for first tag cause we can't calculate
	//size of file in moment of header dumping
	_writeStream->writeUint32LE(0);

	_writeStream->writeUint32LE(kVersionTag);
	_writeStream->writeUint32LE(4);
	_writeStream->writeUint32LE(RECORD_VERSION);
	writeHeaderSection();
	writeGameHash();
	writeRandomRecords();
	writeGameSettings();
	writeScreenSettings();
}

void PlaybackFile::writeHeaderSection() {
	String author = _header.author;
	String comment = _header.notes;
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
	_writeStream->writeUint32LE(kHeaderSectionTag);
	_writeStream->writeUint32LE(headerSize);
	if (!author.empty()) {
		_writeStream->writeUint32LE(kAuthorTag);
		_writeStream->writeUint32LE(author.size());
		_writeStream->writeString(author);
	}
	if (!comment.empty()) {
		_writeStream->writeUint32LE(kCommentsTag);
		_writeStream->writeUint32LE(comment.size());
		_writeStream->writeString(comment);
	}
}

void PlaybackFile::writeGameHash() {
	uint32 hashSectionSize = 0;
	for (StringMap::iterator i = _header.hashRecords.begin(); i != _header.hashRecords.end(); ++i) {
		hashSectionSize = hashSectionSize + i->_key.size() + i->_value.size() + 8;
	}
	if (_header.hashRecords.size() == 0) {
		return;
	}
	_writeStream->writeUint32LE(kHashSectionTag);
	_writeStream->writeUint32LE(hashSectionSize);
	for (StringMap::iterator i = _header.hashRecords.begin(); i != _header.hashRecords.end(); ++i) {
		_writeStream->writeUint32LE(kHashRecordTag);
		_writeStream->writeUint32LE(i->_key.size() + i->_value.size());
		_writeStream->writeString(i->_key);
		_writeStream->writeString(i->_value);
	}
}

void PlaybackFile::writeRandomRecords() {
	uint32 randomSectionSize = 0;
	for (RandomSeedsDictionary::iterator i = _header.randomSourceRecords.begin(); i != _header.randomSourceRecords.end(); ++i) {
		randomSectionSize = randomSectionSize + i->_key.size() + 12;
	}
	if (_header.randomSourceRecords.size() == 0) {
		return;
	}
	_writeStream->writeUint32LE(kRandomSectionTag);
	_writeStream->writeUint32LE(randomSectionSize);
	for (RandomSeedsDictionary::iterator i = _header.randomSourceRecords.begin(); i != _header.randomSourceRecords.end(); ++i) {
		_writeStream->writeUint32LE(kRandomRecordTag);
		_writeStream->writeUint32LE(i->_key.size() + 4);
		_writeStream->writeString(i->_key);
		_writeStream->writeUint32LE(i->_value);
	}
}

void PlaybackFile::writeScreenSettings() {
	_writeStream->writeUint32LE(MKTAG('S','C','R','N'));
	//Chunk size = 4 (width(2 bytes) + height(2 bytes))
	_writeStream->writeUint32LE(4);
	_writeStream->writeUint16LE(g_system->getWidth());
	_writeStream->writeSint16LE(g_system->getHeight());
}

void PlaybackFile::writeEvent(const RecorderEvent &event) {
	assert(_mode == kWrite);
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
	}
}

void PlaybackFile::writeGameSettings() {
	_writeStream->writeUint32LE(kSettingsSectionTag);
	_writeStream->writeUint32LE(_header.settingsSectionSize);
	for (StringMap::iterator i = _header.settingsRecords.begin(); i != _header.settingsRecords.end(); ++i) {
		_writeStream->writeUint32LE(kSettingsRecordTag);
		_writeStream->writeUint32LE(i->_key.size() + i->_value.size() + 16);
		_writeStream->writeUint32LE(kSettingsRecordKeyTag);
		_writeStream->writeUint32LE(i->_key.size());
		_writeStream->writeString(i->_key);
		_writeStream->writeUint32LE(kSettingsRecordValueTag);
		_writeStream->writeUint32LE(i->_value.size());
		_writeStream->writeString(i->_value);
	}
}


} // End of namespace Common
