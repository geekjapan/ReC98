// pc98emu
// -------
// Software emulation of PC-98 text RAM (TRAM): 80x25 cells, each holding a
// JIS codepoint plus an attribute byte (pc98.h:209-210's SEG_TRAM_JIS /
// SEG_TRAM_ATRB, reimplemented here without depending on that header). The
// glyphs themselves come from a vendored Shinonome 16-dot JIS bitmap font
// (see port/assets/font/README.md), loaded on first use.

#ifndef PC98EMU_TEXT_HPP
#define PC98EMU_TEXT_HPP

#include <cstdint>

namespace pc98emu {

constexpr int TEXT_COLS = 80;
constexpr int TEXT_ROWS = 25;
constexpr int CELL_W = 8;  // dots per TRAM column, halfwidth glyph width
constexpr int CELL_H = 16; // dots per TRAM row, both glyph heights

// PC-98 text attribute bits. Matches the real hardware layout bit-for-bit,
// which is also how libs/master.lib/pc98_gfx.hpp's TX_* constants are laid
// out (TX_BLACK=0x01, TX_BLUE=0x21, TX_BLINK=2, TX_REVERSE=4,
// TX_UNDERLINE=8): bit0 must be set for the cell to display at all (real
// hardware treats it as a "secret"/hide flag when clear), bits 5-7 select
// one of 8 fixed digital RGB colors independent of the palette (the PC-98
// text plane bypasses the analog palette entirely), and bits 1-4 are
// blink/reverse/underline/vline.
enum text_atrb_t {
	ATRB_DISP = 0x01,
	ATRB_BLINK = 0x02,
	ATRB_REVERSE = 0x04,
	ATRB_UNDERLINE = 0x08,
	ATRB_VLINE = 0x10,
	ATRB_BLUE = 0x20,
	ATRB_RED = 0x40,
	ATRB_GREEN = 0x80,
};

// Which half of a fullwidth glyph a cell renders. Fullwidth JIS X0208
// glyphs span two consecutive columns; real hardware writes the identical
// JIS code to both cells (see ReC98's TRAMCursor::putkanji(), which does
// exactly this), and it's this field -- not the code itself -- that tells
// the renderer which 8-dot-wide half of the 16-dot glyph belongs at this
// column.
enum class glyph_half_t : uint8_t { NONE, LEFT, RIGHT };

// One TRAM cell. `code` is an 8x16 JIS X0201 codepoint (ASCII or halfwidth
// katakana, ReC98's ank_t) when `half` is NONE, or a 16x16 JIS X0208
// codepoint (ReC98's jis_t: two 0x21-0x7E bytes packed as
// (row_byte << 8) | col_byte) when `half` is LEFT/RIGHT.
struct tram_cell_t {
	uint16_t code = 0x20; // halfwidth space
	uint8_t atrb = 0;     // 0 = ATRB_DISP clear, i.e. not displayed
	glyph_half_t half = glyph_half_t::NONE;
};

extern tram_cell_t g_tram[TEXT_ROWS][TEXT_COLS];

} // namespace pc98emu

// Clears TRAM to blank (halfwidth space, atrb=0) and resets the blink phase
// to visible. Called by pc98emu_init()/pc98emu_reset().
void pc98emu_text_reset();

// Writes a halfwidth (8x16 ANK) glyph at the given cell. Out-of-bounds
// (col, row) is a no-op.
void pc98emu_text_put_halfwidth(int col, int row, uint8_t ank_code, uint8_t atrb);

// Writes a fullwidth (16x16 JIS X0208) glyph spanning columns [col, col+1]
// at `row`. No-op if any of those cells is out of bounds -- matching
// ReC98's z_text_putsa(), which substitutes a halfwidth space rather than
// spanning a fullwidth glyph off the last column.
void pc98emu_text_put_fullwidth(int col, int row, uint16_t jis_code, uint8_t atrb);

// Sets the blink phase used when compositing ATRB_BLINK cells: while
// invisible, those cells are skipped entirely (background shows through),
// same as a non-displayed cell. Real hardware blinks on a fixed hardware
// timer; the port instead leaves phase control to the caller (e.g. the
// frame loop) so it stays deterministic for tests. Defaults to visible.
void pc98emu_text_set_blink_visible(bool visible);

// Composites the current TRAM contents on top of `out_rgba` (already filled
// with the rendered graphics planes -- see pc98emu_render() in vram.cpp,
// which calls this at the end of every frame). Foreground dots (font bit=1,
// or bit=0 under ATRB_REVERSE) are painted with the cell's digital 8-color;
// background dots are left untouched, so text overlays transparently over
// whatever graphics are underneath, matching real PC-98 hardware mixing the
// digital text plane over the analog graphics output. `out_rgba` must be
// pc98emu::RES_X * pc98emu::RES_Y elements, laid out the same way as
// pc98emu_render()'s output.
void pc98emu_text_composite(uint32_t *out_rgba);

#endif // PC98EMU_TEXT_HPP
