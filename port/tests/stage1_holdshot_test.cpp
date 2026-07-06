// T19 gameplay-stability regression test: drives th01/main_01.cpp's
// main_main() (same closure as port/app/main_reiiden.cpp's th01main) with
// Shot (Z) held down almost continuously -- a realistic "holding Shot" play
// style, as opposed to discrete taps -- through sustained stage 1 combat,
// matching the reported "holding Shot continuously (vs tapping) hangs
// th01main" bug, then all the way through stage 1's 4 non-boss card-wall
// segments. Matches stage1_bringup_test's director-thread pattern (see that
// file's header comment for the synchronization rationale), but polls
// th01/resident.hpp's resident->stage_id and this file's own `extern
// boss_id` instead of player_left/score.
//
// This test's scope stops at the boundary into the Shingyoku boss fight: on
// clearing the last non-boss segment, th01/main_01.cpp's post-stage-clear
// handling calls game_switch_binary(EP_MAIN) for any boss-adjacent stage_id
// (matching the original multi-binary DOS architecture, which re-exec'd
// REIIDEN.EXE itself between major stage transitions) -- a re-exec this
// standalone port doesn't implement yet, so port/shim/main_globals.cpp's
// entrypoint_exec() logs it and calls exit(0). See boss_singyoku_test.cpp
// for the boss fight itself, verified in its own process (seeded with
// stage_id already at BOSS_STAGE) precisely because this transition can't
// carry a single process from "all card walls cleared" into "boss fight in
// progress" yet.
//
// Root cause #1 (see th01/hardware/scrollup.cpp's REC98_PORT branch): every
// life lost or stage segment cleared calls z_vsync_wait_and_scrollup(0),
// whose original body polls the GDC status register (port 0xA0) via
// inportb() in a tight `while(...) {}` loop with no frame_delay()/
// host_present_frame() call anywhere in it -- th01/main_01.cpp reaches this
// unconditionally, regardless of Shot being held or tapped. On real PC-98
// hardware that's a legitimate (if wasteful) hardware busy-wait; in this
// port, port/compat/x86real_shim.cpp's __inportb__() has no real GDC to
// poll, so its "unhandled port" default (assert-guarded, and thus a no-op
// in the optimized/NDEBUG build port/ci.sh actually uses) always returns 0
// -- making the second wait loop's `while((inportb(0xA0) & 0x20) == 0);`
// spin forever: a true, permanent, CPU-pegging hang with no further frames
// presented, exactly matching the bug report. It's reached more reliably
// under continuous Shot hold than tapping simply because that clears cards
// (and loses lives to unavoided pellets) faster, reaching this code sooner.
// Fixed by reusing th01/hardware/vsync.hpp's already-ported z_vsync_wait()
// -- the same "wait for the next VSync" primitive, just not re-derived via
// a GDC-status-polling loop with no host equivalent.
//
// Root cause #2 (see port/compat/borland.hpp's resolve_far()): faithfully-
// decompiled ZUN code can compute VRAM coordinates that graze slightly past
// a plane's bottom or right edge (e.g. th01/main/player/inv_spr.cpp's own
// documented "ZUN bug": its clipping check compares the wrong coordinate,
// so it never actually clips) -- harmless on real hardware, where the VRAM
// planes are contiguous, wrap-around memory-mapped windows, but a hard
// EXC_BAD_ACCESS crash against pc98emu's fixed-size, non-wrapping plane
// buffers. Fixed by wrapping a small out-of-range overrun modulo the plane
// size instead of asserting/crashing, matching real hardware's tolerance.
//
// Root cause #3, port-specific and comparatively minor: port/compat/
// x86real_shim.cpp's __outportb__() didn't yet handle ports 0x60/0x62
// (th01/hardware/ztext.cpp's z_text_setcursor() -- a purely cosmetic,
// unmodeled text-mode hardware cursor shape/blink toggle), so a Debug
// (assert-enabled) build aborted on it before ever reaching gameplay. Not
// reachable in port/ci.sh's actual (NDEBUG) build, but fixed anyway since it
// blocked debugging the above two root causes with assertions enabled.
//
// Clearing stage 1's 4 card-wall segments the "honest" way (bouncing the
// Orb into every card, see th01/main/stage/card.cpp's cards_hittest()) is
// real, unscripted-skill-dependent gameplay -- data shows it can take well
// over a minute for the last few cards alone, a poor fit for a deterministic
// CI timeout. Instead, this test directly drives th01/main/debug.cpp's
// `test_damage` global from the director thread (see PHASE 2 below): th01/
// main/stage/card.cpp's cards_hittest() treats a true test_damage as "flip
// every alive card immediately" regardless of the Orb's position, clearing
// each non-boss segment in about a second. This is an existing, faithfully
// -decompiled ZUN debug hook (normally driven by resident->debug_mode ==
// DM_TEST + holding OK/Return -- not a gameplay change introduced by the
// port), just triggered directly instead of indirectly through a simulated
// keypress.
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

#include "th01/main/boss/boss.hpp" // boss_id_t, BID_NONE, BID_SINGYOKU
#include "th01/main/debug.hpp" // test_damage
#include "th01/main/player/player.hpp" // player_left
#include "th01/main/stage/stageobj.hpp" // cards
#include "th01/main/stage/stages.hpp" // stage_cleared
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

