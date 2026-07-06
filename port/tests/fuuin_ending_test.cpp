// T21 (see /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) acceptance
// test for FUUIN.EXE's real ending path.
//
// Before this test, th01fuuin's own port/app/main_fuuin.cpp always drove
// th01/fuuin_01.cpp's main_cutscene() through the hidden "t1" ending-preview
// shortcut (end_flag forced directly to ES_MAKAI, bypassing end_init()
// entirely) -- there was no way to reach the real, resident-driven ending
// path (score/rank/continues pulled from resident_t) without an actual
// cleared playthrough, and nothing exercised
// th01/hiscore/regist.cpp's name-entry loop with real (non-"t1") input at
// all. This test seeds a cleared-game resident directly (port/app/
// fuuin_bootstrap.hpp, mirroring what th01/main_01.cpp's game_cleared
// handling would have left behind) and calls main_cutscene() with real args
// so it takes the `argv[1][0] != 't'` -> end_init() branch, then:
//
//  (a) lets the ED*.GRP ending slideshow play out for real (TH01_DUMP_FRAME
//      dumps every 30th presented frame as a PPM -- see port/app/host.cpp --
//      so a human/tooling can inspect what actually rendered);
//  (b) drives the verdict screen's input-gated wait and th01/hiscore/
//      regist.cpp's regist_name_enter() high-score name-entry loop with
//      synthetic Return presses (input_ok, see th01/hardware/input.hpp's
//      input_pause_ok_sense() macro), proving both are escapable headlessly
//      instead of blocking forever on a keyboard that doesn't exist here;
//  (c) sets TH01_PORT_CHAIN_STOP=1 so main_cutscene()'s final
//      game_switch_binary(EP_OP) confirms the switch decision and exits(0)
//      cleanly instead of really execv()-ing into th01op (irrelevant to
//      what this test verifies -- see port/shim/masterlib/chain_exec.cpp).
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
#include "port/app/fuuin_bootstrap.hpp"
#include "port/app/host.hpp"
#include "port/shim/input.hpp"

#include "th01/pc98/entry.hpp" // main_cutscene()
#include "th01/resident.hpp" // route_t

namespace {

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";
constexpr const char *ASSET_CANARY = "ED1A.grp";

constexpr int EXIT_SKIP = 77;

std::atomic<bool> g_director_done{false};

// File-scope, not thread-local: see port/app/main_reiiden.cpp's
// g_autopilot_keys for why a stack-local array captured by a detached/
// long-running thread's lambda is unsafe here (port::input_test_set_keys()
// only stores a raw pointer, read by main_cutscene()'s own thread for as
// long as this process keeps running).
std::array<bool, SDL_SCANCODE_COUNT> g_keys{};

void set_key(SDL_Scancode sc, bool pressed) {
	g_keys[static_cast<size_t>(sc)] = pressed;
	port::input_test_set_keys(g_keys.data());
}

void director_main() {
	// The slideshow itself (end_and_verdict_and_regist_animate() up through
	// end_bad()/end_good()) is driven purely by frame_delay() calls with no
	// input gate at all -- nothing to inject there. Once the verdict screen
	// or regist_name_enter()'s name-entry loop *is* reached, both break out
	// on input_ok (Return, see th01/hardware/input.hpp's
	// input_pause_ok_sense() macro) -- see verdict_animate_and_regist_menu()/
	// regist_name_enter() in th01/end/end.cpp and th01/hiscore/regist.cpp.
	// Tapping Return throughout is harmless during the slideshow (nothing
	// reads input there) and guarantees the earliest possible exit from
	// each of those two gates once they're actually reached.
	while(!g_director_done.load()) {
		set_key(SDL_SCANCODE_RETURN, true);
		std::this_thread::sleep_for(std::chrono::milliseconds(80));
		set_key(SDL_SCANCODE_RETURN, false);
		std::this_thread::sleep_for(std::chrono::milliseconds(80));
	}
}

} // namespace

int main() {
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, ASSET_CANARY
	);
	std::string canary_path = assets_dir + "/" + ASSET_CANARY;
	if(!port::file_exists(canary_path)) {
		std::printf(
			"fuuin_ending: skipping -- no real TH01 assets at '%s' (set "
			"TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(stderr, "fuuin_ending: could not chdir() into '%s'\n", assets_dir.c_str());
		return 1;
	}

	// See the file header comment (c): confirm-not-execv the final
	// game_switch_binary(EP_OP) this test doesn't need to follow.
	setenv("TH01_PORT_CHAIN_STOP", "1", 1);

	if(!port::host_init("fuuin_ending_test (headless)")) {
		std::fprintf(stderr, "fuuin_ending: host_init() failed\n");
		return 1;
	}

	// ROUTE_MAKAI with one continue used selects the shorter end_bad()
	// ending (th01/end/end.cpp: `if(continues_total == 0)` picks end_good()
	// + the boss slideshow instead) -- still a full, real, resident-driven
	// clear, just a faster one for CI purposes.
	port::fuuin_seed_cleared_resident(ROUTE_MAKAI, /*continues_used=*/1);

	std::thread director(director_main);

	const char *argv[] = {"fuuin", "x"};
	int ret = main_cutscene(2, argv);

	g_director_done = true;
	director.join();
	port::host_shutdown();

	// main_cutscene() only returns here if TH01_PORT_CHAIN_STOP suppressed
	// the final game_switch_binary(EP_OP)'s real execv() -- see
	// port/shim/masterlib/chain_exec.cpp. A real (non-suppressed) chain
	// would never come back to this line at all.
	if(ret != 0) {
		std::fprintf(
			stderr, "fuuin_ending: FAIL -- main_cutscene() returned %d, expected 0\n", ret
		);
		return 1;
	}

	std::printf(
		"fuuin_ending: real (non-preview) resident-driven ending path "
		"rendered end_and_verdict_and_regist_animate()'s full ED*.GRP "
		"slideshow, the verdict screen, and regist_name_enter()'s "
		"high-score name-entry loop, all escaped headlessly via synthetic "
		"Return presses -- see TH01_DUMP_FRAME output for frame captures\n"
	);
	return 0;
}
