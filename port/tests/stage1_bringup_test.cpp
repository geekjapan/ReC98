// Headless pixel-free verification for M3 (SPEC.md): drives th01/main_01.cpp's
// main_main() -- the same stage1/player/bullet closure port/app/main_reiiden.cpp's
// th01main executable runs -- from a fresh "new game" resident state (see
// port/app/reiiden_bootstrap.hpp) through the stage 1 intro (card-flip wall,
// HUD) and into live gameplay, injecting synthetic key states via
// port::input_test_set_keys(), without a real window/keyboard. Matches
// port/tests/menu_nav_test.cpp's director-thread pattern (see that file's
// header comment for the full synchronization rationale) but polls
// th01/main/player/player.hpp's player_left and th01/resident.hpp's score
// instead of th01/op_01.cpp's menu_sel/menu_id.
//
// Shortened for CI speed (a fixed number of director steps rather than a
// wall-clock soak) -- a manual 70+ second unscripted run of th01main itself
// (TH01_ASSETS_DIR=... TH01_DUMP_FRAME=... port/build/th01main) was used to
// confirm the M3 "60+ seconds without crash/assert" acceptance criterion;
// see WORKFLOW.md/T18 notes.
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

#include "th01/main/player/player.hpp" // player_left
#include "th01/pc98/entry.hpp" // main_main()
#include "th01/resident.hpp" // score

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
bool wait_until(const char *what, Pred pred, int timeout_ms = 8000) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while(std::chrono::steady_clock::now() < deadline) {
		if(pred()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	g_director_failed = true;
	g_director_failure = std::string("timed out waiting for: ") + what;
	std::fprintf(stderr, "stage1_bringup: FAIL -- timed out waiting for: %s\n", what);
	return false;
}

void director_main() {
	release_all();

	// (a) Hold Shot (Z) to clear main_01.cpp's stage_wait_for_shot_to_begin
	// gate and let the stage actually start progressing. Stage init (arc
	// load, .grp/.bos/.ptn loads, card-flip wall build-up) takes a real,
	// data-dependent amount of time, so this is a generous timeout, not a
	// fixed wall-clock guess -- same rationale as menu_nav_test's HIT-KEY
	// wait.
	set_key(SDL_SCANCODE_Z, true);

	// player_left is 0 until main_main() reaches its `player_left =
	// PLAYER_LEFT_START;` initialization (after arc/scene loading) -- wait
	// for that first, so the movement check below doesn't race and snapshot
	// the pre-init 0 value as its baseline (0 also happens to be
	// PLAYER_LEFT_MIN, which would make "moved left" impossible to observe).
	if(!wait_until("(a2) player_left initialized to PLAYER_LEFT_START",
	               [] { return player_left != 0; })) {
		g_director_done = true;
		return;
	}

	// (b) Drive left/right movement and confirm player_left actually moves
	// off its startup position (PLAYER_LEFT_START) -- proves input_sense(),
	// the stage's playfield-clamped movement code, and rendering are all
	// live, not just idling.
	screen_x_t start_left = player_left;
	set_key(SDL_SCANCODE_LEFT, true);
	bool moved_left = wait_until(
		"(b) player_left decreases when holding Left",
		[start_left] { return player_left < start_left; }, 15000
	);
	set_key(SDL_SCANCODE_LEFT, false);
	if(!moved_left) {
		g_director_done = true;
		return;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	screen_x_t mid_left = player_left;
	set_key(SDL_SCANCODE_RIGHT, true);
	bool moved_right = wait_until(
		"(c) player_left increases when holding Right",
		[mid_left] { return player_left > mid_left; }
	);
	set_key(SDL_SCANCODE_RIGHT, false);
	if(!moved_right) {
		g_director_done = true;
		return;
	}

	// (d) Keep playing (movement + shot held) for a few more seconds --
	// enough real time for card flips, pellet spawns/collisions, and orb
	// physics to all run for a while under active input without an assert
	// or crash surfacing.
	set_key(SDL_SCANCODE_UP, true);
	for(int i = 0; i < 60; i++) {
		set_key(SDL_SCANCODE_LEFT, (i % 20) < 10);
		set_key(SDL_SCANCODE_RIGHT, (i % 20) >= 10);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

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
			"stage1_bringup: skipping -- no real TH01 assets at '%s' "
			"(set TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(stderr, "stage1_bringup: could not chdir() into '%s'\n", assets_dir.c_str());
		return 1;
	}

	if(!port::host_init("stage1_bringup_test (headless)")) {
		std::fprintf(stderr, "stage1_bringup: host_init() failed\n");
		return 1;
	}

	port::reiiden_seed_fresh_resident();

	std::thread director(director_main);

	// Safety net: main_main()'s stage loop runs forever (`while(1)`), so the
	// director thread -- not main_main() -- decides when this process is
	// done. Once it's finished stepping through the script, forcibly quit
	// the process rather than waiting on main_main() to return on its own.
	std::thread quitter([] {
		while(!g_director_done.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
		std::exit(g_director_failed.load() ? 1 : 0);
	});

	const char *argv0 = "reiiden";
	main_main(1, &argv0);

	// Unreachable in practice (the quitter thread calls std::exit() first),
	// but keep the threads joined for a clean fallback if main_main() ever
	// does return on its own.
	director.join();
	quitter.join();
	port::host_shutdown();

	if(g_director_failed) {
		std::fprintf(stderr, "stage1_bringup: FAIL -- %s\n", g_director_failure.c_str());
		return 1;
	}
	std::printf("stage1_bringup: shot-gate release, left/right movement, "
	            "and sustained active play all verified\n");
	return 0;
}
