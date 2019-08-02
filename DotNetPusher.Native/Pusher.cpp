#include "stdafx.h"
#include "Pusher.h"
#include "ErrorCode.h"
#include "Utils.h"
#include <string>
#include <exception>
#include <iostream>

extern "C"
{
#include "libavutil/time.h"
#include "libavutil/avutil.h"
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h"
#include "libavutil/frame.h"
#include "libavutil/samplefmt.h"
#include "libavformat/avformat.h"
}

Pusher::Pusher()
{
	//Use av_log_set_level will make the application very slow.
	//av_log_set_level(AV_LOG_DEBUG);
	m_stopped = true;
	m_start_time = av_gettime();
	m_format_context = nullptr;
	m_acodec_context = nullptr;
	m_vcodec_context = nullptr;
	//av_register_all();
	avformat_network_init();
}

Pusher::~Pusher()
{
	if (!m_stopped)
	{
		stop_push();
	}
}

void Pusher::free_all()
{
	if (m_format_context != nullptr)
	{
		avformat_free_context(m_format_context);
		m_format_context = nullptr;
	}
	if (m_vcodec_context != nullptr)
	{
		avcodec_close(m_vcodec_context);
		avcodec_free_context(&m_vcodec_context);
		m_vcodec_context = nullptr;
	}
	if (m_acodec_context != nullptr)
	{
		avcodec_close(m_acodec_context);
		avcodec_free_context(&m_acodec_context);
		m_acodec_context = nullptr;
	}
}

void Pusher::start_push(std::string url, int width, int height, int frame_rate)
{
	m_stopped = false;
	//RTMP is using flv format.

	auto ret = avformat_alloc_output_context2(&m_format_context, nullptr, "flv", url.c_str());
	if (ret < 0) 
	{
		Utils::write_log("Can not create output context.");
		throw ERROR_CREATE_PUSHER;
	}

	m_format_context->oformat->video_codec = AV_CODEC_ID_H264;
	m_format_context->oformat->audio_codec = AV_CODEC_ID_AAC;

	//Try find video codec
	const auto format = m_format_context->oformat;
	const auto vcodec = avcodec_find_encoder(format->video_codec);
	printf("found video codec encoder: %s\n", vcodec->name);
	if (vcodec == nullptr)
	{
		free_all();
		Utils::write_log("Can not find codec.");
		throw ERROR_CREATE_PUSHER;
	}

	//Try find audio codec
	const auto acodec = avcodec_find_encoder(format->audio_codec);
	printf("found audeo codec encoder: %s\n", acodec->name);
	if (acodec == nullptr)
	{
		free_all();
		Utils::write_log("Can not find audio codec.");
		throw ERROR_CREATE_PUSHER;
	}

	//new video stream
	const auto out_vstream = avformat_new_stream(m_format_context, vcodec);
	printf("new out_vstream: %d\n", out_vstream->index);
	if (out_vstream == nullptr)
	{
		free_all();
		Utils::write_log("Can not create new output stream.");
		throw ERROR_CREATE_PUSHER;
	}

	//new audio stream
	const auto out_astream = avformat_new_stream(m_format_context, acodec);
	printf("new out_astream: %d\n", out_astream->index);
	if (out_astream == nullptr)
	{
		free_all();
		Utils::write_log("Can not create new output stream.");
		throw ERROR_CREATE_PUSHER;
	}

	//create video context
	m_vcodec_context = avcodec_alloc_context3(vcodec);
	if (m_vcodec_context == nullptr)
	{
		free_all();
		Utils::write_log("Can not create video codec context.");
		throw ERROR_CREATE_PUSHER;
	}

	m_vcodec_context->codec_id = format->video_codec;
	m_vcodec_context->width = width; //Width
	m_vcodec_context->height = height; //Height
	m_vcodec_context->time_base = { 1,frame_rate };
	m_vcodec_context->pix_fmt = AV_PIX_FMT_YUV420P;

	if (format->flags & AVFMT_GLOBALHEADER)
		m_vcodec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	////Open video codec
	if (avcodec_open2(m_vcodec_context, vcodec, nullptr) < 0)
	{
		free_all();
		Utils::write_log("Can not open codec.");
		throw ERROR_CREATE_PUSHER;
	}
	printf("Open video stream: %dx%d\n", m_vcodec_context->width, m_vcodec_context->height);

	//Copy the settings of VCodecContext
	ret = avcodec_parameters_from_context(out_vstream->codecpar, m_vcodec_context);
	if (ret < 0)
	{
		free_all();
		Utils::write_log("Can not copy parameters from codec context.");
		throw ERROR_CREATE_PUSHER;
	}
	out_vstream->codecpar->codec_tag = 0;
	
	//create audio context
	m_acodec_context = avcodec_alloc_context3(acodec);
	if (m_acodec_context == nullptr)
	{
		free_all();
		Utils::write_log("Can not create audio codec context.");
		throw ERROR_CREATE_PUSHER;
	}

	m_acodec_context->codec_id = format->audio_codec;
	m_acodec_context->sample_rate = 44100;
	m_acodec_context->bit_rate = 64000;
	m_acodec_context->time_base = { 1,frame_rate };
	m_acodec_context->channels = 2;
	m_acodec_context->channel_layout = av_get_default_channel_layout(m_acodec_context->channels);
	m_acodec_context->sample_fmt = acodec->sample_fmts ? acodec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;

	////Open audio codec
	if (avcodec_open2(m_acodec_context, acodec, nullptr) < 0)
	{
		free_all();
		Utils::write_log("Can not open codec.");
		throw ERROR_CREATE_PUSHER;
	}
	printf("Open audio stream: bitrate:%d  sample_rate:%d\n", m_acodec_context->bit_rate, m_acodec_context->sample_rate);

	//Copy the settings of ACodecContext
	ret = avcodec_parameters_from_context(out_astream->codecpar, m_acodec_context);
	if (ret < 0)
	{
		free_all();
		Utils::write_log("Can not copy parameters from codec context.");
		throw ERROR_CREATE_PUSHER;
	}
	out_astream->codecpar->codec_tag = 0;


	//Open output URL
	if (!(format->flags & AVFMT_NOFILE))
	{
		ret = avio_open(&m_format_context->pb, url.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0)
		{
			free_all();
			Utils::write_log("Open IO fail.");
			throw ERROR_CREATE_PUSHER;
		}
	}

	//Write file header
	ret = avformat_write_header(m_format_context, nullptr);
	if (ret < 0)
	{
		free_all();
		Utils::write_log("Write header error.");
		throw ERROR_CREATE_PUSHER;
	}
	m_start_time = av_gettime();


}

