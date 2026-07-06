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
//
// T21 (see /Users/geekjapan/dev/projects/touhou/PLAN.md's M5): when
// TH01_PORT_AUTOPILOT_OP=1, spawns a background thread that drives the menu
// straight to START without a real keyboard -- see
// port/app/chain_autopilot.hpp's header comment for why this lives here
// instead of in an external test harness.

#include "port/app/assets_dir.hpp"
#include "port/app/chain_autopilot.hpp"
#include "port/app/host.hpp"
#include "port/shim/input.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <unistd.h>

#include <SDL3/SDL.h>

#include "port/compat/borland.hpp" // __seg (needed by th01/resident.hpp)

#include "th01/pc98/entry.hpp" // main_op()
#include "th01/resident.hpp" // resident_get_or_create()

// th01/op_01.cpp's own globals -- see menu_nav_test.cpp's identical extern
// declarations for why these aren't exposed through any header.
extern volatile int8_t menu_sel;
extern volatile bool quit;

namespace {

// title_init() loads this first, before any other asset -- the canonical
// "do we have TH01's data files at all" check.
constexpr const char *ASSET_CANARY = "REIIDEN2.GRP";

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";

// File-scope (not thread-local-stack) storage for the synthetic key table,
// matching menu_nav_test.cpp's g_keys convention: port::input_test_set_keys()
// only stores a raw pointer, so its target must outlive every future
// key_sense() call from the game's own thread, not just this detached
// autopilot thread's own lifetime.
std::array<bool, SDL_SCANCODE_COUNT> g_autopilot_keys{};

void autopilot_op_set_key(SDL_Scancode sc, bool pressed) {
	g_autopilot_keys[static_cast<size_t>(sc)] = pressed;
	port::input_test_set_keys(g_autopilot_keys.data());
}

// Matches menu_nav_test.cpp's tap()/wait_until() helpers, minus the
// gtest-style failure bookkeeping (a bare stderr message + exit(1) is
// enough for an opt-in production hook).
void autopilot_op_tap(SDL_Scancode sc) {
	autopilot_op_set_key(sc, true);
	std::this_thread::sleep_for(std::chrono::milliseconds(25));
	autopilot_op_set_key(sc, false);
	std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

template <class Pred>
bool autopilot_op_wait(Pred pred, int timeout_ms) {
	auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(timeout_ms);
	while(std::chrono::steady_clock::now() < deadline) {
		if(pred()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return false;
}

void autopilot_op_run() {
	// Hold Q+Down to clear the HIT-KEY loop (Q lives in BIOS keygroup 2,
	// ignored by the menu itself; Down moves the cursor once past it -- see
	// menu_nav_test.cpp's identical (a)+(b) step), then tap Up to land back
	// on choice 0 (START) -- proves we're actually past HIT KEY and the
	// cursor responds to input, while still ending on the default
	// selection.
	autopilot_op_set_key(SDL_SCANCODE_Q, true);
	autopilot_op_set_key(SDL_SCANCODE_DOWN, true);
	bool reached_menu = autopilot_op_wait(
		[] { return menu_sel == 1; }, 20000
	);
	autopilot_op_set_key(SDL_SCANCODE_Q, false);
	autopilot_op_set_key(SDL_SCANCODE_DOWN, false);
	if(!reached_menu) {
		std::fprintf(
			stderr,
			"th01op: autopilot -- timed out waiting for HIT KEY -> main "
			"menu\n"
		);
		std::exit(1);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	autopilot_op_tap(SDL_SCANCODE_UP);
	if(!autopilot_op_wait([] { return menu_sel == 0; }, 5000)) {
		std::fprintf(
			stderr, "th01op: autopilot -- timed out returning to START\n"
		);
		std::exit(1);
	}

	// Write the hand-off sentinel (see chain_autopilot.hpp) before
	// confirming START -- start_game() never touches resident_t::unused_3,
	// so it's still there for port/app/main_reiiden.cpp's autopilot to read
	// back after two real execv() hops.
	resident_t __seg *r = resident_get_or_create();
	r->unused_3 = static_cast<int32_t>(port::CHAIN_TEST_SENTINEL);

	autopilot_op_tap(SDL_SCANCODE_RETURN);
	// start_game(true) calls game_switch_binary(EP_MAIN), which
	// entrypoint_exec()s into th01main -- this process image (and this
	// thread) ends here on success; nothing more to drive.
	if(!autopilot_op_wait([] { return static_cast<bool>(quit); }, 5000)) {
		return; // expected: process got execv()'d away instead of quitting
	}
	std::fprintf(
		stderr,
		"th01op: autopilot -- main_op() quit instead of starting a game "
		"(START selection didn't take effect)\n"
	);
	std::exit(1);
}

void autopilot_op_start_if_enabled() {
	if(std::getenv("TH01_PORT_AUTOPILOT_OP") == nullptr) {
		return;
	}
	std::thread(autopilot_op_run).detach();
}

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

	autopilot_op_start_if_enabled();

	const char *argv0 = "op";
	int ret = main_op(1, &argv0);

	port::host_shutdown();
	return ret;
}
