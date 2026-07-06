#ifdef REC98_PORT
#include <cassert>
#endif
#include "platform.h"
#include "x86real.h"
#include "planar.h"
#include "shiftjis.hpp"
#include "platform/x86real/pc98/font.hpp"

// The PC-98 character generator can be accessed in two ways:
//
// 1) I/O ports (0xA1, 0xA3, 0xA5, 0xA9)
// 2) BIOS function INT 18h AH=14h (reading) / AH=18h (writing gaiji)
//
// The BIOS variant is both simpler to use *and* faster, as the BIOS implements
// it in the optimal way for the specific hardware we run on:
//
// • At worst, it's an optimized ASM version of the I/O port code we would
//   write ourselves (as seen in the PC-9801VX / PC-9801RA BIOS).
// • At best, it directly reads from and writes to memory after mapping the JIS
//   code point to an address, which is always preferable to port I/O (as seen
//   in the PC-98HA BIOS).
// • Emulators directly implement it in native code.

#ifdef REC98_PORT
#include "pc98emu/text.hpp"

// REC98_PORT: the original body below hands a real seg:off far pointer to
// BIOS int 18h AH=14h and lets the BIOS fill in `glyph` directly -- there's
// no equivalent to reconstruct on a flat host (FP_SEG()/FP_OFF() would
// truncate the real 64-bit `&glyph` to 16 bits each, which cannot round-trip
// back through MK_FP()). Read the same vendored Shinonome glyph data
// pc98emu/text.cpp's TRAM compositor uses instead.
//
// `jis` covers three distinct BIOS glyph spaces (see the two ank_t overloads
// below): plain ASCII/ANK bytes (< 0x8000, 8x8), "0x8000 + ank" (8x16 ANK),
// and genuine JIS X0208 kanji rows/cols. th01/hardware/grppsafx.cpp's
// graph_putsa_fx() -- the only OP.EXE-closure caller -- only ever produces
// the 8x16 ANK form (ASCII and halfwidth katakana are both remapped to the
// PC-98-specific "JIS row 0x29" pseudo-kanji block, `0x2900`/`0x2980` +
// code, which the real CG-ROM happens to alias to the identical 8x16 ANK
// bitmap) or genuine kanji rows for the Music Test's track titles (not
// reachable from the title/menu screens M1 covers). The 8x8 ank_t overload
// isn't called from the OP.EXE closure at all.
void font_read(font_glyph_t& glyph, jis_t jis)
{
	if(jis >= 0x8000 ||
	   (jis & 0xFF00) == 0x2900 || (jis & 0xFF00) == 0x2980) {
		// jis >= 0x8000: "0x8000 + ank", the 8x16 ANK BIOS glyph space.
		// 0x2900/0x2980: PC-98 CG-ROM row 0x29, halfwidth ANK glyphs aliased
		// into the fullwidth code space (see graph_putsa_fx()'s own comment
		// on this).
		glyph.tag.w = GLYPH_HALF_W;
		glyph.tag.h = GLYPH_H;
		pc98emu_font_ank_rows(
			static_cast<uint8_t>(jis & 0xFF), glyph.ank_8x16.row
		);
		return;
	}
	glyph.tag.w = GLYPH_FULL_W;
	glyph.tag.h = GLYPH_H;
	pc98emu_font_kanji_rows(static_cast<uint16_t>(jis), glyph.kanji.row);
}
#else
void font_read(font_glyph_t& glyph, jis_t jis)
{
	_BX = FP_SEG(&glyph);
	_CX = FP_OFF(&glyph);
	_DX = jis;
	_AH = 0x14;
	geninterrupt(0x18);
	glyph.tag.w *= GLYPH_HALF_W;
	glyph.tag.h *= GLYPH_HALF_H;
}
#endif

void font_read(font_glyph_ank_8x8_t& glyph, ank_t ank)
{
	font_read(reinterpret_cast<font_glyph_t &>(glyph), static_cast<jis_t>(ank));
}

void font_read(font_glyph_ank_8x16_t& glyph, ank_t ank)
{
	font_read(reinterpret_cast<font_glyph_t &>(glyph), (0x8000 + ank));
}

#ifdef REC98_PORT
// REC98_PORT: not implemented -- gaiji (user-defined glyph) writes aren't
// reached by any port milestone yet (TH01 OP.EXE/REIIDEN.EXE/FUUIN.EXE don't
// call this; see OP_DEPS.md). Would need the same BIOS-call replacement as
// font_read() above (int 18h AH=1Ah instead of AH=14h) if a later task needs
// it.
void font_gaiji_write(
	const dot_rect_t(GLYPH_FULL_W, GLYPH_H)*, uint16_t, uint8_t
)
{
	assert(false && "font_gaiji_write(): not implemented under REC98_PORT");
}
#else
void font_gaiji_write(
	const dot_rect_t(GLYPH_FULL_W, GLYPH_H)* gaiji,
	uint16_t count,
	uint8_t id
)
{
	// Yeah, that copy is kind of silly, but we don't want to force that
	// awkward 16×17 format onto the caller.
	font_glyph_kanji_t tmp;

	jis_t jis = (0x7680 + id);
	for(uint16_t i = 0; i < count; i++) {
		tmp.dots = *gaiji;
		_BX = FP_SEG(&tmp);
		_CX = FP_OFF(&tmp);
		_DX = jis;
		_DX &= 0xFF7F;
		_AH = 0x1A;
		geninterrupt(0x18);

		gaiji++;
		jis++;
	}
}
#endif
