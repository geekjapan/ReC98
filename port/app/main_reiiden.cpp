// port/app/main_reiiden
// ---------------------
// Port entry point for TH01's REIIDEN.EXE (the main gameplay binary:
// stages, player, bullets, bosses).
//
// Calls th01/main_01.cpp's main_main(argc, argv) -- the real REIIDEN.EXE
// entry function -- directly, after resolving an assets directory and
// starting the SDL host. Mirrors port/app/main_op.cpp's rationale
// (see that file's header comment) for bypassing the original DOS
// multi-entrypoint dispatcher entirely.
//
// M1-equivalent acceptance (no assets): main_main() itself would bail out
// almost immediately anyway (its very first real step after resident/rank
// setup is `resident = resident_get()`, which is only non-null if OP.EXE
// previously called resident_get_or_create() when starting a game -- out of
// scope here since th01op never runs that path either). But letting
// main_main() run this far and rely on that check would be an accident of
// control flow, not a deliberate "no assets" diagnostic, and every load
// past that point (arc_load(ARC_FN), scene_init_and_load()'s .grp/.bos/.ptn
// loads) assumes the data files exist -- unlike th01/formats/grp.cpp, none
// of those loaders have a REC98_PORT FileNotFound diagnostic (that was only
// added for the OP.EXE closure's title_init() call, see OP_DEPS.md's T9
// correction section). So, exactly like main_op.cpp, this checks for the
// assets directory itself before calling any original game code, and exits
// with a clear diagnostic if it's missing -- reaching the same conceptual
// "first asset load" point deterministically instead of letting original
// code run into a loader that would crash or hang without data.
//
// ARC_FN (th01/shiftjis/fns.hpp) is main_main()'s first real data load
// (arc_load(ARC_FN), right after game_init()/key_start()), so it's used
// here as the canary file, the same role REIIDEN2.GRP plays for th01op.
//
// M3 (SPEC.md): also seeds a fresh resident structure (see
// port/app/reiiden_bootstrap.hpp's header comment) so main_main() actually
// starts a new game instead of bailing out with ERROR_RESIDENT_INVALID.

#include "port/app/assets_dir.hpp"
#include "port/app/host.hpp"
#include "port/app/reiiden_bootstrap.hpp"

#include <cstdio>
#include <cstdlib>

#include <unistd.h>

#include "th01/pc98/entry.hpp" // main_main()

namespace {

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";

} // namespace

int main(int, char **) {
	std::string arc_fn_utf8 = port::reiiden_arc_fn_utf8();
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, arc_fn_utf8
	);
	std::string canary_path = assets_dir + "/" + arc_fn_utf8;

	if(!port::file_exists(canary_path)) {
		std::fprintf(
			stderr,
			"th01main: missing game data -- expected to find the REIIDEN "
			"arc file under '%s'.\n"
			"Place original TH01 assets there, or set TH01_ASSETS_DIR to "
			"point at a directory containing them.\n",
			assets_dir.c_str()
		);
		return 1;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(
			stderr, "th01main: could not chdir() into assets directory '%s'.\n",
			assets_dir.c_str()
		);
		return 1;
	}

	if(!port::host_init("TH01 -- REIIDEN.EXE (gameplay)")) {
		return 1;
	}

	port::reiiden_seed_fresh_resident();

	const char *argv0 = "reiiden";
	int ret = main_main(1, &argv0);

	port::host_shutdown();
	return ret;
}
