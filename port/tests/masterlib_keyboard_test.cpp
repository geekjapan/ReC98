// Unit test for port/shim/masterlib/keyboard.cpp: key_start/key_end (no-op,
// just must not crash) and key_sense_bios() (ORs shim/input's key_sense()
// across all BIOS keygroups), driven via port::input_test_set_keys() so it
// doesn't need a real SDL window/event loop.

#include <array>
#include <cstdio>

#include <SDL3/SDL.h>

#include "port/compat/borland.hpp"

#include "libs/master.lib/master.hpp"
#include "port/shim/input.hpp"

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
	// No-ops, must not crash.
	key_start();
	key_end();

	release_all();
	expect(key_sense_bios() == 0, "key_sense_bios is 0 with nothing pressed");

	// Esc lives in keygroup 0 -- key_sense_bios() must see it too.
	press_only(SDL_SCANCODE_ESCAPE);
	expect(key_sense_bios() != 0, "key_sense_bios sees a key pressed in group 0");

	// Return lives in keygroup 3.
	press_only(SDL_SCANCODE_RETURN);
	expect(key_sense_bios() != 0, "key_sense_bios sees a key pressed in group 3");

	release_all();
	expect(key_sense_bios() == 0, "key_sense_bios returns to 0 after release");

	port::input_test_set_keys(nullptr);

	if(g_failures == 0) {
		std::printf("masterlib_keyboard_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
