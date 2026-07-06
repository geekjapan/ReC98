// port/tests
// ----------
// Acceptance test for shim/snd_stub.cpp: every th01/snd/mdrv2.h entry point
// must be callable against the real header (compiled with -DREC98_PORT via
// the `compat` target), must not crash, and must report success so callers
// proceed as if MDRV2 were resident and every playback request succeeded.
#include "th01/snd/mdrv2.h"

#include <cstdio>
#include <cstdlib>

namespace {

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		std::exit(1);
	}
}

} // namespace

int main() {
	expect(mdrv2_resident() != 0, "mdrv2_resident() reports resident");
	expect(mdrv2_enable_if_board_installed() != 0, "mdrv2_enable_if_board_installed() reports installed");

	expect(mdrv2_spawn(64) == 0, "mdrv2_spawn() reports success");

	mdrv2_bgm_load("BGM01.BGM");
	mdrv2_bgm_play();
	mdrv2_se_load("SE.DAT");
	mdrv2_se_play(3);
	mdrv2_bgm_fade_out_nonblock();
	mdrv2_bgm_stop();

	expect(mdrv2_remove() == 0, "mdrv2_remove() reports success");

	std::printf("snd_stub_test: OK\n");
	return 0;
}
