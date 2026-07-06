// Unit test for port/shim/input: drives key_sense() via a synthetic
// scancode table (port::input_test_set_keys()), without a real SDL window
// or event loop.

#include <array>
#include <cstdio>

#include <SDL3/SDL.h>

#include "port/shim/input.hpp"
#include "platform/x86real/pc98/keyboard.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

std::array<bool, SDL_SCANCODE_COUNT> keys{};

void press_only(SDL_Scancode sc) {
	keys.fill(false);
	keys[sc] = true;
	port::input_test_set_keys(keys.data());
}

void release_all() {
	keys.fill(false);
	port::input_test_set_keys(keys.data());
}

} // namespace

int main() {
	// No keys down -> every group reads zero.
	release_all();
	expect(key_sense(0) == 0, "idle group 0 is zero");
	expect(key_sense(7) == 0, "idle group 7 is zero");

	// Esc -> keygroup 0, bit K0_ESC.
	press_only(SDL_SCANCODE_ESCAPE);
	expect(key_sense(0) == K0_ESC, "escape sets K0_ESC");

	// Return/Enter -> keygroup 3, bit K3_RETURN.
	press_only(SDL_SCANCODE_RETURN);
	expect(key_sense(3) == K3_RETURN, "return sets K3_RETURN");

	// Z -> keygroup 5, bit K5_Z (shot).
	press_only(SDL_SCANCODE_Z);
	expect(key_sense(5) == K5_Z, "Z sets K5_Z");

	// X -> keygroup 5, bit K5_X (bomb/strike).
	press_only(SDL_SCANCODE_X);
	expect(key_sense(5) == K5_X, "X sets K5_X");

	// Arrow keys -> keygroup 7.
	press_only(SDL_SCANCODE_UP);
	expect(key_sense(7) == K7_ARROW_UP, "up arrow sets K7_ARROW_UP");
	press_only(SDL_SCANCODE_DOWN);
	expect(key_sense(7) == K7_ARROW_DOWN, "down arrow sets K7_ARROW_DOWN");
	press_only(SDL_SCANCODE_LEFT);
	expect(key_sense(7) == K7_ARROW_LEFT, "left arrow sets K7_ARROW_LEFT");
	press_only(SDL_SCANCODE_RIGHT);
	expect(key_sense(7) == K7_ARROW_RIGHT, "right arrow sets K7_ARROW_RIGHT");

	// Numpad alt-arrows -> keygroups 8/9, as read by th01/hardware/input.cpp.
	press_only(SDL_SCANCODE_KP_8);
	expect(key_sense(8) == K8_NUM_8, "KP_8 sets K8_NUM_8");
	press_only(SDL_SCANCODE_KP_4);
	expect(key_sense(8) == K8_NUM_4, "KP_4 sets K8_NUM_4");
	press_only(SDL_SCANCODE_KP_2);
	expect(key_sense(9) == K9_NUM_2, "KP_2 sets K9_NUM_2");
	press_only(SDL_SCANCODE_KP_6);
	expect(key_sense(9) == K9_NUM_6, "KP_6 sets K9_NUM_6");

	// Multiple simultaneous keys within the same group OR together.
	keys.fill(false);
	keys[SDL_SCANCODE_UP] = true;
	keys[SDL_SCANCODE_LEFT] = true;
	port::input_test_set_keys(keys.data());
	expect(
		key_sense(7) == (K7_ARROW_UP | K7_ARROW_LEFT),
		"up+left OR together in group 7"
	);

	// Unmapped bits (e.g. keygroup 10's VF-keys) always read unpressed.
	release_all();
	expect(key_sense(10) == 0, "group 10 (mostly unmapped) is zero");

	// Out-of-range group is zero, not a crash.
	expect(key_sense(-1) == 0, "negative keygroup is zero");
	expect(key_sense(99) == 0, "out-of-range keygroup is zero");

	// Reverting to the live (nullptr) snapshot with no SDL window/events
	// pumped yet must not crash -- it should just read zero.
	port::input_test_set_keys(nullptr);
	expect(key_sense(0) == 0, "unset live snapshot reads zero, doesn't crash");

	if(g_failures == 0) {
		std::printf("shim_input_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
