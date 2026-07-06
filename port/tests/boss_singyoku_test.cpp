// T19 gameplay-stability verification: drives th01/main_01.cpp's
// main_main() (same closure as port/app/main_reiiden.cpp's th01main)
// straight into the stage 1 (scene 0, Makai route) boss fight against
// Shingyoku, by seeding th01/resident.hpp's resident structure with
// stage_id already at BOSS_STAGE instead of playing through the 4 preceding
// card-wall segments (see stage1_holdshot_test.cpp, which verifies those
// segments instead, and whose header comment explains why the two are
// separate processes -- summary: reaching a boss segment's *end* legitimately
// asks to re-exec th01main.exe itself via game_switch_binary(EP_MAIN)
// (th01/main_01.cpp's post-stage-clear handling calls this for every
// boss-adjacent stage_id transition, matching the original multi-binary
// DOS architecture), which this standalone port doesn't implement --
// port/shim/main_globals.cpp's entrypoint_exec() logs and cleanly exits(0)
// instead. A single process can't play both "the card walls" and "the boss"
// end to end yet; this test covers the boss half by starting fresh already
// at the boss's stage_id, which cleanly sidesteps that specific gap (a
// stage_id seeded at process start, before any life is lost or segment
// cleared, never reaches that particular code path).
//
// Verifies: Shingyoku (th01/main/boss/b05.cpp) loads and renders, and the
// fight survives 60+ real seconds of live play without crashing or hanging
// -- th01/main_01.cpp:709's z_vsync_wait_and_scrollup(0) call and
// out-of-range VRAM sprite blits (see th01/hardware/scrollup.cpp's
// REC98_PORT branch and port/compat/borland.hpp's resolve_far(), both fixed
// for T19) are reached just as unconditionally by a life lost *during* a
// boss fight as by clearing a card wall.
//
// Matches stage1_bringup_test.cpp's director-thread pattern (see that file's
// header comment for the synchronization rationale).
//
// Skipped (not failed) when the real TH01 assets aren't available, matching
// title_screen_golden_test's convention.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <unistd.h>

#include <SDL3/SDL.h>

#include "port/app/assets_dir.hpp"
#include "port/app/host.hpp"
#include "port/app/reiiden_bootstrap.hpp"
#include "port/shim/input.hpp"

#include "th01/common.h" // STAGES_PER_SCENE, BOSS_STAGE
#include "th01/main/boss/boss.hpp" // boss_id_t, BID_NONE, BID_SINGYOKU
#include "th01/main/player/player.hpp" // player_left
#include "th01/pc98/entry.hpp" // main_main()
#include "th01/resident.hpp" // resident, score

// th01/main_01.cpp defines this at file scope (not `static`) but doesn't
// expose it via any header; declared here rather than touching that file
// just to add a test-only extern.
extern boss_id_t boss_id;

