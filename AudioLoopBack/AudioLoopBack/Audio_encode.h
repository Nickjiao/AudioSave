#ifndef AUDIO_ENCODER
#define AUDIO_ENCODER

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>

#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample\swresample.h>
#include <libavformat\avformat.h>
#ifdef __cplusplus
}
#endif

class AudioEncoder {
public:
	AudioEncoder(char *file_name, int64_t bit_rate, enum AVSampleFormat sample_fmt);
	~AudioEncoder();

	int Init();
	int process(int16_t *data, int nBytes);
	void flush() {
		encode();
	}

private:
	void encode();

private:
	char *file_name_;
	int64_t bit_rate_;
	enum AVSampleFormat sample_fmt_;
	AVStream *audio_stream_;

	AVFormatContext* fmt_ctx_;
	AVOutputFormat* fmt_;
	AVCodec *codec_;
	AVCodecContext *codec_ctx_;
	AVFrame *frame_;
	AVPacket *pkt_;
	SwrContext *audio_convert_ctx_;

	int pts_;
};

#endif