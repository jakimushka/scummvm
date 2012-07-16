#ifndef BACKEND_SAVES_RECORDER_H
#define BACKEND_SAVES_RECORDER_H

#include "backends/saves/default/default-saves.h"

/**
 * Provides a savefile manager implementation for event recorder.
 */
class RecorderSaveFileManager : public DefaultSaveFileManager {
	virtual Common::InSaveFile *openForLoading(const Common::String &filename);
};

#endif