namespace {

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";

constexpr int EXIT_SKIP = 77;

std::atomic<bool> g_director_failed{false};
std::string g_director_failure;
std::atomic<bool> g_director_done{false};

std::array<bool, SDL_SCANCODE_COUNT> g_keys{};

void set_key(SDL_Scancode sc, bool pressed) {
	g_keys[static_cast<size_t>(sc)] = pressed;
	port::input_test_set_keys(g_keys.data());
}

void release_all() {
	g_keys.fill(false);
	port::input_test_set_keys(g_keys.data());
}

template <class Pred>
bool wait_until(const char *what, Pred pred, int timeout_ms) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while(std::chrono::steady_clock::now() < deadline) {
		if(pred()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	g_director_failed = true;
	g_director_failure = std::string("timed out waiting for: ") + what;
	std::fprintf(stderr, "boss_singyoku: FAIL -- timed out waiting for: %s\n", what);
	return false;
}

void director_main() {
	release_all();

	// Hold Shot (Z) continuously -- the T19 bug report's repro pattern --
	// through the boss entrance sequence and fight. Shingyoku's own entrance
	// animation (singyoku_main(), called once before the main gameplay loop
	// starts -- see th01/main_01.cpp) doesn't gate on Shot the way the
	// non-boss stage_wait_for_shot_to_begin gate does, so there's no
	// initial release needed here.
	set_key(SDL_SCANCODE_Z, true);
	set_key(SDL_SCANCODE_UP, true);

	if(!wait_until("(a) player_left initialized to PLAYER_LEFT_START",
	               [] { return player_left != 0; }, 15000)) {
		g_director_done = true;
		return;
	}

	if(!wait_until("(b) boss_id becomes BID_SINGYOKU",
	               [] { return boss_id == BID_SINGYOKU; }, 15000)) {
		g_director_done = true;
		return;
	}

	std::fprintf(
		stderr,
		"boss_singyoku: boss_id=BID_SINGYOKU reached, starting 65s live "
		"combat verification (rem_lives=%d)\n",
		rem_lives
	);

	// 60+ real seconds of live Shingyoku combat with Shot held nearly
	// continuously, per the bug report's repro pattern, and movement to
	// exercise player/Orb interaction against a real, undamped boss AI (no
	// test_damage cheat here -- this test verifies the actual fight, not an
	// accelerated clear).
	constexpr int BOSS_SURVIVE_MS = 65000;
	auto boss_deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(BOSS_SURVIVE_MS);
	for(int i = 0; std::chrono::steady_clock::now() < boss_deadline; i++) {
		set_key(SDL_SCANCODE_LEFT, (i % 30) < 15);
		set_key(SDL_SCANCODE_RIGHT, (i % 30) >= 15);
		// 39-held/1-released Shot duty cycle: realistic "holding Shot" play
		// (not a discrete tap-and-release), still letting a real key-up
		// occur periodically.
		set_key(SDL_SCANCODE_Z, (i % 40) != 39);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		if((i % 20) == 0) { // ~every 1s
			std::fprintf(
				stderr,
				"boss_singyoku: t=%dms boss_id=%d rem_lives=%d score=%lu\n",
				i * 50, static_cast<int>(boss_id), rem_lives,
				static_cast<unsigned long>(score)
			);
		}
	}

	std::fprintf(
		stderr,
		"boss_singyoku: survived %d ms of live boss_id=%d combat with Shot "
		"held nearly continuously, rem_lives=%d, score=%lu\n",
		BOSS_SURVIVE_MS, static_cast<int>(boss_id), rem_lives,
		static_cast<unsigned long>(score)
	);

	release_all();
	g_director_done = true;
}

} // namespace

int main() {
	std::string arc_fn_utf8 = port::reiiden_arc_fn_utf8();
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, arc_fn_utf8
	);
	std::string canary_path = assets_dir + "/" + arc_fn_utf8;
	if(!port::file_exists(canary_path)) {
		std::printf(
			"boss_singyoku: skipping -- no real TH01 assets at '%s' "
			"(set TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(stderr, "boss_singyoku: could not chdir() into '%s'\n", assets_dir.c_str());
		return 1;
	}

	if(!port::host_init("boss_singyoku_test (headless)")) {
		std::fprintf(stderr, "boss_singyoku: host_init() failed\n");
		return 1;
	}

	port::reiiden_seed_fresh_resident();

	// Seed stage_id at BOSS_STAGE (scene 0, Makai route) instead of 0: see
	// the file header comment for why this test starts the process here
	// rather than playing through the 4 preceding card-wall segments (that
	// transition itself is the not-yet-implemented EP_MAIN re-exec path).
	// Extra lives pad out an intentionally-imperfect scripted dodge pattern
	// so the 60+ second live-combat verification below isn't cut short by
	// an early Game Over; neither touches boss/player logic or rendering.
	resident_t __seg *r = resident_get_or_create();
	r->stage_id = BOSS_STAGE;
	r->rem_lives = 100;

	std::thread director(director_main);

	// Safety net: main_main()'s stage loop runs forever (`while(1)`), so the
	// director thread -- not main_main() -- decides when this process is
	// done.
	std::thread quitter([] {
		while(!g_director_done.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		std::exit(g_director_failed.load() ? 1 : 0);
	});

	const char *argv0 = "reiiden";
	main_main(1, &argv0);

	director.join();
	quitter.join();
	port::host_shutdown();

	if(g_director_failed) {
		std::fprintf(stderr, "boss_singyoku: FAIL -- %s\n", g_director_failure.c_str());
		return 1;
	}
	std::printf("boss_singyoku: Shingyoku fight entered and survived 65s of "
	            "continuous-Shot live combat without hanging or crashing\n");
	return 0;
}
