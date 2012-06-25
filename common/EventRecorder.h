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

#ifndef COMMON_EVENTRECORDER_H
#define COMMON_EVENTRECORDER_H

#include "common/scummsys.h"
#include "common/events.h"
#include "common/singleton.h"
#include "common/mutex.h"
#include "common/array.h"
#include "common/queue.h"
#include "common/memstream.h"
#include "backends/keymapper/keymapper.h"
#include "backends/mixer/sdl/sdl-mixer.h"
#include "backends/mixer/nullmixer/nullsdl-mixer.h"
#include "backends/timer/sdl/sdl-timer.h"
#include "backends/timer/default/default-timer.h"
#include "engines/advancedDetector.h"
#include "common/hashmap.h"
#include "common/config-manager.h"

#define g_eventRec (Common::EventRecorder::instance())

//capacity of records buffer
#define kMaxBufferedRecords 10000
#define kRecordBuffSize sizeof(RecorderEvent) * kMaxBufferedRecords

namespace Common {

class RandomSource;
class SeekableReadStream;
class WriteStream;


struct RecorderEvent : Common::Event {
	uint32 time;
	uint32 count;
};


struct ChunkHeader {
	uint32 id;
	uint32 len;
};



class PlaybackFile {
	struct PlaybackFileHeader {
		Common::String author;
		Common::String name;
		Common::String notes;
		Common::String description;
		Common::StringMap hashRecords;
		uint32 settingsSectionSize;
		Common::StringMap settingsRecords;
		HashMap<String, uint32, IgnoreCase_Hash, IgnoreCase_EqualTo> randomSourceRecords;
		PlaybackFileHeader() {
			settingsSectionSize = 0;
		}
		void setSettings(const String& key, const String& value) {
			settingsRecords[key] = value;
			settingsSectionSize += key.size() + value.size() + 24;
		}
	} _header;
	enum fileMode {
		kRead = 0,
		kWrite = 1
	};
	enum PlaybackFileState {
		kFileStateCheckFormat,
		kFileStateCheckVersion,
		kFileStateProcessHash,
		kFileStateProcessHeader,
		kFileStateProcessRandom,
		kFileStateReadRnd,
		kFileStateSelectSection,
		kFileStateProcessSettings,
		kFileStateProcessSettingsRecord,
		kFileStateDone,
		kFileStateError
	};
	enum fileTags {
		kFormatIdTag = MKTAG('P','B','C','K'),
		kVersionTag = MKTAG('V','E','R','S'),
		kHeaderSectionTag = MKTAG('H','E','A','D'),
		kHashSectionTag = MKTAG('H','A','S','H'),
		kRandomSectionTag = MKTAG('R','A','N','D'),
		kEventTag = MKTAG('E','V','N','T'),
		kScreenShotTag = MKTAG('B','M','H','T'),
		kSettingsSectionTag = MKTAG('S','E','T','T'),
		kAuthorTag = MKTAG('H','A','U','T'),
		kCommentsTag = MKTAG('H','C','M','T'),
		kHashRecordTag = MKTAG('H','R','C','D'),
		kRandomRecordTag = MKTAG('R','R','C','D'),
		kSettingsRecordTag = MKTAG('S','R','E','C'),
		kSettingsRecordKeyTag = MKTAG('S','K','E','Y'),
		kSettingsRecordValueTag = MKTAG('S','V','A','L'),
		kMD5Tag = MKTAG('M','D','5',' ')
	};
public:
	PlaybackFile();
	~PlaybackFile();
	bool openWrite(Common::String fileName);
	bool openRead(Common::String fileName);
	void close();
	bool parseHeader();
	Common::RecorderEvent getNextEvent();
	bool isEventsBufferEmpty();
	PlaybackFileHeader &getHeader() {return _header;}
private:
	uint32 _eventsSize;
	byte _tmpBuffer[kRecordBuffSize];
	SeekableMemoryWriteStream _tmpRecordFile;
	MemoryReadStream _tmpPlaybackFile;

	fileMode _mode;
	SeekableReadStream *_readStream;
	WriteStream *_writeStream;

	PlaybackFileState _playbackParseState;

