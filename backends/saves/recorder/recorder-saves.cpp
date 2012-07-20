#include "backends/saves/recorder/recorder-saves.h"
#include "common/EventRecorder.h"
#include "common/savefile.h"

Common::InSaveFile *RecorderSaveFileManager::openForLoading(const Common::String &filename) {
	Common::InSaveFile *result = g_eventRec.processSaveStream(filename);
	return result;
}

Common::StringArray RecorderSaveFileManager::listSaveFiles(const Common::String &pattern) {
	return g_eventRec.listSaveFiles(pattern);
}

