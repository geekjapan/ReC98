// port/app/main_op
// ----------------
// Port entry point for TH01's OP.EXE (title screen + options menu).
//
// Calls th01/op_01.cpp's main_op(argc, argv) -- the real OP.EXE entry
// function -- directly, after resolving an assets directory and starting
// the SDL host. This deliberately does NOT reproduce the original DOS
// multi-entrypoint dispatcher (th01/pc98/entry.cpp's main(), which picks
// between main_op()/main_main()/main_cutscene() by argv[1] and restarts the
// process via execl() to switch): th01op only ever wants the "op" entry
// point, SDL already owns this process's main(), and there is no DOS
// process to exec a different argv into. See port/shim/op_globals.cpp for
// the entrypoint_exec() stub that covers the one path (starting a game from
// the main menu) that would otherwise want that dispatcher.
//
// M1 acceptance (SPEC.md): without the original game's data files, the
// closure must still build+link cleanly, and the startup sequence must run
// up to the point where it needs real data, then fail with a clear message
// -- not hang or crash. th01/op_01.cpp's own first asset load
// (title_init()'s grp_put("REIIDEN2.grp", ...)) ignores a FileNotFound
// return and falls through into an interactive "press any key to continue"
// loop: correct behavior for the original binary, but not something a
// headless build can observe finishing, since nothing here can synthesize a
// keypress. So this checks for the assets directory itself, before running
// any original game code (game_init()/cfg_load()/etc.), and exits with a
// clear diagnostic if it's missing -- this is the "clear message" M1 asks
// for, produced deterministically rather than racing an interactive loop.
// (th01/formats/grp.cpp also got a small REC98_PORT-gated diagnostic at its
// own FileNotFound branch, for the case where a *partial* asset set is
// missing just one file after this check already passed.)

#include "port/app/assets_dir.hpp"
#include "port/app/host.hpp"

#include <cstdio>
#include <cstdlib>

#include <unistd.h>

#include "th01/pc98/entry.hpp" // main_op()

namespace {

// title_init() loads this first, before any other asset -- the canonical
// "do we have TH01's data files at all" check.
constexpr const char *ASSET_CANARY = "REIIDEN2.GRP";

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
			"th01op: missing game data -- expected to find '%s' under '%s'.\n"
			"Place original TH01 assets there, or set TH01_ASSETS_DIR to "
			"point at a directory containing them.\n",
			ASSET_CANARY, assets_dir.c_str()
		);
		return 1;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(
			stderr, "th01op: could not chdir() into assets directory '%s'.\n",
			assets_dir.c_str()
		);
		return 1;
	}

	if(!port::host_init("TH01 -- OP.EXE (title / menu)")) {
		return 1;
	}

	const char *argv0 = "op";
	int ret = main_op(1, &argv0);

	port::host_shutdown();
	return ret;
}
