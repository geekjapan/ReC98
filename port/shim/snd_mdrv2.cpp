// port/shim/snd_mdrv2.cpp
// -----------------------------------------------------------------------
// Real (non-stub) implementation of th01/snd/mdrv2.h for the REC98_PORT
// build: parses MDRV2's compiled .MDT BGM files (port/snd/mdt_format.hpp)
// and plays them back through a ymfm YM2203 emulation + SDL3 audio output
// (port/snd/mdrv2_engine.hpp, port/audio/audio_out.hpp), standing in for
// the real MDRV98.COM TSR + INT 0F2h calls th01/snd/mdrv2.cpp used to make.
//
// Effect sounds (.MDE, loaded via mdrv2_se_load()/played via
// mdrv2_se_play()) use a different compiled format that isn't
// reverse-engineered here (see port/snd/mdt_format.hpp's header comment for
// what MDT research this build *is* based on) -- se_load/se_play are still
// silent no-ops, same as the original port/shim/snd_stub.cpp. BGM is the
// M4 milestone's acceptance criterion (SPEC.md), so it gets the real
// implementation first.
//
// mdrv2_resident()/mdrv2_enable_if_board_installed()/mdrv2_spawn()/
// mdrv2_remove() model a real-mode DOS TSR's presence/absence, which has no
// equivalent here -- they keep snd_stub.cpp's "always succeeds" behavior so
// callers proceed as if MDRV98 were resident and an FM board were detected.

#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

#include "port/audio/audio_out.hpp"
#include "port/snd/mdrv2_engine.hpp"
#include "port/snd/mdt_format.hpp"
#include "th01/snd/mdrv2.h"

namespace {

#ifndef NDEBUG
#define SND_LOG(...) std::fprintf(stderr, "[mdrv2] " __VA_ARGS__)
#else
#define SND_LOG(...) ((void)0)
#endif

std::mutex g_engine_mutex;
port::snd::Mdrv2Engine g_engine;
port::audio::AudioOut g_audio_out;
bool g_audio_started = false;

bool read_file(const char *fn, std::vector<uint8_t> &out) {
	std::ifstream f(fn, std::ios::binary | std::ios::ate);
	if(!f) {
		return false;
	}
	std::streamsize size = f.tellg();
	if(size < 0) {
		return false;
	}
	f.seekg(0, std::ios::beg);
	out.resize(static_cast<size_t>(size));
	if(size > 0 && !f.read(reinterpret_cast<char *>(out.data()), size)) {
		return false;
	}
	return true;
}

void ensure_audio_started() {
	if(g_audio_started) {
		return;
	}
	uint32_t rate = g_engine.sample_rate();
	g_audio_started = g_audio_out.start(rate, [](int16_t *out, uint32_t frames) {
		std::lock_guard<std::mutex> lock(g_engine_mutex);
		g_engine.render(out, frames);
	});
	if(!g_audio_started) {
		SND_LOG("audio device unavailable; BGM will be silently sequenced but not audible\n");
	}
}

} // namespace

bool16 mdrv2_resident(void) {
	return true;
}

bool16 mdrv2_enable_if_board_installed(void) {
	return true;
}

void mdrv2_bgm_load(const char *fn) {
	SND_LOG("mdrv2_bgm_load(\"%s\")\n", fn);
	std::vector<uint8_t> bytes;
	if(!read_file(fn, bytes)) {
		SND_LOG("mdrv2_bgm_load: couldn't read \"%s\"\n", fn);
		return;
	}
	try {
		port::snd::MdtSong song = port::snd::mdt_parse(std::move(bytes));
		std::lock_guard<std::mutex> lock(g_engine_mutex);
		g_engine.load(std::move(song)); // mdrv2_bgm_play() is the actual "start" call
	} catch(const port::snd::MdtParseError &e) {
		SND_LOG("mdrv2_bgm_load: %s\n", e.what());
	}
}

void mdrv2_se_load(const char *fn) {
	SND_LOG("mdrv2_se_load(\"%s\") -- SE playback not yet implemented\n", fn);
}

void mdrv2_bgm_play(void) {
	SND_LOG("mdrv2_bgm_play()\n");
	{
		std::lock_guard<std::mutex> lock(g_engine_mutex);
		g_engine.play();
	}
	ensure_audio_started();
}

void mdrv2_bgm_stop(void) {
	SND_LOG("mdrv2_bgm_stop()\n");
	std::lock_guard<std::mutex> lock(g_engine_mutex);
	g_engine.stop();
}

void mdrv2_bgm_fade_out_nonblock(void) {
	SND_LOG("mdrv2_bgm_fade_out_nonblock() -- approximated as an immediate stop\n");
	std::lock_guard<std::mutex> lock(g_engine_mutex);
	g_engine.stop();
}

void mdrv2_se_play(int se) {
	SND_LOG("mdrv2_se_play(%d) -- SE playback not yet implemented\n", se);
}

int mdrv2_spawn(uint8_t bgm_data_kib) {
	SND_LOG("mdrv2_spawn(%u)\n", bgm_data_kib);
	return 0;
}

int mdrv2_remove(void) {
	SND_LOG("mdrv2_remove()\n");
	g_audio_out.stop();
	g_audio_started = false;
	return 0;
}

#undef SND_LOG
