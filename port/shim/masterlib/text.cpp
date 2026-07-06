// port/shim/masterlib/text
// --------------------------
// libs/master.lib/pc98_gfx.hpp's raw TRAM cell functions (text_clear(),
// text_fillca()) -- distinct from th01/hardware/ztext.cpp's z_text_*()
// family, which drives an ANSI-escape-sequence legacy console text driver
// instead of touching TRAM directly (see x86real_shim.cpp's int86(0xDC)
// comment). Needed by boss-defeat animations and stage timer resets
// (th01/main/boss/b20j.cpp, b20m.cpp, th01/main/stage/timer.cpp) --
// unreached by the OP.EXE title/menu closure, so T3's masterlib shim never
// had to implement them. Backed by pc98emu's TRAM emulation (T6).
#include "port/compat/borland.hpp"

#include "libs/master.lib/pc98_gfx.hpp"
#include "pc98emu/text.hpp"

void text_clear(void) {
	pc98emu_text_reset();
}

void text_fillca(unsigned ch, unsigned atrb) {
	for(int row = 0; row < pc98emu::TEXT_ROWS; row++) {
		for(int col = 0; col < pc98emu::TEXT_COLS; col++) {
			pc98emu_text_put_halfwidth(
				col, row, static_cast<uint8_t>(ch), static_cast<uint8_t>(atrb)
			);
		}
	}
}
