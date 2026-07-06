// Unit test for port/shim/fuuin_globals.cpp: the link-time-only globals and
// stub functions th01fuuin_core needs beyond what th01/fuuin_01.cpp,
// th01/end/end.cpp, and th01/hiscore/regist.cpp define themselves (see that
// file's header comment for why each one exists).

#include <cstdio>

#include "port/compat/borland.hpp"

#include "th01/main/debug.hpp"
#include "th01/main/player/bomb.hpp"
#include "th01/rank.h"
#include "th01/resident.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

} // namespace

int main() {
	// `rank`/`mode_test`/`bomb_doubletap_frame` are plain writable globals
	// with the exact types th01/resident.hpp / th01/main/debug.hpp /
	// th01/main/player/bomb.hpp declare them as (matching those types is the
	// whole point of this shim -- an ODR mismatch would fail to link, not
	// misbehave at runtime, so this mostly just documents intent).
	expect(rank == RANK_NORMAL, "rank defaults to RANK_NORMAL");
	rank = RANK_LUNATIC;
	expect(rank == RANK_LUNATIC, "rank is a plain writable uint8_t");
	rank = RANK_NORMAL;

	expect(mode_test == false, "mode_test defaults to false (debug menu inert)");
	expect(bomb_doubletap_frame == 0, "bomb_doubletap_frame defaults to 0");

	// debug_mem()/debug_show_game() are unreachable at runtime (only called
	// from th01/hardware/input.cpp's `(end_flag == ES_NONE) && mode_test`
	// branch, and FUUIN.EXE always runs with end_flag != ES_NONE) -- this
	// just confirms the link-time stubs exist and don't crash if called.
	debug_mem();
	debug_show_game();

	if(g_failures == 0) {
		std::printf("fuuin_globals_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