	ChunkHeader readChunkHeader();
	Common::String readString(int len);
	bool processChunk(ChunkHeader &nextChunk);
	void returnToChunkHeader();
	bool checkPlaybackFileVersion();
	void readHashMap(ChunkHeader chunk);
	void processRndSeedRecord(ChunkHeader chunk);
	bool processSettingsRecord(ChunkHeader chunk);
	void readEvent(RecorderEvent& event);
	void readEventsToBuffer(uint32 size);
};

/**
 * Our generic event recorder.
 *
 * TODO: Add more documentation.
 */
class EventRecorder : private EventSource, private EventObserver, public Singleton<EventRecorder>, private DefaultEventMapper {
	friend class Singleton<SingletonBaseType>;
	EventRecorder();
	~EventRecorder();
public:
	enum RecordMode {
		kPassthrough = 0,
		kRecorderRecord = 1,
		kRecorderPlayback = 2,
		kRecorderPlaybackPause = 3
	};
	void init();
	void init(Common::String gameid, RecordMode mode);
	void init(const ADGameDescription *desc, RecordMode mode);
	void deinit();
	bool delayMillis(uint msecs, bool logged = false);
	/** TODO: Add documentation, this is only used by the backend */
	void processMillis(uint32 &millis);
	bool processAudio(uint32 &samples, bool paused);
	void sync();
	SdlMixerManager *getMixerManager();
	DefaultTimerManager *getTimerManager();
	void setAuthor(const Common::String &author);
	void setNotes(const Common::String &desc);
	void setName(const Common::String &name);
	/** Register random source so it can be serialized in game test purposes */
	uint32 getRandomSeed(const String &name);
	void processGameDescription(const ADGameDescription *desc);
	void registerMixerManager(SdlMixerManager *mixerManager);
	void registerTimerManager(DefaultTimerManager *timerManager);
	uint32 getTimer() {return _fakeTimer;}
	void deleteRecord(const String& fileName);
	bool isRecording() {
		return initialized;
	}
	void RegisterEventSource();
private:	
	typedef HashMap<String, uint32, IgnoreCase_Hash, IgnoreCase_EqualTo> randomSeedsDictionary;
	virtual List<Event> mapEvent(const Event &ev, EventSource *source);
	bool initialized;
	void setGameMd5(const ADGameDescription *gameDesc);
	ChunkHeader readChunkHeader();
	void getConfig();
	void applyPlaybackSettings();
	void removeDifferentEntriesInDomain(ConfigManager::Domain *domain);
	void getConfigFromDomain(ConfigManager::Domain *domain);
	bool processChunk(ChunkHeader &nextChunk);
	void updateSubsystems();
	bool _headerDumped;
	MutexRef _recorderMutex;
	SdlMixerManager *_realMixerManager;
	NullSdlMixerManager *_fakeMixerManager;
	DefaultTimerManager *_timerManager;
	void switchMixer();
	void switchTimerManagers();
	void writeVersion();
	void writeHeader();
	void writeFormatId();
	bool grabScreenAndComputeMD5(Graphics::Surface &screen, uint8 md5[16]);
	void writeGameHash();
	void writeRandomRecords();
	bool openRecordFile(const String &fileName);
	bool checkGameHash(const ADGameDescription *desc);
	bool notifyEvent(const Event &ev);
	String findMD5ByFileName(const ADGameDescription *gameDesc, const String &fileName);
	bool notifyPoll();
	bool pollEvent(Event &ev);
	bool allowMapping() const { return false; }
	void writeNextEventsChunk();
	void writeEvent(const RecorderEvent &event);
	void checkForKeyCode(const Event &event);
	void writeAudioEvent(uint32 samplesCount);
	void writeGameSettings();
	void readAudioEvent();
	void increaseEngineSpeed();
	void decreaseEngineSpeed();
	void togglePause();
	void dumpRecordsToFile();
	void dumpHeaderToFile();
	void writeScreenSettings();
	RecorderEvent _nextEvent;
	uint8 _engineSpeedMultiplier;
	volatile uint32 _recordCount;
	volatile uint32 _recordSize;
	byte _recordBuffer[kRecordBuffSize];
	SeekableMemoryWriteStream _tmpRecordFile;
	WriteStream *_recordFile;
	WriteStream *_screenshotsFile;
	MutexRef _timeMutex;
	volatile uint32 _lastMillis;
	volatile uint32 _fakeTimer;
	uint32 _lastScreenshotTime;
	uint32 _screenshotPeriod;
	PlaybackFile _playbackFile;
	void saveScreenShot();
	void checkRecordedMD5();
	volatile RecordMode _recordMode;
};

} // End of namespace Common

#endif
