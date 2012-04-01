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

#include "common/bufferedstream.h"
#include "common/config-manager.h"
#include "common/random.h"
#include "common/savefile.h"
#include "common/textconsole.h"

namespace Common {

DECLARE_SINGLETON(EventRecorder);

#define RECORD_SIGNATURE 0x54455354
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

void EventRecorder::readEvent(Event &event) {
	event.type = (EventType)_playbackFile->readUint32LE();

	switch (event.type) {
	case EVENT_KEYDOWN:
	case EVENT_KEYUP:
		event.kbd.keycode = (KeyCode)_playbackFile->readSint32LE();
		event.kbd.ascii = _playbackFile->readUint16LE();
		event.kbd.flags = _playbackFile->readByte();
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
		event.mouse.x = _playbackFile->readSint16LE();
		event.mouse.y = _playbackFile->readSint16LE();
		break;
	default:
		break;
	}
}

void EventRecorder::writeEvent(Event &event) {
	_recordFile->writeUint32LE((uint32)event.type);

	switch (event.type) {
	case EVENT_KEYDOWN:
	case EVENT_KEYUP:
		_recordFile->writeSint32LE(event.kbd.keycode);
		_recordFile->writeUint16LE(event.kbd.ascii);
		_recordFile->writeByte(event.kbd.flags);
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
		_recordFile->writeSint16LE(event.mouse.x);
		_recordFile->writeSint16LE(event.mouse.y);
		break;
	default:
		break;
	}
}

EventRecorder::EventRecorder() {
	_recordFile = NULL;
	_recordTimeFile = NULL;
	_playbackFile = NULL;
	_playbackTimeFile = NULL;
	_timeMutex = g_system->createMutex();
	_recorderMutex = g_system->createMutex();

	_eventCount = 0;
	_lastEventCount = 0;
	_lastMillis = 0;
	_lastEventMillis = 0;
	_engineSpeedMultiplier = 1;
	_fakeTimer = 0;

	_recordMode = kPassthrough;
}

EventRecorder::~EventRecorder() {
	deinit();

	g_system->deleteMutex(_timeMutex);
	g_system->deleteMutex(_recorderMutex);
}

void EventRecorder::init() {
	String recordModeString = ConfMan.get("record_mode");
	if (recordModeString.compareToIgnoreCase("record") == 0) {
		_recordMode = kRecorderRecord;

		debug(3, "EventRecorder: record");
	} else {
		if (recordModeString.compareToIgnoreCase("playback") == 0) {
			_recordMode = kRecorderPlayback;
			debug(3, "EventRecorder: playback");
		} else {
			_recordMode = kPassthrough;
			debug(3, "EventRecorder: passthrough");
		}
	}

	_recordFileName = ConfMan.get("record_file_name");
	if (_recordFileName.empty()) {
		_recordFileName = "record.bin";
	}
	_recordTempFileName = ConfMan.get("record_temp_file_name");
	if (_recordTempFileName.empty()) {
		_recordTempFileName = "record.tmp";
	}
	_recordTimeFileName = ConfMan.get("record_time_file_name");
	if (_recordTimeFileName.empty()) {
		_recordTimeFileName = "record.time";
	}

	// recorder stuff
	if (_recordMode == kRecorderRecord) {
		_recordCount = 0;
		_recordTimeCount = 0;
		_recordFile = wrapBufferedWriteStream(g_system->getSavefileManager()->openForSaving(_recordFileName), 128 * 1024);
		_recordTimeFile = wrapBufferedWriteStream(g_system->getSavefileManager()->openForSaving(_recordTimeFileName), 128 * 1024);
		_recordSubtitles = ConfMan.getBool("subtitles");
	}

	uint32 sign;
	uint32 randomSourceCount;
	if (_recordMode == kRecorderPlayback) {
		_playbackCount = 0;
		_playbackTimeCount = 0;
		_playbackFile = wrapBufferedSeekableReadStream(g_system->getSavefileManager()->openForLoading(_recordFileName), 128 * 1024, DisposeAfterUse::YES);
		_playbackTimeFile = wrapBufferedSeekableReadStream(g_system->getSavefileManager()->openForLoading(_recordTimeFileName), 128 * 1024, DisposeAfterUse::YES);

		if (!_playbackFile) {
			warning("Cannot open playback file %s. Playback was switched off", _recordFileName.c_str());
			_recordMode = kPassthrough;
		}

		if (!_playbackTimeFile) {
			warning("Cannot open playback time file %s. Playback was switched off", _recordTimeFileName.c_str());
			_recordMode = kPassthrough;
		}		
	}

	if (_recordMode == kRecorderRecord) {
		_recordFile->writeUint32LE(RECORD_SIGNATURE);
		_recordFile->writeUint32LE(RECORD_VERSION);
	}

	if (_recordMode == kRecorderPlayback) {
		sign = _playbackFile->readUint32LE();
		if (sign != RECORD_SIGNATURE) {
			error("Unknown record file signature");
		}
		_playbackFile->readUint32LE(); // version
	}

	g_system->getEventManager()->getEventDispatcher()->registerSource(this, false);
	g_system->getEventManager()->getEventDispatcher()->registerObserver(this, EventManager::kEventRecorderPriority, false, true);
}

void EventRecorder::deinit() {
	debug(3, "EventRecorder: deinit");

	g_system->getEventManager()->getEventDispatcher()->unregisterSource(this);
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);

