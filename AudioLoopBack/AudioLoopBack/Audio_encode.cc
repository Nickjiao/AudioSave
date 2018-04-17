/*
* Copyright (c) 2001 Fabrice Bellard
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*/

/**
* @file
* audio encoding with libavcodec API example.
*
* @example encode_audio.c
*/
#include "Audio_encode.h"


/* check that a given sample format is supported by the encoder */
static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
	const enum AVSampleFormat *p = codec->sample_fmts;

	while (*p != AV_SAMPLE_FMT_NONE) {
		if (*p == sample_fmt)
			return 1;
		p++;
	}
	return 0;
}

void AudioEncoder::encode()
{
	int ret;
	
	/* send the frame for encoding */
	ret = avcodec_send_frame(codec_ctx_, frame_);
	if (ret < 0) {
		fprintf(stderr, "Error sending the frame to the encoder\n");
		exit(1);
	}

	/* read all the available output packets (in general there may be any
	* number of them */
	while (ret >= 0) {
		
		ret = avcodec_receive_packet(codec_ctx_, pkt_);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return;
		else if (ret < 0) {
			fprintf(stderr, "Error encoding audio frame\n");
			exit(1);
		}
		pkt_->stream_index = audio_stream_->index;

		if (pkt_->pts != AV_NOPTS_VALUE)
			pkt_->pts =
			av_rescale_q(pkt_->pts, codec_ctx_->time_base,
				fmt_ctx_->streams[0]->time_base);
		if (pkt_->dts != AV_NOPTS_VALUE)
			pkt_->dts =
			av_rescale_q(pkt_->dts, codec_ctx_->time_base,
				fmt_ctx_->streams[0]->time_base);

		ret = av_interleaved_write_frame(fmt_ctx_, pkt_);
		if (ret < 0)
			return;
		av_packet_unref(pkt_);
	}
}


AudioEncoder::AudioEncoder(char *file_name, int64_t bit_rate, enum AVSampleFormat sample_fmt) {
	file_name_ = file_name;
	bit_rate_ = bit_rate;
	sample_fmt_ = sample_fmt;

	codec_ = NULL;
	codec_ctx_ = NULL;
	frame_ = NULL;
	pkt_ = NULL;

	pts_ = 0;
}

AudioEncoder::~AudioEncoder() {
	//Write Trailer  

	av_write_trailer(fmt_ctx_);
	avio_close(fmt_ctx_->pb);
	avcodec_free_context(&codec_ctx_);
	av_frame_free(&frame_);
	av_packet_free(&pkt_);
	swr_close(audio_convert_ctx_);
	avformat_free_context(fmt_ctx_);
}

int AudioEncoder::Init() {

	int i, j, k, ret;
	uint16_t *samples;
	float t, tincr;

	audio_convert_ctx_ = swr_alloc();

	fmt_ctx_ = avformat_alloc_context();

	fmt_ = av_guess_format(NULL, file_name_, NULL);

	fmt_ctx_->oformat = fmt_;

	/* find the MP2 encoder */
	codec_ = avcodec_find_encoder(fmt_->audio_codec);
	if (!codec_) {
		fprintf(stderr, "Codec not found\n");
		exit(1);
	}

	codec_ctx_ = avcodec_alloc_context3(codec_);
	if (!codec_ctx_) {
		fprintf(stderr, "Could not allocate audio codec context\n");
		exit(1);
	}

	codec_ctx_->codec_id = fmt_->audio_codec;
	codec_ctx_->bit_rate = bit_rate_;

	/* check that the encoder supports s16 pcm input */
	codec_ctx_->sample_fmt = sample_fmt_;
	if (!check_sample_fmt(codec_, codec_ctx_->sample_fmt)) {
		fprintf(stderr, "Encoder does not support sample format %s",
			av_get_sample_fmt_name(codec_ctx_->sample_fmt));
		exit(1);
	}

	/* select other audio parameters supported by the encoder */
	codec_ctx_->sample_rate = 48000;
	codec_ctx_->channel_layout = av_get_default_channel_layout(2);
	codec_ctx_->channels = 2;

	/* open it */
	if (avcodec_open2(codec_ctx_, codec_, NULL) < 0) {
		fprintf(stderr, "Could not open codec\n");
		exit(1);
	}

	audio_stream_ = avformat_new_stream(fmt_ctx_, nullptr);
	avcodec_parameters_from_context(audio_stream_->codecpar, codec_ctx_);

	//Open output URL  
	if (avio_open(&fmt_ctx_->pb, file_name_, AVIO_FLAG_WRITE) < 0) {
		printf("Failed to open output file!\n");
		return -1;
	}

	
	/* packet for holding encoded output */
	pkt_ = av_packet_alloc();
	if (!pkt_) {
		fprintf(stderr, "could not allocate the packet\n");
		exit(1);
	}

	/* frame containing input raw audio */
	frame_ = av_frame_alloc();
	if (!frame_) {
		fprintf(stderr, "Could not allocate audio frame\n");
		exit(1);
	}

	frame_->nb_samples = codec_ctx_->frame_size;
	frame_->format = codec_ctx_->sample_fmt;
	frame_->channel_layout = codec_ctx_->channel_layout;
	frame_->channels = codec_ctx_->channels;

	/* allocate the data buffers */
	ret = av_frame_get_buffer(frame_, 0);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate audio data buffers\n");
		exit(1);
	}

	audio_convert_ctx_ = swr_alloc_set_opts(audio_convert_ctx_, codec_ctx_->channel_layout, codec_ctx_->sample_fmt, codec_ctx_->sample_rate,
		codec_ctx_->channel_layout, AV_SAMPLE_FMT_S16, codec_ctx_->sample_rate, 0, NULL);

	if (swr_init(audio_convert_ctx_) < 0) {
		fprintf(stderr, "swr_init() failed\n");
		return -1;
	}

	//Write Header  
	avformat_write_header(fmt_ctx_, NULL);

	av_init_packet(pkt_);

	return 0;
}

int AudioEncoder::process(int16_t *data, int nBytes) {

	int ret = av_frame_make_writable(frame_);
	if (ret < 0)
		exit(1);
	
	int len =  swr_convert(audio_convert_ctx_, frame_->data, frame_->nb_samples, (const uint8_t **)&data, nBytes / 4);
	frame_->pts = pts_++;
	encode();
}
