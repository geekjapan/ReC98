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
// always populated by whichever caller execl()'d into FUUIN.EXE. We supply
// our own fixed "t1" (the hidden ending-preview flag: Makai route), rather
// than the normal resident-structure-driven path, since a fresh port
// process has no resident structure any OP.EXE/REIIDEN.EXE run in this
// build ever created (same reasoning as main_reiiden.cpp's header comment
// on resident_get()) -- "t1" is the only invocation that reaches real
// content deterministically.
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

#include <cstdio>
#include <cstdlib>

#include <unistd.h>

#include "th01/pc98/entry.hpp" // main_cutscene()

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

	const char *argv[] = { "fuuin", "t1" };
	int ret = main_cutscene(2, argv);

	port::host_shutdown();
	return ret;
}
