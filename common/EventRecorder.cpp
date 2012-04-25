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
#include "backends/timer/sdl/sdl-timer.h"
#include "backends/mixer/sdl/sdl-mixer.h"
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

void EventRecorder::readEvent(RecorderEvent &event) {
	_recordCount++;
	event.type = (EventType)_playbackFile->readUint32LE();
	switch (event.type) {	case EVENT_TIMER:
		event.time = _playbackFile->readUint32LE();
		event.count = _playbackFile->readUint32LE();
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

void EventRecorder::writeEvent(const Event &event) {
	_recordMode = kPassthrough;
	_recordFile->writeUint32LE((uint32)event.type);
	_recordCount++;

	switch (event.type) {
	case EVENT_TIMER:
		_recordFile->writeUint32LE((uint32)_fakeTimer);
		_recordFile->writeUint32LE((uint32)_eventCount);
		break;
	case EVENT_DELAY:
		_recordFile->writeUint32LE((uint32)_delayMillis);
		break;
	case EVENT_KEYDOWN:
	case EVENT_KEYUP:
		_recordFile->writeUint32LE((uint32)_fakeTimer);
		_recordFile->writeSint32LE(event.kbd.keycode);
		_recordFile->writeUint16LE(event.kbd.ascii);
		_recordFile->writeByte(event.kbd.flags);
		break;
	case EVENT_AUDIO:
		_recordFile->writeUint32LE((uint32)_fakeTimer);
		_recordFile->writeUint32LE(_samplesCount);
		break;
	case EVENT_LBUTTONDOWN:
	case EVENT_LBUTTONUP:
	case EVENT_RBUTTONDOWN:
	case EVENT_RBUTTONUP:
	case EVENT_WHEELUP:
	case EVENT_WHEELDOWN:
	case EVENT_MBUTTONDOWN:
	case EVENT_MBUTTONUP:
		_recordFile->writeUint32LE((uint32)_fakeTimer);
		_recordFile->writeSint16LE(event.mouse.x);
		_recordFile->writeSint16LE(event.mouse.y);
		break;
	default:
		_recordFile->writeUint32LE((uint32)_fakeTimer);
		break;
	}
	_recordMode = kRecorderRecord;
}


EventRecorder::EventRecorder() {
	_recordFile = NULL;
	_recordTimeFile = NULL;
	_playbackFile = NULL;
	_playbackTimeFile = NULL;
	_timeMutex = g_system->createMutex();
	_recorderMutex = g_system->createMutex();

	_eventCount = 1;
	_lastEventCount = 0;
	_lastMillis = 0;
	_lastEventMillis = 0;
	_engineSpeedMultiplier = 1;

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

	_recordCount = 0;
	// recorder stuff
	if (_recordMode == kRecorderRecord) {
		_recordTimeCount = 0;
		_recordFile = wrapBufferedWriteStream(g_system->getSavefileManager()->openForSaving(_recordFileName), 128 * 1024);
		_recordTimeFile = wrapBufferedWriteStream(g_system->getSavefileManager()->openForSaving(_recordTimeFileName), 128 * 1024);
		_recordSubtitles = ConfMan.getBool("subtitles");
	}

	_fakeTimer = 0;

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
		getNextEvent();
	}

}

void EventRecorder::deinit() {
	debug(3, "EventRecorder: deinit");

	g_system->getEventManager()->getEventDispatcher()->unregisterSource(this);
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);

	g_system->lockMutex(_timeMutex);
	g_system->lockMutex(_recorderMutex);
	_recordMode = kPassthrough;
	delete _playbackFile;
	delete _playbackTimeFile;

	if (_recordFile != NULL) {
		_recordFile->finalize();
		delete _recordFile;
		_recordTimeFile->finalize();
		delete _recordTimeFile;
	}
	g_system->unlockMutex(_timeMutex);
	g_system->unlockMutex(_recorderMutex);
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

bool EventRecorder::delayMillis(uint msecs, bool logged) {
	if (_recordMode == kRecorderRecord)	{
		Common::Event delayEvent;
		delayEvent.type = EVENT_DELAY;
		_delayMillis = msecs;
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
 	_mixer->update();
	if (_recordMode == kRecorderRecord) {
		StackLock lock(_recorderMutex);
		uint32 _millisDelay;
		_millisDelay = millis - _lastMillis;
		_lastMillis = millis;
		_fakeTimer += _millisDelay;
		Common::Event timerEvent;
		timerEvent.type = EVENT_TIMER;
		writeEvent(timerEvent);
	}

	if (_recordMode == kRecorderPlayback) {
		uint32 _millisDelay;
		uint32 audioTime = 0;
		StackLock lock(_recorderMutex);
		if (_nextEvent.type == EVENT_TIMER) {
			if (audioTime != 0) {
				debug("AudioEventTime = %d, TimerTime = %d",audioTime,_nextEvent.time);
			}
			_fakeTimer = _nextEvent.time;
			getNextEvent();
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
	if (ev.type == EVENT_MOUSEMOVE) return false;
	if ((ev.type == EVENT_LBUTTONDOWN) || (ev.type == EVENT_LBUTTONUP)) {
		debug("%d %d %d %d %d",ev.type,_fakeTimer,_fakeTimer,ev.mouse.x,ev.mouse.y);
	}

	writeEvent(ev);
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

	if ((_nextEvent.type == EVENT_MOUSEMOVE) || (_nextEvent.type == EVENT_TIMER) || (_nextEvent.type == EVENT_DELAY) || (_nextEvent.type == EVENT_AUDIO)) {
		return false;
	}

	if (_nextEvent.time > _fakeTimer) {
		return false;
	}
	if ((_nextEvent.type == EVENT_LBUTTONDOWN) || (_nextEvent.type == EVENT_LBUTTONUP)) {
		debug("%d %d %d %d %d",_nextEvent.type,_nextEvent.time,_fakeTimer,_nextEvent.mouse.x,_nextEvent.mouse.y);
	}
	
	switch (_nextEvent.type) {
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
	debug("Decrease speed: %d",_engineSpeedMultiplier);
}

void EventRecorder::increaseEngineSpeed() {
	if (_engineSpeedMultiplier != 8) {
		_engineSpeedMultiplier = _engineSpeedMultiplier * 2;
	}
	debug("Increase speed: %d",_engineSpeedMultiplier);
}

void EventRecorder::togglePause() {
	switch (_recordMode) {
	case kRecorderPlayback:	
		_recordMode = kRecorderPlaybackPause;
		debug("Pause");
		break;
	case kRecorderPlaybackPause:
		_recordMode = kRecorderPlayback;
		debug("Resume");
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
		Common::Event audioEvent;
		audioEvent.type =  EVENT_AUDIO;
		_samplesCount = samples;
		writeEvent(audioEvent);
		return true;
	}
	if (_recordMode == kRecorderPlayback) {

		if ((_nextEvent.type == EVENT_AUDIO) /*&& !paused */) {
			if (_nextEvent.time <= _fakeTimer) {
				_nextEvent.count;			
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

SdlMixerManager* EventRecorder::createMixerManager() {
	if (_recordMode == kPassthrough) {
		return new SdlMixerManager();
	}
	else {
		_mixer = new NullSdlMixerManager();
		return _mixer;
	}

}

void EventRecorder::RegisterEventSource() {
	g_system->getEventManager()->getEventDispatcher()->registerSource(this, false);
	g_system->getEventManager()->getEventDispatcher()->registerObserver(this, EventManager::kEventRecorderPriority, false, true);
}

} // End of namespace Common