	g_system->lockMutex(_timeMutex);
	g_system->lockMutex(_recorderMutex);
	_recordMode = kPassthrough;
	g_system->unlockMutex(_timeMutex);
	g_system->unlockMutex(_recorderMutex);

	delete _playbackFile;
	delete _playbackTimeFile;

	if (_recordFile != NULL) {
		_recordFile->finalize();
		delete _recordFile;
		_recordTimeFile->finalize();
		delete _recordTimeFile;

		_playbackFile = g_system->getSavefileManager()->openForLoading(_recordTempFileName);
	}
}

void EventRecorder::registerRandomSource(RandomSource &rnd, const String &name) {
	if (_recordMode == kRecorderRecord) {
		RandomSourceRecord rec;
		rec.name = name;
		rec.seed = rnd.getSeed();
		_randomSourceRecords.push_back(rec);
	}

	if (_recordMode == kRecorderPlayback) {
		for (uint i = 0; i < _randomSourceRecords.size(); ++i) {
			if (_randomSourceRecords[i].name == name) {
				rnd.setSeed(_randomSourceRecords[i].seed);
				_randomSourceRecords.remove_at(i);
				break;
			}
		}
	}
}


void EventRecorder::processMillis(uint32 &millis) {
	uint32 d;
	if (_recordMode == kPassthrough) {
		return;
	}

	g_system->lockMutex(_timeMutex);
	if (_recordMode == kRecorderRecord) {
		return;
	}

	if (_recordMode == kRecorderPlayback) {
		if (_recordTimeCount > _playbackTimeCount) {
			uint32 _millisDelay;
			_millisDelay = millis - _lastMillis;
			_lastMillis = millis;
			if (_recordMode != kRecorderPlaybackPause) {
				_fakeTimer += _millisDelay * _engineSpeedMultiplier;
			}
			millis = _fakeTimer;
		}
	}

	g_system->unlockMutex(_timeMutex);
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
	checkForKeyCode(ev);
	if (_recordMode != kRecorderRecord)
		return false;
	_eventsQueue.push(ev);

	return false;
}

bool EventRecorder::notifyPoll() {
	return false;
}

bool EventRecorder::pollEvent(Event &ev) {
	if (_recordMode != kRecorderPlayback)
		return false;
	StackLock lock(_recorderMutex);

	if (_eventsQueue.empty())
		return false;
	ev = _eventsQueue.pop();
	switch (ev.type) {
	case EVENT_MOUSEMOVE:
	case EVENT_LBUTTONDOWN:
	case EVENT_LBUTTONUP:
	case EVENT_RBUTTONDOWN:
	case EVENT_RBUTTONUP:
	case EVENT_WHEELUP:
	case EVENT_WHEELDOWN:
		g_system->warpMouse(ev.mouse.x, ev.mouse.y);
		break;
	default:
		break;
	}
	return true;
}

void EventRecorder::sync()
{
	switch (_recordMode) {
	case kRecorderPlayback:
			readNextEventsChunk();
		break;
	case kRecorderRecord:
			writeNextEventsChunk();
		break;
	}
}

void EventRecorder::readNextEventsChunk()
{
	Event event;
	_eventsQueue.clear();
	uint16 recordsCount = _playbackFile->readUint16LE();
	for (uint16 i = 0; i < recordsCount; ++i) {
		readEvent(event);
		_eventsQueue.push(event);
	}
}

void EventRecorder::writeNextEventsChunk()
{
	Event event;
	_recordFile->writeUint16LE(_eventsQueue.size());
	while (!_eventsQueue.empty()) {
		event = _eventsQueue.pop();
		writeEvent(event);
	}
}

void EventRecorder::decreaseEngineSpeed()
{
	if (_engineSpeedMultiplier != 1){
		_engineSpeedMultiplier = _engineSpeedMultiplier / 2;
	}
}

void EventRecorder::increaseEngineSpeed()
{
	if (_engineSpeedMultiplier != 8) {
		_engineSpeedMultiplier = _engineSpeedMultiplier * 2;
	}
}

void EventRecorder::togglePause()
{
	switch (_recordMode) {
	case kRecorderPlayback:		
		_recordMode = kRecorderPlaybackPause;
		break;
	case kRecorderPlaybackPause:
		_recordMode = kRecorderPlayback;
		break;
	}
}

} // End of namespace Common
