// Unit test for port/shim/masterlib/gfx.cpp: egc_on/off/start, graph_start,
// palette_show, and grcg_boxfill against the real
// libs/master.lib/pc98_gfx.hpp declarations.

#include <cstdio>
#include <cstring>

#include "port/compat/borland.hpp"

#include "libs/master.lib/pc98_gfx.hpp"
#include "pc98emu/egc.hpp"
#include "pc98emu/grcg.hpp"
#include "pc98emu/vram.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

bool plane_all_zero(int page, int plane) {
	const uint8_t *p = pc98emu_planes[page][plane];
	for(int i = 0; i < PLANE_SIZE; i++) {
		if(p[i] != 0) {
			return false;
		}
	}
	return true;
}

} // namespace

int main() {
	// egc_on/off/start toggle pc98emu's EGC active flag without crashing.
	egc_off();
	expect(!pc98emu::g_egc_state.active, "egc_off clears active flag");
	egc_on();
	expect(pc98emu::g_egc_state.active, "egc_on sets active flag");
	egc_start();
	expect(!pc98emu::g_egc_state.active, "egc_start leaves EGC off (on then off, per asm)");

	// graph_start: darkens the palette, resets pages to 0, clears page 0.
	pc98emu_page_set_accessible(1);
	pc98emu_page_set_shown(1);
	// Dirty page 0 first so the clear is actually observed.
	pc98emu_page_set_accessible(0);
	std::memset(pc98emu_planes[0][pc98emu::PLANE_B], 0xFF, PLANE_SIZE);

	Palettes[0].set(255, 255, 255);
	PaletteTone = 100;

	graph_start();

	expect(pc98emu_page_get_accessible() == 0, "graph_start resets accessible page to 0");
	expect(pc98emu_page_get_shown() == 0, "graph_start resets shown page to 0");
	expect(plane_all_zero(0, pc98emu::PLANE_B), "graph_start clears page 0 plane B");
	expect(plane_all_zero(0, pc98emu::PLANE_E), "graph_start clears page 0 plane E");
	expect(
		pc98emu_pal[0][0] == 0 && pc98emu_pal[0][1] == 0 && pc98emu_pal[0][2] == 0,
		"graph_start darkens the palette (tone=0 -> black) regardless of Palettes[] content"
	);

	// palette_show applies PaletteTone to Palettes[] independent of
	// graph_start (tone=100 is a straight passthrough, scaled to 4-bit).
	Palettes[1].set(255, 0, 128);
	PaletteTone = 100;
	palette_show();
	expect(pc98emu_pal[1][0] == 15, "palette_show: full-intensity R at tone=100 -> 4-bit 15");
	expect(pc98emu_pal[1][1] == 0, "palette_show: zero G at tone=100 -> 4-bit 0");
	expect(pc98emu_pal[1][2] == (128 >> 4), "palette_show: mid-intensity B scaled to 4-bit");

	// grcg_boxfill: set GRCG to RMW mode with an all-1s tile (so every dot
	// in the box is set), fill a byte-unaligned rectangle, and verify the
	// edge byte masking as well as the untouched surrounding VRAM.
	pc98emu_page_set_accessible(0);
	std::memset(pc98emu_planes[0][pc98emu::PLANE_B], 0, PLANE_SIZE);
	pc98emu_outportb(0x7C, 0xC0); // GC_RMW, no plane excluded
	pc98emu_outportb(0x7E, 0xFF); // tile B
	pc98emu_outportb(0x7E, 0xFF); // tile R
	pc98emu_outportb(0x7E, 0xFF); // tile G
	pc98emu_outportb(0x7E, 0xFF); // tile E

	grcg_boxfill(4, 2, 19, 2); // one row, x=[4,19] -- spans byte 0 (partial) to byte 2 (partial)

	const uint8_t *row2 = pc98emu_planes[0][pc98emu::PLANE_B] + (2 * ROW_SIZE);
	expect(row2[0] == 0x0F, "grcg_boxfill left edge byte: bits 4-7 set (0x0F)");
	expect(row2[1] == 0xFF, "grcg_boxfill interior byte fully set");
	expect(row2[2] == 0xF0, "grcg_boxfill right edge byte: leftmost 4 dots set (0xF0), x=19 is the last one included");
	expect(row2[3] == 0x00, "grcg_boxfill does not touch past the right edge");

	const uint8_t *row1 = pc98emu_planes[0][pc98emu::PLANE_B] + (1 * ROW_SIZE);
	bool row1_untouched = true;
	for(int i = 0; i < ROW_SIZE; i++) {
		if(row1[i] != 0) {
			row1_untouched = false;
			break;
		}
	}
	expect(row1_untouched, "grcg_boxfill does not touch rows outside [y1, y2]");

	pc98emu_outportb(0x7C, 0x00); // GC_OFF, leave GRCG in a clean state

	if(g_failures == 0) {
		std::printf("masterlib_gfx_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
