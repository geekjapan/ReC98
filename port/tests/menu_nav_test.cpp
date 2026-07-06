// Headless pixel-free verification for M2 (SPEC.md): drives th01/op_01.cpp's
// main_op() -- the same title/menu closure port/app/main_op.cpp's th01op
// executable runs -- through the full OP.EXE menu tree by injecting
// synthetic key states via port::input_test_set_keys(), without a real
// window/keyboard.
//
// main_op() is one long blocking call (its own internal loop keeps calling
// th01/hardware/vsync.hpp's z_vsync_Count1, which pumps one SDL frame via
// port::host_present_frame() every time it's read -- see vsync.cpp). SDL's
// window/event/render calls all have to stay on the thread that called
// port::host_init() (required by Cocoa on macOS), so main_op() itself runs
// on the main thread here; a second "director" thread sleeps in small steps
// and flips synthetic key state, polling th01/op_01.cpp's existing (already
// extern-linkage) menu_sel/menu_id/quit globals to know when each step has
// taken effect. Frame execution under SDL's dummy/headless presentation path
// is effectively unthrottled (thousands of frames/sec), so wall-clock
// polling with a generous timeout is a reliable, if not textbook-rigorous,
// way to synchronize with it -- see each step's comment below.
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

#include "port/compat/borland.hpp"

#include "port/app/assets_dir.hpp"
#include "port/app/host.hpp"
#include "port/shim/input.hpp"
#include "platform/x86real/pc98/keyboard.hpp"
#include "th01/pc98/entry.hpp"

// th01/op_01.cpp's own globals -- plain extern-linkage ints/bools, not
// exposed through any header (that file predates REC98_PORT and isn't meant
// to grow a test-support API), so this test just extern-declares them
// directly, matching their actual definitions' underlying types.
//
// `volatile` here (not present on the actual definitions) only affects how
// *this* TU loads them -- it stops the compiler from caching a stale value
// in a register across this file's sleep-based polling loops. The values
// themselves are plain int8_t/bool with no atomicity guarantee; this is a
// pragmatic test-only synchronization mechanism, not a formally race-free
// one (see the file header comment above).
extern volatile int8_t menu_sel;
extern volatile int8_t menu_id;
extern volatile bool quit;

namespace {

constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";
constexpr const char *ASSET_CANARY = "REIIDEN2.GRP";

constexpr int EXIT_SKIP = 77;

// Keep in sync with th01/op_01.cpp's local `menu_id_t` enum order.
enum {
	TEST_MID_MAIN = 0,
	TEST_MID_OPTION = 1,
	TEST_MID_MUSIC = 2,
	TEST_MID_UPDATE_BGM_MODE__DELAY__SWITCH_TO_MAIN = 3,
	TEST_MID_DELAY__SWITCH_TO_OPTION = 4,
};

std::atomic<bool> g_director_failed{false};
std::string g_director_failure;

std::array<bool, SDL_SCANCODE_COUNT> g_keys{};

void set_key(SDL_Scancode sc, bool pressed) {
	g_keys[static_cast<size_t>(sc)] = pressed;
	port::input_test_set_keys(g_keys.data());
}

void release_all() {
	g_keys.fill(false);
	port::input_test_set_keys(g_keys.data());
}

// Polls `pred` until it's true or `timeout_ms` elapses. Returns false (and
// records a failure message) on timeout.
template <class Pred>
bool wait_until(const char *what, Pred pred, int timeout_ms = 5000) {
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	while(std::chrono::steady_clock::now() < deadline) {
		if(pred()) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	g_director_failed = true;
	g_director_failure = std::string("timed out waiting for: ") + what;
	std::fprintf(stderr, "menu_nav: FAIL -- timed out waiting for: %s\n", what);
	return false;
}

// Best-effort escape hatch: whatever menu we're in, Esc/Cancel eventually
// walks back down to the main menu and then quits, since every menu_id
// branch in op_01.cpp's while(!quit) loop ORs in `input_cancel`.
void escape_to_quit() {
	for(int i = 0; i < 6 && !quit; i++) {
		set_key(SDL_SCANCODE_ESCAPE, true);
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		set_key(SDL_SCANCODE_ESCAPE, false);
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
	}
}

// Presses and releases `sc`, waiting `settle_ms` after each half so the
// (unthrottled) frame loop has plenty of real time to observe both edges --
// op_01.cpp's input_onchange_ring() requires an observed release before a
// held ring-key can advance the selection again.
void tap(SDL_Scancode sc, int settle_ms = 25) {
	set_key(sc, true);
	std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
	set_key(sc, false);
	std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));
}

