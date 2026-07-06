// port/app/main_fuuin
// --------------------
// Port entry point for TH01's FUUIN.EXE (ending slideshow, verdict screen,
// and high-score registration).
//
// Calls th01/fuuin_01.cpp's main_cutscene(argc, argv) -- the real FUUIN.EXE
// entry function -- directly, after resolving an assets directory and
// starting the SDL host. Mirrors port/app/main_op.cpp's and
// port/app/main_reiiden.cpp's rationale for bypassing the original DOS
// multi-entrypoint dispatcher entirely.
//
// main_cutscene() always dereferences argv[1][0] (and, in the preview
// branch, argv[1][1]) without an argc check -- on real hardware this is
// always populated by whichever caller execl()'d into FUUIN.EXE. T21 (see
// /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) made th01fuuin
// reachable via a real execv() hand-off from th01main's game_cleared
// handling (th01/main_01.cpp: `resident->end_flag = ...;
// game_switch_binary(EP_CUTSCENE);`) -- port/shim/masterlib/chain_exec.cpp's
// state file already restores that resident before main() reaches this
// point, exactly as end_init() (the `argv[1][0] != 't'` branch) expects. So
// this now passes real args whenever a resident structure exists,
// exercising the actual ending path (score/rank/continues pulled from
// resident_t) instead of always forcing the preview shortcut. Standalone
// launches with no resident (this file's own historical use case, and still
// the only option before a chain hand-off exists) keep the "t1" fallback --
// port/tests/fuuin_ending_test.cpp exercises the real path directly instead,
// seeding its own cleared-game resident (port/app/fuuin_bootstrap.hpp).
//
// No-assets acceptance (mirrors th01op/th01main): main_cutscene()'s normal
// path would actually already exit gracefully without any assets (missing
// resident struct -> end_init() prints an error and returns early), but the
// preview path we invoke runs straight into
// end_and_verdict_and_regist_animate(), which loads real .grp files and
// ends in the interactive high-score name-entry loop (regist_name_enter())
// that nothing here can synthesize input for. So, exactly like the other
// two entry points, this checks for the assets directory itself before
// calling any original game code, and exits with a clear diagnostic if it's
// missing -- reaching the same "first asset load" checkpoint
// deterministically instead of letting original code run into that
// unescapable interactive loop. ED1A.grp is end_and_verdict_and_regist_
// animate()'s first load, so it's used here as the canary file.

#include "port/app/assets_dir.hpp"
#include "port/app/host.hpp"
#include "port/compat/borland.hpp" // __seg (needed by th01/resident.hpp)

#include <cstdio>
#include <cstdlib>

#include <unistd.h>

#include "th01/pc98/entry.hpp" // main_cutscene()
#include "th01/resident.hpp" // resident_get()

namespace {

constexpr const char *ASSET_CANARY = "ED1A.grp";

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";

} // namespace

int main(int, char **) {
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, ASSET_CANARY
	);
	std::string canary_path = assets_dir + "/" + ASSET_CANARY;

	if(!port::file_exists(canary_path)) {
		std::fprintf(
			stderr,
			"th01fuuin: missing game data -- expected to find '%s' under "
			"'%s'.\n"
			"Place original TH01 assets there, or set TH01_ASSETS_DIR to "
			"point at a directory containing them.\n",
			ASSET_CANARY, assets_dir.c_str()
		);
		return 1;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(
			stderr, "th01fuuin: could not chdir() into assets directory "
			"'%s'.\n",
			assets_dir.c_str()
		);
		return 1;
	}

	if(!port::host_init("TH01 -- FUUIN.EXE (ending / registration)")) {
		return 1;
	}

	// Real ending path (resident-driven) when a chain hand-off seeded one;
	// otherwise the "t1" preview shortcut, the only option with no resident
	// -- see the header comment above.
	const char *argv[] = { "fuuin", resident_get() ? "x" : "t1" };
	int ret = main_cutscene(2, argv);

	port::host_shutdown();
	return ret;
}
