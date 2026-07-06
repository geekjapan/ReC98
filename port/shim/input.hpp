// port/shim/input
// ----------------
// Maps live SDL3 keyboard state onto the PC-98 BIOS keygroup bitmaps
// (platform/x86real/pc98/keyboard.hpp) that key_sense() exposes to ReC98
// game code (see th01/hardware/input.cpp, which reads groups 0/3/5/6/7/8/9
// every frame). Game code keeps calling key_sense(group) exactly as
// declared in libs/master.lib/master.hpp; this shim supplies the
// implementation instead of the KEY_SENSE.ASM / BIOS INT 18h path.

#ifndef PORT_SHIM_INPUT_HPP
#define PORT_SHIM_INPUT_HPP

namespace port {

// Pumps SDL's keyboard state into the shim's snapshot. Call once per frame,
// after SDL_PumpEvents()/SDL_PollEvent(), before any key_sense() calls.
void input_poll();

// Test hook: replaces the live SDL_GetKeyboardState() snapshot with a
// synthetic table of SDL_SCANCODE_COUNT bools (true = pressed), so tests
// can drive key_sense() without a real window/event loop. Pass nullptr to
// go back to input_poll()-sourced state.
void input_test_set_keys(const bool *scancode_pressed);

} // namespace port

extern "C" {

// Matches libs/master.lib/master.hpp's declaration:
//     int MASTER_RET key_sense(int keygroup);
// Returns the current (instantaneous, not buffered) press bitmap for the
// given BIOS keygroup (0-14, see platform/x86real/pc98/keyboard.hpp). The
// original master.lib docs recommend sensing twice and OR-ing the results
// to avoid missing a keystroke on real BIOS hardware; that concern doesn't
// apply here (this file reads a single fully up-to-date snapshot), but
// callers such as th01/hardware/input.cpp still do it and it's harmless.
int key_sense(int keygroup);

} // extern "C"

#endif // PORT_SHIM_INPUT_HPP