void Pusher::stop_push()
{
	//Write file trailer
	if (av_write_trailer(m_format_context) != 0)
	{
		free_all();
		Utils::write_log("Write trailer fail.");
		throw ERROR_STOP_PUSHER;
	}
	//close output
	if (m_format_context != nullptr && !(m_format_context->oformat->flags & AVFMT_NOFILE))
	{
		if (avio_close(m_format_context->pb) < 0)
		{
			free_all();
			Utils::write_log("Close IO error.");
			throw ERROR_STOP_PUSHER;
		}
	}
	free_all();
	m_start_time = av_gettime();
	m_stopped = true;
}

void Pusher::push_video_packet(VideoPacket* video_packet)
{
	const auto av_packet = video_packet->get_packet();

	////Delay if too fast, too fast may cause the server crash.
	const auto time_base = m_vcodec_context->time_base;
	const AVRational time_base_q = { 1,AV_TIME_BASE };
	const auto pts_time = av_rescale_q(av_packet->dts, time_base, time_base_q);
	const auto now_time = av_gettime() - m_start_time;
	if (pts_time > now_time)
	{
		av_usleep(static_cast<unsigned int>(pts_time - now_time));
		Utils::write_log("Frame sleep.");
	}

	//Re calc the pts dts and duration.
	const auto out_stream = m_format_context->streams[0];
	av_packet->pts = av_rescale_q_rnd(av_packet->pts, m_vcodec_context->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	av_packet->dts = av_rescale_q_rnd(av_packet->dts, m_vcodec_context->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	av_packet->duration = av_rescale_q(av_packet->duration, m_vcodec_context->time_base, out_stream->time_base);
	
	total_bytes += av_packet->size;
	printf("Send videodata... ( %d ) ", total_bytes);

	auto cloned_av_packet = Utils::clone_av_packet(av_packet);
	//Write the frame, notice that here must use av_interleaved_write_frame, it will automatic control the interval.
	//Notice2, after call av_interleaved_write_frame, the packet's data will be released.
	const int tick_count = GetTickCount();
	const auto ret = av_interleaved_write_frame(m_format_context, cloned_av_packet);//av_interleaved_write_frame();
	if (ret < 0)
	{
		free_all();
		av_packet_free(&cloned_av_packet);
		cloned_av_packet = nullptr;
		throw ERROR_PUSH_PACKET;
	}
	const int time_used = GetTickCount() - tick_count;
	if (time_used > 300)
	{
		const auto str = new char[1024];
		sprintf_s(str, 1024, "Send frame too long. size:%d , %dms used.\n", cloned_av_packet->buf->size, time_used);
		Utils::write_log(str);
		delete[] str;
	}
	av_packet_free(&cloned_av_packet);

	printf("finished...\n");

	push_audio_packet(encode_audio_data());
}

void Pusher::push_audio_packet(AVPacket* packet)
{
	if (!packet || _aPts < 0) return;

	const auto av_packet = packet;

	////Delay if too fast, too fast may cause the server crash.
	const auto time_base = m_vcodec_context->time_base;
	const AVRational time_base_q = { 1,AV_TIME_BASE };
	const auto pts_time = av_rescale_q(av_packet->dts, time_base, time_base_q);
	const auto now_time = av_gettime() - m_start_time;
	if (pts_time > now_time)
	{
		av_usleep(static_cast<unsigned int>(pts_time - now_time));
		Utils::write_log("Frame sleep.");
	}
	printf("Send audiodata... %d ", av_packet->pts);
	//Re calc the pts dts and duration.
	const auto out_stream = m_format_context->streams[1];
	av_packet->pts = av_rescale_q_rnd(av_packet->pts, m_vcodec_context->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	av_packet->dts = av_rescale_q_rnd(av_packet->dts, m_vcodec_context->time_base, out_stream->time_base, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
	av_packet->duration = av_rescale_q(av_packet->duration, m_vcodec_context->time_base, out_stream->time_base);
	av_packet->stream_index = 1;

	auto cloned_av_packet = Utils::clone_av_packet(av_packet);
	//Write the frame, notice that here must use av_interleaved_write_frame, it will automatic control the interval.
	//Notice2, after call av_interleaved_write_frame, the packet's data will be released.
	const int tick_count = GetTickCount();
	const auto ret = av_interleaved_write_frame(m_format_context, av_packet);//av_interleaved_write_frame();
	if (ret < 0)
	{
		free_all();
		av_packet_free(&cloned_av_packet);
		cloned_av_packet = nullptr;
		throw ERROR_PUSH_PACKET;
	}
	const int time_used = GetTickCount() - tick_count;
	if (time_used > 300)
	{
		const auto str = new char[1024];
		sprintf_s(str, 1024, "Send frame too long. size:%d , %dms used.\n", cloned_av_packet->buf->size, time_used);
		Utils::write_log(str);
		delete[] str;
	}
	av_packet_free(&cloned_av_packet);
	Utils::write_log("finished \n");
}

AVPacket* Pusher::encode_audio_data()
{
	//To produce AudioData Packet to send. 
	//lazy fake audio data initialization here.
	const uint8_t *pcm[2 * 1024] = {0};
	AVPacket* packet = av_packet_alloc();
	av_init_packet(packet);

	if (!_swrCtx)
	{
		_swrCtx = swr_alloc_set_opts(
			_swrCtx,
			m_acodec_context->channel_layout,
			m_acodec_context->sample_fmt,
			m_acodec_context->sample_rate,
			av_get_default_channel_layout(m_acodec_context->channels),
			(AVSampleFormat)AV_SAMPLE_FMT_S16,
			m_acodec_context->sample_rate,
			0, NULL);

		int ret = swr_init(_swrCtx);
		if (ret != 0)
		{
			free_all();
			Utils::write_log("Can not init swr context.\n");
			return nullptr;
		}

		if (!_pcmFrame)
		{
			_pcmFrame = av_frame_alloc();
			_pcmFrame->format = m_acodec_context->sample_fmt;
			_pcmFrame->channels = m_acodec_context->channels;
			_pcmFrame->channel_layout = m_acodec_context->channel_layout;
			_pcmFrame->nb_samples = 1024; //1024
			ret = av_frame_get_buffer(_pcmFrame, 0);
			if (ret < 0)
			{
				Utils::write_log("av_frame_get_buffer() failed.\n");
				return nullptr;
			}
		}
	
	}

	const uint8_t *audio_data[AV_NUM_DATA_POINTERS] = { 0 };
	audio_data[0] = (uint8_t *)pcm;
	int len = swr_convert(
		_swrCtx,
		_pcmFrame->data, 
		_pcmFrame->nb_samples,
		audio_data,
		_pcmFrame->nb_samples);

	if (len < 0)
	{
		Utils::write_log("swr_convert() failed.\n");
		return nullptr;
	}

	_pcmFrame->pts = _aPts++;

	int ret = avcodec_send_frame(m_acodec_context, _pcmFrame);
	if (ret != 0)
	{
		return nullptr;
	}

	ret = avcodec_receive_packet(m_acodec_context, packet);
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
	{
		return nullptr;
	}
	else if (ret < 0)
	{
		Utils::write_log("avcodec_receive_packet() failed.");
		return nullptr;
	}

	if (ret != 0)
	{
		free_all();
		Utils::write_log("avcodec_fill_audio_frame error.\n");
		return nullptr;
	}

	return packet;
}
