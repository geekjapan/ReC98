// Unit test for the REC98_PORT branches added to
// th01/hardware/tram_x16.cpp's TRAMCursor::putkanji() and
// th01/hardware/ztext.cpp's z_text_putsa(): both used to write directly to
// SEG_TRAM_JIS/SEG_TRAM_ATRB via MK_FP/poke/pokeb, which
// port/compat/borland.hpp's resolve_far() always asserts on (those segments
// aren't byte-addressable in this port -- see its comment). They now call
// pc98emu/text.hpp's pc98emu_text_put_halfwidth()/put_fullwidth() cell API
// directly instead; this test drives the real game code (not the pc98emu
// API directly) and inspects the resulting pc98emu::g_tram cells.

#include <cstdio>

#include "port/compat/borland.hpp"

#include "libs/master.lib/pc98_gfx.hpp"
#include "pc98emu/text.hpp"
#include "th01/hardware/tram_x16.hpp"
#include "th01/hardware/ztext.hpp"

using namespace pc98emu;

// z_text_putsa() has external linkage but, unlike z_text_vputsa(), isn't
// declared in ztext.hpp (it's only meant to be reached through the vararg
// wrapper) -- declare it here to call it directly rather than going through
// vsprintf() for no reason.
void z_text_putsa(tram_x_t x, tram_y_t y, int z_atrb, const sshiftjis_t *str);

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

bool cell_is(int row, int col, uint16_t code, uint8_t atrb, glyph_half_t half) {
	const tram_cell_t &cell = g_tram[row][col];
	return cell.code == code && cell.atrb == atrb && cell.half == half;
}

} // namespace

int main() {
	// --- TRAMCursor::putkanji() ----------------------------------------
	pc98emu_text_reset();
	{
		TRAMCursor cursor;
		cursor.rewind_to_topleft();

		cursor.putkanji(0x3021, TX_BLACK | TX_REVERSE); // "亜", cell 0-1
		expect(
			cell_is(0, 0, 0x3021, TX_BLACK | TX_REVERSE, glyph_half_t::LEFT),
			"putkanji: first call writes column 0 as the LEFT half"
		);
		expect(
			cell_is(0, 1, 0x3021, TX_BLACK | TX_REVERSE, glyph_half_t::RIGHT),
			"putkanji: first call writes column 1 as the RIGHT half"
		);

		cursor.putkanji(' ', TX_BLACK); // cell 2-3 -- halfwidth codepoints go through the same path
		expect(
			cell_is(0, 2, ' ', TX_BLACK, glyph_half_t::LEFT),
			"putkanji: halfwidth codepoints are still written via put_fullwidth (both cells)"
		);
		expect(
			cell_is(0, 3, ' ', TX_BLACK, glyph_half_t::RIGHT),
			"putkanji: halfwidth codepoint's second cell"
		);

		// Advance to exactly the end of row 0 (80 cells = 40 more putkanji()
		// calls after the 2 above), then confirm the next call wraps to
		// row 1, column 0 -- matching `p`'s flat pointer walking off the end
		// of one TRAM row directly into the next.
		for(int i = 0; i < 38; i++) {
			cursor.putkanji(' ', TX_BLACK);
		}
		cursor.putkanji(0x2421, 0x55); // row 1, col 0-1
		expect(
			cell_is(1, 0, 0x2421, 0x55, glyph_half_t::LEFT),
			"putkanji: cursor wraps from column 78 into row 1, column 0"
		);
		expect(
			cell_is(1, 1, 0x2421, 0x55, glyph_half_t::RIGHT),
			"putkanji: wrapped row's second cell"
		);

		cursor.rewind_to_topleft();
		cursor.putkanji(0x4A4B, 0x11);
		expect(
			cell_is(0, 0, 0x4A4B, 0x11, glyph_half_t::LEFT),
			"rewind_to_topleft() resets the cursor back to row 0, column 0"
		);
	}

	// --- z_text_putsa() --------------------------------------------------
	pc98emu_text_reset();

	// Halfwidth ASCII.
	z_text_putsa(0, 2, Z_ATRB_RED, "AB");
	expect(
		cell_is(2, 0, 'A', TX_RED, glyph_half_t::NONE),
		"z_text_putsa: halfwidth 'A' with Z_ATRB_RED maps to TX_RED"
	);
	expect(
		cell_is(2, 1, 'B', TX_RED, glyph_half_t::NONE),
		"z_text_putsa: halfwidth 'B'"
	);

	// Fullwidth Shift-JIS lead byte: 0x88 0x9F is "亜" == JIS 0x3021 (same
	// codepoint port/tests/text_test.cpp uses), via _mbcjmstojis().
	z_text_putsa(5, 3, Z_ATRB_GREEN, "\x88\x9F");
	const uint8_t atrb_green = 1 | 0x80; // ATRB_DISP | ATRB_GREEN
	expect(
		cell_is(3, 5, 0x3021, atrb_green, glyph_half_t::LEFT),
		"z_text_putsa: fullwidth glyph's LEFT half at the requested column"
	);
	expect(
		cell_is(3, 6, 0x3021, atrb_green, glyph_half_t::RIGHT),
		"z_text_putsa: fullwidth glyph's RIGHT half at column+1"
	);

	// Last-column fallback: a fullwidth glyph can't fit at column 79, so a
	// halfwidth space is substituted instead (matching the original's
	// `x == (text_width() - 1)` branch).
	z_text_putsa(text_width() - 1, 4, Z_ATRB_BLUE, "\x88\x9F");
	const uint8_t atrb_blue = 1 | 0x20; // ATRB_DISP | ATRB_BLUE
	expect(
		cell_is(4, text_width() - 1, ' ', atrb_blue, glyph_half_t::NONE),
		"z_text_putsa: fullwidth glyph at the last column substitutes a halfwidth space"
	);

	if(g_failures == 0) {
		std::printf("tram_text_test: all checks passed\n");
		return 0;
	}
	std::fprintf(stderr, "tram_text_test: %d check(s) FAILED\n", g_failures);
	return 1;
}
