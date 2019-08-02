#pragma once
#include "VideoPacket.h"
#include <string>

class VideoPacket;

extern "C"
{
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
}


class Pusher
{
	AVFormatContext* m_format_context{};
	AVCodecContext* m_vcodec_context;
	AVCodecContext* m_acodec_context;
	int64_t m_start_time{};

	SwrContext* _swrCtx = nullptr;
	AVFrame* _pcmFrame = nullptr;
	uint32_t _aPts = 0;
	uint64_t total_bytes = 0;

	bool m_stopped;
	void free_all();
public:
	Pusher();
	~Pusher();
	void start_push(std::string url, int width, int height, int frame_rate);
	void stop_push();
	void push_video_packet(VideoPacket* video_packet);
	void push_audio_packet(AVPacket* packet);
	AVPacket* encode_audio_data();
};
