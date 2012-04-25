#ifndef BACKENDS_MIXER_NULLSDL_H
#define BACKENDS_MIXER_NULLSDL_H

#include "backends/mixer/sdl/sdl-mixer.h"
#include "common/str.h"

class NullSdlMixerManager :	public SdlMixerManager {
public:
	NullSdlMixerManager();
	virtual ~NullSdlMixerManager();

	virtual void init();

	virtual void suspendAudio();

	virtual int resumeAudio();

	void update();

protected:

	virtual void startAudio();

	virtual void callbackHandler(byte *samples, int len);

private:
	Common::WriteStream *_audioFile;
	Common::String 	_audioFileName;
	uint16 _outputRate;
	uint32 _callsCounter;
	uint8  _callbackPeriod;
	uint32 _samples;
	uint8* _samplesBuf;
};

#endif