void director_main() {
	release_all();

	// (a)+(b): Q lives in BIOS keygroup 2, which key_sense_bios() ORs into
	// its "any key" check (th01/op_01.cpp's `while(!key_sense_bios())`
	// HIT-KEY-to-continue loop) but which main_input_sense()/
	// option_input_sense() never read -- so it's a safe way to leave the
	// HIT KEY screen without also registering as OK/Shot/Cancel/an arrow in
	// the menu that follows (unlike Return, which doubles as OK and could
	// immediately "start game" if menu_sel was still 0).
	//
	// Both Q and Down are held down (not tapped) and kept held across the
	// whole title sequence (title_init()'s ~200 frames of its own
	// frame_delay()/whitelines_animate() calls, before the HIT-KEY loop even
	// starts polling) -- op_01.cpp's input_onchange_ring() only reacts to
	// the low-to-high edge, so a continuously-held Down still only moves the
	// cursor once, and there's no fixed wall-clock delay to get wrong here:
	// wait_until() just blocks however long title_init() actually takes on
	// this machine. Observing menu_sel actually reach 1 proves both that
	// HIT KEY advanced into the interactive main menu *and* that the cursor
	// responds to input.
	set_key(SDL_SCANCODE_Q, true);
	set_key(SDL_SCANCODE_DOWN, true);
	bool reached_menu = wait_until(
		"(a)+(b) HIT KEY -> main menu, cursor moves to choice 1",
		[] { return menu_sel == 1; }, 20000
	);
	set_key(SDL_SCANCODE_Q, false);
	set_key(SDL_SCANCODE_DOWN, false);
	if(!reached_menu) {
		escape_to_quit();
		return;
	}
	// Let the game observe the release above before the next tap() presses
	// Down again -- op_01.cpp's input_onchange_ring() needs to see the
	// low edge before it'll recognize a fresh press as a new one.
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// (b) continued: move from CONTINUE (1) to OPTION (2).
	tap(SDL_SCANCODE_DOWN);
	if(!wait_until("(b) cursor moves to choice 2 (OPTION)",
	               [] { return menu_sel == 2; })) {
		escape_to_quit();
		return;
	}

	// (c) OK on OPTION opens the Config screen.
	tap(SDL_SCANCODE_RETURN);
	if(!wait_until("(c) Config screen opens (menu_id == MID_OPTION)",
	               [] { return menu_id == TEST_MID_OPTION; })) {
		escape_to_quit();
		return;
	}

	// (c) continued: Cancel backs out of Config, all the way back to the
	// main menu (via MID_UPDATE_BGM_MODE__DELAY__SWITCH_TO_MAIN's frame_delay(15)).
	tap(SDL_SCANCODE_ESCAPE, 60);
	if(!wait_until("(c) Config screen returns to the main menu",
	               [] { return menu_id == TEST_MID_MAIN; }, 8000)) {
		escape_to_quit();
		return;
	}

	// (d) Re-enter Config (menu_sel is left at 2/OPTION by the return path
	// above), then move down 3 steps to M.TEST (index 3) and confirm.
	tap(SDL_SCANCODE_RETURN);
	if(!wait_until("(d) re-enter Config for Music Test",
	               [] { return menu_id == TEST_MID_OPTION; })) {
		escape_to_quit();
		return;
	}
	for(int step = 1; step <= 3; step++) {
		tap(SDL_SCANCODE_DOWN);
		if(!wait_until("(d) cursor reaches M.TEST",
		               [step] { return menu_sel == step; })) {
			escape_to_quit();
			return;
		}
	}
	tap(SDL_SCANCODE_RETURN);
	if(!wait_until("(d) Music Test opens (menu_id == MID_MUSIC)",
	               [] { return menu_id == TEST_MID_MUSIC; })) {
		escape_to_quit();
		return;
	}

	// (e) Quit path: back out of Music Test -> Config -> main menu -> quit.
	tap(SDL_SCANCODE_ESCAPE, 60);
	if(!wait_until("Music Test returns to Config",
	               [] { return menu_id == TEST_MID_OPTION; }, 8000)) {
		escape_to_quit();
		return;
	}
	tap(SDL_SCANCODE_ESCAPE, 60);
	if(!wait_until("Config returns to the main menu",
	               [] { return menu_id == TEST_MID_MAIN; }, 8000)) {
		escape_to_quit();
		return;
	}
	tap(SDL_SCANCODE_ESCAPE);
	wait_until("(e) quit path exits cleanly (quit == true)",
	           [] { return static_cast<bool>(quit); });
}

} // namespace

int main() {
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, ASSET_CANARY
	);
	std::string canary_path = assets_dir + "/" + ASSET_CANARY;
	if(!port::file_exists(canary_path)) {
		std::printf(
			"menu_nav: skipping -- no real TH01 assets at '%s' "
			"(set TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	if(::chdir(assets_dir.c_str()) != 0) {
		std::fprintf(stderr, "menu_nav: could not chdir() into '%s'\n", assets_dir.c_str());
		return 1;
	}

	if(!port::host_init("menu_nav_test (headless)")) {
		std::fprintf(stderr, "menu_nav: host_init() failed\n");
		return 1;
	}

	std::thread director(director_main);

	const char *argv0 = "op";
	int ret = main_op(1, &argv0);

	director.join();
	port::host_shutdown();

	if(g_director_failed) {
		std::fprintf(stderr, "menu_nav: FAIL -- %s\n", g_director_failure.c_str());
		return 1;
	}
	if(!quit) {
		std::fprintf(stderr, "menu_nav: FAIL -- main_op() returned without quit being set\n");
		return 1;
	}
	if(ret != 0) {
		std::fprintf(stderr, "menu_nav: FAIL -- main_op() returned %d, expected 0\n", ret);
		return 1;
	}

	std::printf("menu_nav: HIT KEY -> menu, cursor movement, Config open/return, "
	            "Music Test open, and clean quit all verified\n");
	return 0;
}
