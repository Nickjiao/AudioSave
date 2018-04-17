#ifndef AUDIO_RECORD
#define AUDIO_RECORD

#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <process.h>
#include <avrt.h>

#include <mutex>
#include <condition_variable>
#include <queue>

#include <string>

namespace AUDIOLOOPBACK {

int16_t *GetAudio(int& nBytes);
int SaveAudio(int16_t *data, int len);

class AudioRecord {
public:
	AudioRecord();
	AudioRecord(unsigned int dur_time, std::string format);
	~AudioRecord();

	int Start();
	int Stop();

private:
	unsigned int dur_time_;
	std::string format_;
};
}//AUDIOLOOPBACK

#endif