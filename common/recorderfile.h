#ifndef RECORDERFILE_H
#define RECORDERFILE_H

#include "common/scummsys.h"
#include "common/events.h"
#include "common/mutex.h"
#include "common/memstream.h"
#include "common/config-manager.h"
#include "common/savefile.h"

//capacity of records buffer
#define kMaxBufferedRecords 10000
#define kRecordBuffSize sizeof(RecorderEvent) * kMaxBufferedRecords

namespace Common {

struct RecorderEvent : Common::Event {
	uint32 time;
	uint32 count;
};



class PlaybackFile {
	typedef HashMap<String, uint32, IgnoreCase_Hash, IgnoreCase_EqualTo> RandomSeedsDictionary;
	enum fileMode {
		kRead = 0,
		kWrite = 1,
		kClosed = 2
	};
	enum PlaybackFileState {
		kFileStateCheckFormat,
		kFileStateCheckVersion,
		kFileStateProcessHash,
		kFileStateProcessHeader,
		kFileStateProcessRandom,
		kFileStateSelectSection,
		kFileStateProcessSettings,
		kFileStateProcessSave,
		kFileStateDone,
		kFileStateError
	};
	enum FileTag {
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
		kNameTag = MKTAG('H','N','A','M'),
		kHashRecordTag = MKTAG('H','R','C','D'),
		kRandomRecordTag = MKTAG('R','R','C','D'),
		kSettingsRecordTag = MKTAG('S','R','E','C'),
		kSettingsRecordKeyTag = MKTAG('S','K','E','Y'),
		kSettingsRecordValueTag = MKTAG('S','V','A','L'),
		kSaveTag = MKTAG('S','A','V','E'),
		kSaveRecordTag = MKTAG('R','S','A','V'),
		kSaveRecordNameTag = MKTAG('S','N','A','M'),
		kSaveRecordBufferTag = MKTAG('S','B','U','F'),
		kMD5Tag = MKTAG('M','D','5',' ')
	};
	struct ChunkHeader {
		FileTag id;
		uint32 len;
	};
public:
	struct SaveFileBuffer {
		byte *buffer;
		uint32 size;
	};
	struct PlaybackFileHeader {
		Common::String fileName;
		Common::String author;
		Common::String name;
		Common::String notes;
		Common::String description;
		Common::StringMap hashRecords;
		Common::StringMap settingsRecords;
		Common::HashMap<Common::String, SaveFileBuffer> saveFiles;
		RandomSeedsDictionary randomSourceRecords;
	};
	PlaybackFile();
	~PlaybackFile();
	bool openWrite(Common::String fileName);
	bool openRead(Common::String fileName);
	void close();
	Common::RecorderEvent getNextEvent();
	void writeEvent(const RecorderEvent &event);
	void saveScreenShot(Graphics::Surface &screen, byte md5[16]);
	bool isEventsBufferEmpty();
	PlaybackFileHeader &getHeader() {return _header;}
	int getScreensCount();
	Graphics::Surface *getScreenShot(int number);
	void updateHeader();
	void addSaveFile(const Common::String &fileName, Common::InSaveFile *saveStream);
private:
	WriteStream *_screenshotsFile;
	PlaybackFileHeader _header;
	int _recordCount;
	int _headerDumped;
	uint32 _eventsSize;
	byte _tmpBuffer[kRecordBuffSize];
	SeekableMemoryWriteStream _tmpRecordFile;
	MemoryReadStream _tmpPlaybackFile;
	WriteStream *_recordFile;
	fileMode _mode;
	SeekableReadStream *_readStream;
	WriteStream *_writeStream;
	PlaybackFileState _playbackParseState;
	void skipHeader();
	void writeSaveFilesSection();
	void writeGameSettings();
	void writeHeaderSection();
	void writeGameHash();
	void writeRandomRecords();
	bool parseHeader();
	void dumpRecordsToFile();
	void dumpHeaderToFile();
	bool readSaveRecord();
	bool skipToNextScreenshot();
	bool readChunkHeader(ChunkHeader &nextChunk);
	Common::String readString(int len);
	bool processChunk(ChunkHeader &nextChunk);
	void returnToChunkHeader();
	bool checkPlaybackFileVersion();
	void readHashMap(ChunkHeader chunk);
	void processRndSeedRecord(ChunkHeader chunk);
	bool processSettingsRecord();
	void readEvent(RecorderEvent& event);
	void readEventsToBuffer(uint32 size);
	void checkRecordedMD5();
	bool grabScreenAndComputeMD5(Graphics::Surface &screen, uint8 md5[16]);
};

} // End of namespace Common

#endif
