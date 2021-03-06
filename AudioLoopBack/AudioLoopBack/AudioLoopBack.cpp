/**
* AudioLoopBack.cc, Apr. 17, 2018.
*
* Copyright 2018 haiquanjiao123@163.com. All rights reserved.
* haiquanjiao123@163.com PROPRIETARY/CONFIDENTIAL. Use is subject to license terms.
*/
#include <iostream>
#include <thread>
#include <cstring>
#include "AudioRecord.h"
#include "Audio_encode.h"

using std::thread;
extern bool start;

void little_sleep(std::chrono::minutes us)
{
	auto start = std::chrono::high_resolution_clock::now();
	auto end = start + us;
	do {
		std::this_thread::yield();
	} while (std::chrono::high_resolution_clock::now() < end);
}

void process() {

	int64_t bit_rate = 192000;
	enum AVSampleFormat sample_fmt = AV_SAMPLE_FMT_FLTP;
	char *filename = new char[20];
	//输出文件名
	strcpy(filename,"out.aac");
	AudioEncoder *audio_encoder_ = new AudioEncoder(filename,bit_rate,sample_fmt);
	audio_encoder_->Init();

	int16_t *data_out = NULL;

	const int FRAME_LEN = 1024;
	const int FRAME_LEN1 = 1024 << 1;
	const int FRAME_LEN2 = 1024 << 2;

	//一个采样点包含左、右两个信道，共1024采样点
	int16_t *buffer = new int16_t[FRAME_LEN2];

	memset(buffer,0, FRAME_LEN2 * sizeof(int16_t));
	int index = 0;

	while (start) {
		int nBytes = 0;
		data_out = AUDIOLOOPBACK::GetAudio(nBytes);
		memcpy(buffer + index, data_out, nBytes);
		index += (nBytes / 2);
		while (index >= FRAME_LEN1) {
			audio_encoder_->process(buffer, FRAME_LEN2);//FRAME_LEN * channels * sizeof(int16_t)
			memmove(buffer, buffer + FRAME_LEN1, (index - FRAME_LEN1) * sizeof(int16_t));
			index -= FRAME_LEN1;
		}
		delete[] data_out;
		data_out = NULL;
	}
	audio_encoder_->flush();
	delete audio_encoder_;
	delete[] buffer;
	delete[] filename;
	buffer = NULL;
	filename = NULL;
}

int main()
{
	thread t1(process);

	AUDIOLOOPBACK::AudioRecord *audio_record = new AUDIOLOOPBACK::AudioRecord();
	audio_record->Start();
	//设置录音时间
	little_sleep(std::chrono::minutes(1));
	audio_record->Stop();
	delete audio_record;
	audio_record = NULL;
	t1.join();
    return 0;
}

