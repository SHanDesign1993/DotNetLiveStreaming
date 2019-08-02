#ifndef _AUDIO_CAPTURE_H
#define _AUDIO_CAPTURE_H

#include <thread>
#include <cstdint>
#include <memory>
#include "net/RingBuffer.h"
#include "portaudio.h"  

#define AUDIO_LENGTH_PER_FRAME 1024

struct PCMFrame
{
	PCMFrame(uint32_t size = 100)
		: data(new uint8_t[size + 1024])
	{
		this->size = size;
	}
	uint32_t size = 0;
	uint32_t channels = 2;
	uint32_t samplerate = 44100;
	std::shared_ptr<uint8_t> data;
};

class AudioFrame
{
public:
	AudioFrame & operator=(const AudioFrame &) = delete;
	AudioFrame(const AudioFrame &) = delete;
	static AudioFrame& instance();
	~AudioFrame();

	bool init(uint32_t samplerate = 44100, uint32_t channels = 2);
	void exit();

	bool start();
	void stop();

	bool getFrame(PCMFrame& frame);

	bool isCapturing()
	{
		return (_isInitialized && Pa_IsStreamActive(_stream));
	}

private:
	AudioFrame();
	static int FrameCallback(const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags, void *userData);

	PaStreamParameters _inputParameters;
	PaStream* _stream = nullptr;
	bool _isInitialized = false;
	uint32_t _channels = 2;
	uint32_t _samplerate = 44100;
	std::shared_ptr<xop::RingBuffer<PCMFrame>> _frameBuffer;
};

#endif