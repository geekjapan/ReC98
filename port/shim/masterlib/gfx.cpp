// port/shim/masterlib/gfx.cpp
// -----------------------------------------------------------------------
// libs/master.lib/pc98_gfx.hpp entries actually called (unshadowed) from
// the OP.EXE closure: egc_on/egc_off/egc_start (th01/hardware/egcstart.cpp,
// th01/core/initexit.cpp), graph_start (th01/core/initexit.cpp), and
// grcg_boxfill (th01/hardware/grppsafx.cpp). Also defines the
// Palettes/PaletteTone globals and a real palette_show(), both needed
// internally by graph_start() (see graph_start.asm: it darkens the palette
// before turning the display on).
//
// th01/hardware/graph.cpp's z_graph_*/z_palette_*/grcg_setcolor_rmw and
// th01/hardware/grcg.hpp's grcg_off() macro are th01-local code that
// bypasses master.lib with direct BIOS/port-I/O reimplementations -- NOT
// part of this shim.

#include "port/compat/borland.hpp"

#include <cstring>

#include "libs/master.lib/pc98_gfx.hpp"
#include "pc98emu/egc.hpp"
#include "pc98emu/grcg.hpp"
#include "pc98emu/vram.hpp"

Palette8 Palettes;
unsigned int PaletteTone = 100;

void egc_on(void) {
	pc98emu_egc_activate();
}

void egc_off(void) {
	pc98emu_egc_deactivate();
}

void egc_start(void) {
	// Matches egc_start.asm: turn EGC on (programming its default
	// registers), then immediately back off, leaving GRCG-compatible mode
	// active until something (e.g. egc_start_copy()) turns EGC on again.
	pc98emu_egc_activate();
	pc98emu_egc_deactivate();
}

void palette_show(void) {
	for(vc2 col = 0; col < COLOR_COUNT; col++) {
		uint8_t out[3];
		for(int comp = 0; comp < 3; comp++) {
			int c = Palettes[col].v[comp];
			int toned;
			if(PaletteTone <= 100) {
				toned = (c * static_cast<int>(PaletteTone)) / 100;
			} else {
				toned = c + (((255 - c) * (static_cast<int>(PaletteTone) - 100)) / 100);
			}
			if(toned < 0) {
				toned = 0;
			} else if(toned > 255) {
				toned = 255;
			}
			out[comp] = static_cast<uint8_t>(toned);
		}
		pc98emu_palette_set(col, out[0] >> 4, out[1] >> 4, out[2] >> 4);
	}
}

void graph_start(void) {
	// graph_start.asm: darken the palette first, show/access page 0, then
	// clear it. GRAPH_SHOW/GRAPH_400LINE have no pc98emu-visible effect
	// (the emulated hardware has no separate "display enable"/line-count
	// state), so only the page reset and VRAM clear are reproduced here.
	PaletteTone = 0;
	palette_show();
	pc98emu_page_set_shown(0);
	pc98emu_page_set_accessible(0);
	// Unqualified, not pc98emu::PLANE_COUNT/PLANE_SIZE: pc98_gfx.hpp pulls in
	// pc98.h, which #defines identically-named, identically-valued macros at
	// global scope -- those would rewrite the qualified names into garbage
	// (e.g. "pc98emu::4") before the compiler ever sees them.
	for(int plane = 0; plane < PLANE_COUNT; plane++) {
		std::memset(pc98emu_planes[0][plane], 0, PLANE_SIZE);
	}
}

void grcg_boxfill(int x1, int y1, int x2, int y2) {
	if(x1 > x2) {
		int tmp = x1;
		x1 = x2;
		x2 = tmp;
	}
	if(y1 > y2) {
		int tmp = y1;
		y1 = y2;
		y2 = tmp;
	}
	if(x1 < 0) {
		x1 = 0;
	}
	if(y1 < 0) {
		y1 = 0;
	}
	if(x2 > RES_X - 1) {
		x2 = RES_X - 1;
	}
	if(y2 > RES_Y - 1) {
		y2 = RES_Y - 1;
	}
	if(x1 > x2 || y1 > y2) {
		return;
	}

	int left_byte = x1 / 8;
	int right_byte = x2 / 8;
	uint8_t left_mask = static_cast<uint8_t>(0xFF >> (x1 & 7));
	uint8_t right_mask = static_cast<uint8_t>(0xFF << (7 - (x2 & 7)));

	for(int y = y1; y <= y2; y++) {
		uint16_t row_offset = static_cast<uint16_t>(y * ROW_SIZE);
		if(left_byte == right_byte) {
			grcg_vram_write(
				0, row_offset + left_byte,
				static_cast<uint8_t>(left_mask & right_mask), 8
			);
			continue;
		}
		grcg_vram_write(0, row_offset + left_byte, left_mask, 8);
		for(int b = left_byte + 1; b < right_byte; b++) {
			grcg_vram_write(0, row_offset + b, 0xFF, 8);
		}
		grcg_vram_write(0, row_offset + right_byte, right_mask, 8);
	}
}
