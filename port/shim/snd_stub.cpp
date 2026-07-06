// port/shim/snd_stub.cpp
// -----------------------
// Silent no-op implementation of th01/snd/mdrv2.h for the REC98_PORT build.
// MDRV98 reimplementation is T13 (sound-m4); until then, every call here
// just logs (debug builds only) and reports success so callers proceed as
// if the TSR were resident and every playback request succeeded.

#include <cstdio>

#include "th01/snd/mdrv2.h"

namespace {

#ifndef NDEBUG
#define SND_STUB_LOG(...) std::fprintf(stderr, "[mdrv2_stub] " __VA_ARGS__)
#else
#define SND_STUB_LOG(...) ((void)0)
#endif

} // namespace

bool16 mdrv2_resident(void) {
	SND_STUB_LOG("mdrv2_resident()\n");
	return true;
}

bool16 mdrv2_enable_if_board_installed(void) {
	SND_STUB_LOG("mdrv2_enable_if_board_installed()\n");
	return true;
}

void mdrv2_bgm_load(const char *fn) {
	SND_STUB_LOG("mdrv2_bgm_load(\"%s\")\n", fn);
}

void mdrv2_se_load(const char *fn) {
	SND_STUB_LOG("mdrv2_se_load(\"%s\")\n", fn);
}

void mdrv2_bgm_play(void) {
	SND_STUB_LOG("mdrv2_bgm_play()\n");
}

void mdrv2_bgm_stop(void) {
	SND_STUB_LOG("mdrv2_bgm_stop()\n");
}

void mdrv2_bgm_fade_out_nonblock(void) {
	SND_STUB_LOG("mdrv2_bgm_fade_out_nonblock()\n");
}

void mdrv2_se_play(int se) {
	SND_STUB_LOG("mdrv2_se_play(%d)\n", se);
}

int mdrv2_spawn(uint8_t bgm_data_kib) {
	SND_STUB_LOG("mdrv2_spawn(%u)\n", bgm_data_kib);
	return 0;
}

int mdrv2_remove(void) {
	SND_STUB_LOG("mdrv2_remove()\n");
	return 0;
}

#undef SND_STUB_LOG
