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
// starts a new game instead of bailing out with ERROR_RESIDENT_INVALID --
// but only when no resident structure already exists. T21 (see
// /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) made th01main
// reachable via a real execv() hand-off from th01op (OP.EXE's start_game())
// or from a fresh th01main process re-exec'd by its own previous instance
// (a boss-adjacent stage_id transition, see port/shim/main_globals.cpp) --
// port/shim/masterlib/chain_exec.cpp's state file already restores that
// resident before main() reaches this point in both cases. Unconditionally
// reseeding here would silently discard whatever OP.EXE (or the previous
// hop) put there -- rank/lives/route/stage_id/score, the exact state the
// switch is supposed to carry over.
//
// T21 also adds an opt-in (TH01_PORT_AUTOPILOT_MAIN=1) autopilot thread --
// see port/app/chain_autopilot.hpp's header comment for why a scripted
// driver has to live in the production binary itself rather than an
// external test harness once entrypoint_exec() does a real execv(). It
// reuses stage1_holdshot_test.cpp's proven "hold Shot/Up, force
// th01/main/debug.hpp's test_damage true" technique to blow through stage
// 1's card-wall segments in seconds; once resident_t::stage_id reaches a
// boss-adjacent transition, main_main() calls game_switch_binary(EP_MAIN)
// on its own (a real self-re-exec into a fresh th01main process, matching
// the original multi-binary DOS architecture) -- the *second* time this
// file's main() runs, it finds stage_id already there and, instead of
// driving further (fighting the boss is out of this acceptance test's
// scope), just confirms the hand-off and reports success.

#include "port/app/assets_dir.hpp"
#include "port/app/chain_autopilot.hpp"
#include "port/app/host.hpp"
#include "port/app/reiiden_bootstrap.hpp"
#include "port/shim/input.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <unistd.h>

#include <SDL3/SDL.h>

#include "th01/common.h" // BOSS_STAGE
#include "th01/main/debug.hpp" // test_damage
#include "th01/main/player/player.hpp" // player_left
#include "th01/pc98/entry.hpp" // main_main()

namespace {

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";

// Writes the fullchain_binary_switch_test.cpp result file (if
// TH01_PORT_CHAIN_MARKER is set) and exits. Called from the *second*
// th01main process the chain re-execs into once resident_t::stage_id
// already reflects the boss-adjacent transition -- there's nothing further
// for this test to drive.
[[noreturn]] void autopilot_main_report_and_exit(resident_t __seg *r) {
	std::fprintf(
		stderr,
		"th01main: autopilot -- stage transition verified (stage_id=%u "
		"rank=%u rem_lives=%d score=%lu sentinel=0x%x)\n",
		r->stage_id, r->rank, r->rem_lives,
		static_cast<unsigned long>(r->score),
		static_cast<unsigned>(r->unused_3)
	);
	if(const char *marker = std::getenv(port::CHAIN_TEST_MARKER_ENV)) {
		if(FILE *f = std::fopen(marker, "w")) {
			std::fprintf(
				f, "stage_id=%u rank=%u rem_lives=%d score=%lu sentinel=0x%x\n",
				r->stage_id, r->rank, r->rem_lives,
				static_cast<unsigned long>(r->score),
				static_cast<unsigned>(r->unused_3)
			);
			std::fclose(f);
		}
	}
	std::exit(0);
}

// File-scope (not thread-local-stack) storage for the synthetic key table --
// see port/app/main_op.cpp's g_autopilot_keys for why this can't be a local
// inside autopilot_main_run(): resident_t::stage_id reaching BOSS_STAGE
// doesn't mean main_main() is done reading input yet. th01/main/stage/
// bonus.cpp's stagebonus_animate()/totle_animate() (called synchronously,
// still on the main thread, right after the stage_id increment that makes
// this thread observe the transition) itself polls input_sense()/key_sense()
// in a "wait for Shot/OK release" gate *before* game_switch_binary() is ever
// reached -- so this thread returning (and a stack-local array going out of
// scope) exactly when it detects the transition raced a real
// EXC_BAD_ACCESS against the main thread's next key_sense() call during
// manual verification of this test. Keeping the array alive for the whole
// process lifetime (and continuing to drive it for a short grace period,
// see below) avoids the race entirely.
std::array<bool, SDL_SCANCODE_COUNT> g_autopilot_keys{};

void autopilot_main_set_key(SDL_Scancode sc, bool pressed) {
	g_autopilot_keys[static_cast<size_t>(sc)] = pressed;
	port::input_test_set_keys(g_autopilot_keys.data());
}

void autopilot_main_run() {
	resident_t __seg *r = resident_get_or_create();
	if(r->stage_id >= BOSS_STAGE) {
		autopilot_main_report_and_exit(r);
	}

	autopilot_main_set_key(SDL_SCANCODE_Z, true);
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
	while(player_left == 0 && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	if(player_left == 0) {
		std::fprintf(
			stderr,
			"th01main: autopilot -- timed out waiting for stage 1 init\n"
		);
		std::exit(1);
	}

	autopilot_main_set_key(SDL_SCANCODE_UP, true);
	bool reached_boss_stage = false;
	// Once the boss-adjacent stage_id transition is observed, keep cycling
	// the same duty pattern for a short grace period rather than returning
	// immediately: th01/main/stage/bonus.cpp's post-clear "release Shot/OK
	// to continue" wait (still running synchronously on the main thread, see
	// the comment above) needs to actually see a release edge before
	// main_main() reaches game_switch_binary()'s real execv() -- the same
	// duty cycle stage1_holdshot_test.cpp relies on for that gate, just not
	// stopped short here.
	constexpr int GRACE_ITERS = 60; // ~3s @ 50ms
	int grace_left = -1;
	for(int i = 0; i < 1200; i++) { // up to 60s @ 50ms
		autopilot_main_set_key(SDL_SCANCODE_LEFT, (i % 20) < 10);
		autopilot_main_set_key(SDL_SCANCODE_RIGHT, (i % 20) >= 10);
		autopilot_main_set_key(SDL_SCANCODE_Z, (i % 40) != 39);
		test_damage = true;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		if(!reached_boss_stage && r->stage_id >= BOSS_STAGE) {
			reached_boss_stage = true;
			grace_left = GRACE_ITERS;
		}
		if(grace_left == 0) {
			break;
		}
		if(grace_left > 0) {
			grace_left--;
		}
	}
	if(!reached_boss_stage) {
		std::fprintf(
			stderr,
			"th01main: autopilot -- timed out before a boss-adjacent stage_id "
			"transition (stage_id=%u)\n",
			r->stage_id
		);
		std::exit(1);
	}
	// Expected outcome from here: main_main() reaches game_switch_binary(
	// EP_MAIN) on its own (a real execv(), see port/shim/main_globals.cpp)
	// well within the grace period above, replacing this whole process --
	// nothing more for this thread to do either way.
}

void autopilot_main_start_if_enabled() {
	if(std::getenv("TH01_PORT_AUTOPILOT_MAIN") == nullptr) {
		return;
	}
	std::thread(autopilot_main_run).detach();
}

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

	if(!resident_get()) {
		port::reiiden_seed_fresh_resident();
	}

	autopilot_main_start_if_enabled();

	const char *argv0 = "reiiden";
	int ret = main_main(1, &argv0);

	port::host_shutdown();
	return ret;
}
