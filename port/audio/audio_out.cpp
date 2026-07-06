// port/audio/audio_out.cpp
#include "port/audio/audio_out.hpp"

#include <cstdio>
#include <vector>

#include <SDL3/SDL.h>

namespace port::audio {

namespace {
// Feed the device in small chunks so playback latency stays low and we
// don't queue minutes of audio if the feeder thread gets descheduled.
constexpr uint32_t kChunkFrames = 512;
constexpr int kMaxQueuedBytes = kChunkFrames * sizeof(int16_t) * 8;
} // namespace

AudioOut::AudioOut() = default;

AudioOut::~AudioOut() {
	stop();
}

bool AudioOut::start(uint32_t source_rate, Renderer renderer) {
	stop();

	SDL_AudioSpec spec{};
	spec.format = SDL_AUDIO_S16;
	spec.channels = 1;
	spec.freq = static_cast<int>(source_rate);

	stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if(stream_ == nullptr) {
		std::fprintf(stderr, "[audio_out] SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
		return false;
	}
	if(!SDL_ResumeAudioStreamDevice(stream_)) {
		std::fprintf(stderr, "[audio_out] SDL_ResumeAudioStreamDevice failed: %s\n", SDL_GetError());
		SDL_DestroyAudioStream(stream_);
		stream_ = nullptr;
		return false;
	}

	renderer_ = std::move(renderer);
	running_ = true;
	thread_ = std::thread(&AudioOut::feeder_thread_main, this);
	return true;
}

void AudioOut::stop() {
	running_ = false;
	if(thread_.joinable()) {
		thread_.join();
	}
	if(stream_ != nullptr) {
		SDL_DestroyAudioStream(stream_);
		stream_ = nullptr;
	}
}

void AudioOut::feeder_thread_main() {
	std::vector<int16_t> chunk(kChunkFrames);
	while(running_) {
		if(SDL_GetAudioStreamQueued(stream_) > kMaxQueuedBytes) {
			SDL_Delay(1);
			continue;
		}
		renderer_(chunk.data(), kChunkFrames);
		SDL_PutAudioStreamData(stream_, chunk.data(),
		                        static_cast<int>(chunk.size() * sizeof(int16_t)));
	}
}

} // namespace port::audio
