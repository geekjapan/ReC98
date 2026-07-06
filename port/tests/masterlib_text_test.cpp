// Unit test for port/shim/masterlib/text.cpp: text_clear()/text_fillca(),
// libs/master.lib/pc98_gfx.hpp's raw TRAM cell functions -- distinct from
// th01/hardware/ztext.cpp's escape-sequence-driven z_text_*() family.
// Needed by boss-defeat animations and stage timer resets (T11a); never
// reached by the OP.EXE title/menu closure (T9), so never previously
// shimmed.

#include <cstdio>

#include "port/compat/borland.hpp"

#include "libs/master.lib/pc98_gfx.hpp"
#include "pc98emu/text.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond) \
	do { \
		if(!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			g_failures++; \
		} \
	} while(0)

} // namespace

int main() {
	// text_fillca() must reach every one of the 80x25 cells with the given
	// halfwidth glyph + attribute.
	text_fillca('A', TX_WHITE);
	for(int row = 0; row < pc98emu::TEXT_ROWS; row++) {
		for(int col = 0; col < pc98emu::TEXT_COLS; col++) {
			const pc98emu::tram_cell_t &cell = pc98emu::g_tram[row][col];
			CHECK(cell.code == 'A');
			CHECK(cell.atrb == TX_WHITE);
			CHECK(cell.half == pc98emu::glyph_half_t::NONE);
		}
	}

	// text_clear() must reset every cell back to blank/atrb=0.
	text_clear();
	for(int row = 0; row < pc98emu::TEXT_ROWS; row++) {
		for(int col = 0; col < pc98emu::TEXT_COLS; col++) {
			const pc98emu::tram_cell_t &cell = pc98emu::g_tram[row][col];
			CHECK(cell.code == 0x20);
			CHECK(cell.atrb == 0);
		}
	}

	if(g_failures == 0) {
		std::printf("masterlib_text_test: all checks passed\n");
		return 0;
	}
	std::fprintf(stderr, "masterlib_text_test: %d check(s) FAILED\n", g_failures);
	return 1;
}
