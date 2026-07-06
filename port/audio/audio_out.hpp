// port/audio/audio_out.hpp
// -----------------------------------------------------------------------
// SDL3 audio device output: a background thread pulls mono PCM from a
// Renderer callback and pushes it into an SDL_AudioStream bound to the
// default playback device, letting SDL handle resampling from the source
// (OPN chip) rate to whatever the device actually wants.
//
// Deliberately NOT exercised by ctest (opening a real playback device isn't
// guaranteed to succeed in a headless/CI environment) -- port/snd's engine
// and format tests cover the actual playback logic by rendering PCM
// in-process. See port/tests/snd_*_test.cpp.

#ifndef PORT_AUDIO_AUDIO_OUT_HPP
#define PORT_AUDIO_AUDIO_OUT_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

struct SDL_AudioStream;

namespace port::audio {

// Called from the feeder thread to produce `frames` mono samples.
using Renderer = std::function<void(int16_t *out, uint32_t frames)>;

class AudioOut {
public:
	AudioOut();
	~AudioOut();

	AudioOut(const AudioOut &) = delete;
	AudioOut &operator=(const AudioOut &) = delete;

	// Opens the default playback device at `source_rate` (mono S16) and
	// starts the feeder thread calling `renderer`. Returns false (logging to
	// stderr) if the device couldn't be opened -- callers should treat this
	// as "no audio available" rather than a fatal error.
	bool start(uint32_t source_rate, Renderer renderer);

	void stop();

	bool running() const { return running_; }

private:
	void feeder_thread_main();

	SDL_AudioStream *stream_ = nullptr;
	Renderer renderer_;
	std::thread thread_;
	std::atomic<bool> running_{false};
};

} // namespace port::audio

#endif // PORT_AUDIO_AUDIO_OUT_HPP
