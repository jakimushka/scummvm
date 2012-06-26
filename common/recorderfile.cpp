#include "common/recorderfile.h"
#include "common/savefile.h"
#include "common/bufferedstream.h"
#include "graphics/thumbnail.h"

#define RECORD_VERSION 1

namespace Common {

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
	close();
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

int PlaybackFile::getScreensCount() {
	if (_mode != kRead) {
		return 0;
	}
	_readStream->seek(0);
	uint32 id = _readStream->readUint32LE();
	_readStream->skip(4);
	int result = 0;
	while (skipToNextScreenshot()) {
		uint32 size = _readStream->readUint32BE();
		_readStream->skip(size-8);
		++result;
	}
	return result;
}

bool PlaybackFile::skipToNextScreenshot() {
	while (true) {
		uint32 id = _readStream->readUint32LE();
		if (_readStream->eos()) {
			break;
		}
		if (id == kScreenShotTag) {
			return true;
		}
		else {
			uint32 size = _readStream->readUint32LE();
			_readStream->skip(size);
		}
	}
	return false;
}

Graphics::Surface *PlaybackFile::getScreenShot(int number) {
	if (_mode != NULL) {
		return NULL;
	}
	_readStream->seek(0);
	uint32 id = _readStream->readUint32LE();
	_readStream->skip(4);
	int screenCount = 1;
	while (skipToNextScreenshot()) {
		if (screenCount == number) {
			screenCount++;
			_readStream->seek(-4, SEEK_CUR);
			return Graphics::loadThumbnail(*_readStream);
		} else {
			uint32 size = _readStream->readUint32BE();
			_readStream->skip(size-8);
			screenCount++;
		}
	}
	return NULL;
}
}
