#include "port/shim/input.hpp"

#include <array>

#include <SDL3/SDL.h>

namespace {

using GroupMap = std::array<SDL_Scancode, 8>;

// One entry per bit (LSB first) of a BIOS keygroup byte, matching the enum
// order in platform/x86real/pc98/keyboard.hpp. SDL_SCANCODE_UNKNOWN marks
// bits with no reasonable host-keyboard equivalent (NICOLA shift keys,
// VF-keys, the second HOME key, ...); those always read as unpressed.
// Groups 0/3/5/6/7/8/9 are the ones actually read by th01/hardware/
// input.cpp and are mapped with confidence; the rest are a best-effort
// QWERTY-layout mapping for future consumers (op-bringup menus etc.).
constexpr GroupMap group_maps[15] = {
	/* 0 */ {SDL_SCANCODE_ESCAPE, SDL_SCANCODE_1, SDL_SCANCODE_2,
	         SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
	         SDL_SCANCODE_7},
	/* 1 */ {SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_0,
	         SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS,
	         SDL_SCANCODE_INTERNATIONAL3, SDL_SCANCODE_BACKSPACE,
	         SDL_SCANCODE_TAB},
	/* 2 */ {SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R,
	         SDL_SCANCODE_T, SDL_SCANCODE_Y, SDL_SCANCODE_U, SDL_SCANCODE_I},
	/* 3 */ {SDL_SCANCODE_O, SDL_SCANCODE_P, SDL_SCANCODE_LEFTBRACKET,
	         SDL_SCANCODE_RIGHTBRACKET, SDL_SCANCODE_RETURN, SDL_SCANCODE_A,
	         SDL_SCANCODE_S, SDL_SCANCODE_D},
	/* 4 */ {SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_H, SDL_SCANCODE_J,
	         SDL_SCANCODE_K, SDL_SCANCODE_L, SDL_SCANCODE_SEMICOLON,
	         SDL_SCANCODE_APOSTROPHE},
	/* 5 */ {SDL_SCANCODE_BACKSLASH, SDL_SCANCODE_Z, SDL_SCANCODE_X,
	         SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N,
	         SDL_SCANCODE_M},
	/* 6 */ {SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
	         SDL_SCANCODE_INTERNATIONAL1, SDL_SCANCODE_SPACE,
	         SDL_SCANCODE_INTERNATIONAL4, SDL_SCANCODE_PAGEUP,
	         SDL_SCANCODE_PAGEDOWN},
	/* 7 */ {SDL_SCANCODE_INSERT, SDL_SCANCODE_DELETE, SDL_SCANCODE_UP,
	         SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN,
	         SDL_SCANCODE_HOME, SDL_SCANCODE_END},
	/* 8 */ {SDL_SCANCODE_KP_MINUS, SDL_SCANCODE_KP_DIVIDE, SDL_SCANCODE_KP_7,
	         SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_MULTIPLY,
	         SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5},
	/* 9 */ {SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_PLUS, SDL_SCANCODE_KP_1,
	         SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_EQUALS,
	         SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_COMMA},
	/* 10 */ {SDL_SCANCODE_KP_PERIOD, SDL_SCANCODE_INTERNATIONAL5,
	          SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
	          SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
	          SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN},
	/* 11 */ {SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_NUMLOCKCLEAR,
	          SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
	          SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
	          SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN},
	/* 12 */ {SDL_SCANCODE_PAUSE, SDL_SCANCODE_COPY, SDL_SCANCODE_F1,
	          SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
	          SDL_SCANCODE_F5, SDL_SCANCODE_F6},
	/* 13 */ {SDL_SCANCODE_F7, SDL_SCANCODE_F8, SDL_SCANCODE_F9,
	          SDL_SCANCODE_F10, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
	          SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN},
	/* 14 */ {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_CAPSLOCK,
	          SDL_SCANCODE_INTERNATIONAL2, SDL_SCANCODE_LALT,
	          SDL_SCANCODE_LCTRL, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
	          SDL_SCANCODE_UNKNOWN},
};

const bool *g_live_keys = nullptr;
int g_live_key_count = 0;
const bool *g_test_keys = nullptr;

bool scancode_pressed(SDL_Scancode sc) {
	if(sc == SDL_SCANCODE_UNKNOWN) {
		return false;
	}
	if(g_test_keys != nullptr) {
		return g_test_keys[sc];
	}
	if(g_live_keys == nullptr || sc >= g_live_key_count) {
		return false;
	}
	return g_live_keys[sc];
}

} // namespace

namespace port {

void input_poll() {
	SDL_PumpEvents();
	int count = 0;
	g_live_keys = SDL_GetKeyboardState(&count);
	g_live_key_count = count;
}

void input_test_set_keys(const bool *scancode_pressed_table) {
	g_test_keys = scancode_pressed_table;
}

} // namespace port

int key_sense(int keygroup) {
	constexpr int num_groups = sizeof(group_maps) / sizeof(group_maps[0]);
	if(keygroup < 0 || keygroup >= num_groups) {
		return 0;
	}

	// On real PC-98 hardware, key_sense() is a synchronous BIOS/keyboard-
	// controller register read: every single call reflects the current,
	// live key matrix, with no separate "poll" step. Most call sites only
	// ever go through input_sense(), invoked once per game frame right
	// after host_present_frame() has already refreshed g_live_keys -- but a
	// few busy-wait loops (e.g. th01/main/bonus.cpp's post-stage-clear
	// "release Shot/OK to continue" waits) call key_sense() in a tight loop
	// with no frame_delay()/host_present_frame() in between at all. Without
	// this refresh, those loops would keep re-reading the single stale
	// snapshot from before the wait started, so a real player's key-up
	// could never be observed -- turning an intentional "let go to
	// continue" gate into a permanent deadlock. Re-polling here keeps
	// key_sense() live for those loops too, matching hardware fidelity.
	if(g_test_keys == nullptr) {
		port::input_poll();
	}

	const GroupMap &map = group_maps[keygroup];
	int bits = 0;
	for(int bit = 0; bit < 8; bit++) {
		if(scancode_pressed(map[bit])) {
			bits |= (1 << bit);
		}
	}
	return bits;
}