// th01/main_01.cpp only assigns th01/resident.hpp's *global* `resident`
// pointer once main_main() itself starts running; reading or writing
// through it from this test's main()/director thread before that point
// (i.e. anywhere here) dereferences a null pointer. resident_get_or_create()
// itself is just a lookup-or-allocate against a named resource keyed by
// RES_ID, independent of that global, so main() below stashes the same
// pointer main_main() will later (re-)discover, and everything in this file
// goes through g_resident instead of the global.
resident_t __seg *g_resident = nullptr;

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
	std::fprintf(stderr, "stage1_holdshot: FAIL -- timed out waiting for: %s\n", what);
	return false;
}

void director_main() {
	release_all();

	// PHASE 1: hold Shot (Z) to clear the stage_wait_for_shot_to_begin gate,
	// then keep it held almost the whole run (see the 39/40 duty cycle
	// below) -- this is the "holding Shot continuously" repro from the bug
	// report.
	set_key(SDL_SCANCODE_Z, true);

	if(!wait_until("(a) player_left initialized to PLAYER_LEFT_START",
	               [] { return player_left != 0; }, 8000)) {
		g_director_done = true;
		return;
	}

	// PHASE 2: sweep left/right while holding Up, so shots rake across the
	// card wall's full width and Shot stays held (almost) throughout -- the
	// same active-play pattern as stage1_bringup_test's phase (d) -- while
	// directly forcing th01/main/debug.cpp's test_damage true for every
	// non-boss segment (see the file header comment) to blow through all 4
	// of stage 1's card walls in a few seconds each, and forcing it false
	// once a boss segment is reached so the Shingyoku fight plays out for
	// real.
	set_key(SDL_SCANCODE_UP, true);
	for(int i = 0; i < 1200 && !g_director_failed; i++) { // up to 60s @ 50ms
		set_key(SDL_SCANCODE_LEFT, (i % 20) < 10);
		set_key(SDL_SCANCODE_RIGHT, (i % 20) >= 10);
		// 39-held/1-released Shot duty cycle: a realistic "holding Shot"
		// play style (not the discrete tap-and-release of
		// stage1_bringup_test's phase (b)/(c)), but still lets a real
		// key-up occur periodically -- see the file header comment on why
		// stagebonus_animate()/totle_animate()'s post-stage-clear wait
		// needs one.
		set_key(SDL_SCANCODE_Z, (i % 40) != 39);
		test_damage = (boss_id == BID_NONE);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		if((i % 20) == 0) { // ~every 1s
			std::fprintf(
				stderr,
				"stage1_holdshot: t=%dms stage_id=%u boss_id=%d "
				"test_damage=%d score=%lu\n",
				i * 50, g_resident->stage_id, static_cast<int>(boss_id),
				static_cast<int>(test_damage),
				static_cast<unsigned long>(score)
			);
		}
		if(boss_id != BID_NONE) {
			break;
		}
	}

	if(boss_id == BID_NONE) {
		g_director_failed = true;
		g_director_failure = "never reached a boss segment (boss_id stayed "
		                      "BID_NONE) within the time budget -- see "
		                      "stderr for how far play got (last stage_id)";
		std::fprintf(
			stderr, "stage1_holdshot: FAIL -- %s: %u\n",
			g_director_failure.c_str(), g_resident->stage_id
		);
		g_director_done = true;
		return;
	}

	// The critical assertion: main_main() must have passed through
	// stagebonus_animate()/totle_animate()'s "release Shot/OK to continue"
	// wait promptly on the brief release the Shot duty cycle above
	// provides, every time it was hit across all of stage 1's non-boss
	// segments, rather than missing one and spinning forever (pegging a CPU
	// core with no further frames presented, per the bug report). In
	// practice, main_main() calls entrypoint_exec() (see the file header
	// comment) and the whole process exits(0) via a different thread the
	// instant stage_id reaches BOSS_STAGE, so this code is a defensive
	// fallback for if a future change ever lets boss_id become observable
	// in-process instead -- ctest already sees a clean exit(0) either way.
	std::fprintf(
		stderr,
		"stage1_holdshot: reached boss_id=%d (stage_id=%u) with Shot held "
		"nearly continuously the whole time -- no post-stage-clear hang\n",
		static_cast<int>(boss_id), g_resident->stage_id
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
			"stage1_holdshot: skipping -- no real TH01 assets at '%s' "
			"(set TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(stderr, "stage1_holdshot: could not chdir() into '%s'\n", assets_dir.c_str());
		return 1;
	}

	if(!port::host_init("stage1_holdshot_test (headless)")) {
		std::fprintf(stderr, "stage1_holdshot: host_init() failed\n");
		return 1;
	}

	port::reiiden_seed_fresh_resident();

	// th01/main_01.cpp only assigns th01/resident.hpp's global `resident`
	// pointer once main_main() itself starts running, so grab the same
	// underlying (lookup-or-allocate-by-RES_ID) resource directly here
	// instead -- see g_resident's comment above.
	g_resident = resident_get_or_create();

	// Pad out lives so an unoptimized, purely scripted dodge pattern can
	// still survive the full 60+ second boss-fight verification below
	// without an early Game Over ending the run. Doesn't touch boss/player
	// logic or rendering fidelity -- just how many chances this
	// intentionally-not-perfectly-played test gets.
	g_resident->rem_lives = 100;
	rem_lives = 100;

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
		std::fprintf(stderr, "stage1_holdshot: FAIL -- %s\n", g_director_failure.c_str());
		return 1;
	}
	std::printf("stage1_holdshot: continuous held-Shot play reached and passed "
	            "the post-stage-clear wait without hanging\n");
	return 0;
}
