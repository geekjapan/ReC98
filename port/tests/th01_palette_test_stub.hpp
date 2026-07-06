// port/tests/th01_palette_test_stub.hpp
// -----------------------------------------------------------------------
// Test-only linkage for th01/hardware/palette.h's z_Palettes/
// z_palette_set_all_show() and th01/hardware/frmdelay.h's frame_delay().
// Their real implementations live in th01/hardware/graph.cpp and
// th01/hardware/frmdelay.cpp (out of scope for T8 -- see
// th01_grcg_test_stub.hpp for the same reasoning). th01/formats/grp.cpp's
// grp_palette_settone()/grp_palette_inout() reference them even though
// format tests never exercise those specific tone-fade code paths (they
// only call grp_put() with GPF_PALETTE_KEEP) -- but since grp.cpp is one
// translation unit, the linker still needs every symbol it references to
// resolve.
//
// z_palette_set_all_show() is a real (not fake) implementation: it applies
// [pal] to pc98emu's hardware palette exactly like the real function would
// (see port/shim/masterlib/gfx.cpp's palette_show() for the equivalent
// master.lib-side logic). frame_delay() is a no-op: it's a real-hardware
// vsync wait with no meaning in a test binary that never renders a frame.

#ifndef PORT_TESTS_TH01_PALETTE_TEST_STUB_HPP
#define PORT_TESTS_TH01_PALETTE_TEST_STUB_HPP

#include "port/compat/borland.hpp"

#include "th01/hardware/palette.h"
#include "th01/hardware/frmdelay.h"
#include "pc98emu/vram.hpp"

// Not `inline`: this header is only ever included by a single translation
// unit per test binary (no multi-TU ODR concern here), and plain
// external-linkage definitions are always emitted regardless of whether
// this TU's own code happens to call them -- unlike `inline`, which clang
// is free to (and does) drop entirely when unreferenced, leaving grp.cpp's
// references to these symbols unresolved at link time.
Palette4 z_Palettes;

void z_palette_set_all_show(const Palette4 &pal) {
	for(vc2 col = 0; col < COLOR_COUNT; col++) {
		pc98emu_palette_set(
			col,
			static_cast<uint8_t>(pal[col].c.r),
			static_cast<uint8_t>(pal[col].c.g),
			static_cast<uint8_t>(pal[col].c.b)
		);
	}
}

void frame_delay(unsigned int frames) {
	(void)frames;
}

#endif // PORT_TESTS_TH01_PALETTE_TEST_STUB_HPP
