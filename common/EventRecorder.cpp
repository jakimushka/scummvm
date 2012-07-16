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

#define MAX_RECORDS_NAMES 0x64
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
	_initialized = false;
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
	_initialized = false;
	_recordMode = kPassthrough;
	debugC(3, kDebugLevelEventRec, "EventRecorder: deinit");
	g_system->getEventManager()->getEventDispatcher()->unregisterSource(this);
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);
	g_system->lockMutex(_timeMutex);
	g_system->lockMutex(_recorderMutex);
	_recordMode = kPassthrough;
	_playbackFile.close();
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
	if (!_initialized) {
		return;
	}
	uint32 millisDelay;
	RecorderEvent timerEvent;
	switch (_recordMode) {
	case kRecorderRecord:
		updateSubsystems();
		millisDelay = millis - _lastMillis;
		_lastMillis = millis;
		_fakeTimer += millisDelay;
		timerEvent.type = EVENT_TIMER;
		timerEvent.time = _fakeTimer;
		_playbackFile.writeEvent(timerEvent);
		takeScreenshot();
		_timerManager->handler(_fakeTimer);
		break;
	case kRecorderPlayback:
		updateSubsystems();
		if (_nextEvent.type == EVENT_TIMER) {
			_fakeTimer = _nextEvent.time;
			_nextEvent = _playbackFile.getNextEvent();
		}
		_timerManager->handler(_fakeTimer);
		millis = _fakeTimer;
		break;
	case kRecorderPlaybackPause:
		millis = _fakeTimer;
		break;
	default:
		break;
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
	if (_recordMode != kRecorderRecord)
		return false;
	if (!_initialized) {
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
	if (!_initialized) {
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
		if (!_initialized) {
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
		if (!_initialized) {
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

Common::String EventRecorder::generateRecordFileName(const String &target) {
	ConfMan.getActiveDomainName();
	Common::String pattern(target+".r??");
	Common::StringArray files = g_system->getSavefileManager()->listSavefiles(pattern);
	for (int i = 0; i < MAX_RECORDS_NAMES; ++i) {
		Common::String recordName = Common::String::format("%s.r%02d", target.c_str(), i);
		if (Common::find(files.begin(), files.end(), recordName) != files.end()) {
			continue;
		}
		return recordName;
	}
	return "";
}


void EventRecorder::init(Common::String recordFileName, RecordMode mode) {
	_fakeTimer = 0;
	_lastMillis = 0;
	_engineSpeedMultiplier = 1;
	_lastScreenshotTime = g_system->getMillis();
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
		TimeDate t;
		const EnginePlugin *plugin = 0;
		GameDescriptor desc = EngineMan.findGame(ConfMan.getActiveDomainName(), &plugin);
		g_system->getTimeAndDate(t);
		if (_playbackFile.getHeader().author.empty()) {
			setAuthor("Unknown Author");
		}
		if (_playbackFile.getHeader().name.empty()) {
			g_eventRec.setName(Common::String::format("%.2d.%.2d.%.4d ", t.tm_mday, t.tm_mon, 1900 + t.tm_year) + desc.description());
		}
		getConfig();
	}
	switchMixer();
	switchTimerManagers();
	_initialized = true;
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
		_playbackFile.getHeader().settingsRecords[entry->_key] = entry->_value;
	}
}

void EventRecorder::getConfig() {
	getConfigFromDomain(ConfMan.getDomain(ConfMan.kApplicationDomain));
	getConfigFromDomain(ConfMan.getActiveDomain());
	_playbackFile.getHeader().settingsRecords["save_slot"] = ConfMan.get("save_slot");
}


void EventRecorder::applyPlaybackSettings() {
	for (StringMap::iterator i = _playbackFile.getHeader().settingsRecords.begin(); i != _playbackFile.getHeader().settingsRecords.end(); ++i) {
		String currentValue = ConfMan.get(i->_key);
		if (currentValue != i->_value) {
			warning("Config value <%s>: %s -> %s", i->_key.c_str(), i->_value.c_str(), currentValue.c_str());
			ConfMan.set(i->_key, i->_value, ConfMan.kTransientDomain);
		}
	}
	removeDifferentEntriesInDomain(ConfMan.getDomain(ConfMan.kApplicationDomain));
	removeDifferentEntriesInDomain(ConfMan.getActiveDomain());
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
	checkForKeyCode(ev);
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

void EventRecorder::takeScreenshot() {
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

Common::SeekableReadStream * EventRecorder::getSaveStream() {
	return new MemoryReadStream(saveBuffer, 0);
}

} // End of namespace Common
