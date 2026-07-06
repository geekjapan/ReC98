/* ReC98
 * -----
 * Useless reimplementations of master.lib text functions, half of which are
 * unused anyway.
 */

#ifdef REC98_PORT
#include "port/compat/mbstring_shim.hpp"
#include "pc98emu/text.hpp"
#else
#include <mbctype.h>
#include <mbstring.h>
#endif
#include <stdarg.h>
#include "libs/master.lib/pc98_gfx.hpp"
#include "th01/hardware/ztext.hpp"

void z_text_init(void)
{
	union REGS regs;
	regs.h.ah = 0xE; // "Set single display area"
	regs.w.dx = 0;
	int86(0x18, &regs, &regs);
	z_text_25line();
	z_text_systemline_hide();
	z_text_setcursor(CURSOR_HIDE);
	z_text_clear();
	z_text_show();
}

void z_text_exit(void)
{
	z_text_25line();
	z_text_systemline_show();
	z_text_setcursor(CURSOR_BLOCK);
	z_text_clear();
	z_text_show();
}

void z_text_25line(void)
{
	z_text_print("\x1B[>3l");
}

void z_text_20line(void)
{
	z_text_print("\x1B[>3h");
}

void z_text_systemline_show(void)
{
	z_text_print("\x1B[>1l");
}

void z_text_systemline_hide(void)
{
	z_text_print("\x1B[>1h");
}

void z_text_clear(void)
{
	z_text_print("\x1B[2J");
}

void z_text_show(void)
{
	union REGS regs;
	regs.h.ah = 0xC;
	int86(0x18, &regs, &regs);
}

void z_text_hide(void)
{
	union REGS regs;
	regs.h.ah = 0xD;
	int86(0x18, &regs, &regs);
}

void z_text_setcursor(z_text_cursor_t type)
{
	_BX = _peekb_(0, 0x53B); // Text mode line height
	outportb(0x62, 0x4B); // CSRFORM
	outportb(0x60, (_BL | 0x80));
	switch(type) {
		case CURSOR_HIDE:
			outportb(0x60, 0x9F);
			break;
		case CURSOR_BLOCK:
			outportb(0x60, 0x80);
			break;
		case CURSOR_UNDERLINE:
			outportb(0x60, (_BL + 0x7D));
			break;
	}
	outportb(0x60, ((_BL << 3) + 2));
}

#ifndef REC98_PORT
inline uint8_t* tram_jis(uint16_t offset) {
	return reinterpret_cast<uint8_t *>(MK_FP(SEG_TRAM_JIS, offset));
}

inline tram_atrb2* tram_atrb(uint16_t offset) {
	return reinterpret_cast<tram_atrb2 *>(MK_FP(SEG_TRAM_ATRB, offset));
}
#endif

void z_text_putsa(tram_x_t x, tram_y_t y, int z_atrb, const sshiftjis_t *str)
{
	jis_t codepoint;
#ifndef REC98_PORT
	int p = (((y * text_width()) + x) * 2);
#endif
	tram_atrb2 hw_atrb = 1;

	if(z_atrb & Z_ATRB_BLUE) {
		hw_atrb += 0x20;
	}
	if(z_atrb & Z_ATRB_RED) {
		hw_atrb += 0x40;
	}
	if(z_atrb & Z_ATRB_GREEN) {
		hw_atrb += 0x80;
	}
	if(z_atrb & Z_ATRB_VLINE) {
		hw_atrb += 0x10;
	}
	if(z_atrb & Z_ATRB_UNDERLINE) {
		hw_atrb += TX_UNDERLINE;
	}
	if(z_atrb & Z_ATRB_REVERSE) {
		hw_atrb += TX_REVERSE;
	}
	if(z_atrb & Z_ATRB_BLINK) {
		hw_atrb += TX_BLINK;
	}
	hw_atrb += (hw_atrb << 8);
#ifdef REC98_PORT
	// REC98_PORT: pc98emu/text.hpp's text_atrb_t bits match Z_ATRB's hw_atrb
	// encoding above bit-for-bit (ATRB_DISP=1, ATRB_BLUE=0x20, ATRB_RED=0x40,
	// ATRB_GREEN=0x80, ATRB_VLINE=0x10, TX_UNDERLINE/TX_REVERSE/TX_BLINK ==
	// ATRB_UNDERLINE/ATRB_REVERSE/ATRB_BLINK -- see pc98emu/text.hpp's
	// text_atrb_t comment), so the low byte of hw_atrb can be passed straight
	// through to pc98emu_text_put_halfwidth()/put_fullwidth(). No raw
	// MK_FP(SEG_TRAM_JIS/ATRB, ...) byte pokes here -- see
	// port/compat/borland.hpp's resolve_far() comment on why that segment
	// intentionally isn't byte-addressable in this port.
	const uint8_t port_atrb = static_cast<uint8_t>(hw_atrb);
	while(str[0]) {
		if(_ismbblead(str[0])) {
			codepoint = _mbcjmstojis(
				(str[0] << 8) + static_cast<uint8_t>(str[1])
			);
			str += 2;
			if(x == (text_width() - 1)) {
				pc98emu_text_put_halfwidth(x, y, ' ', port_atrb);
			} else {
				// Simplification: the original's 0x2921-0x2B7E codepoint
				// range is a real-hardware quirk that renders a fullwidth
				// glyph compressed into a single TRAM column (writing only
				// one cell, not advancing the column cursor by 2) -- an
				// addressing detail the original decomp itself flags as
				// unverified ("more research and proof forthcoming", see
				// th01/hardware/tram_x16.cpp). z_text_putsa() has no
				// reachable caller in this port's closure, so rather than
				// guess at unverified hardware behavior, this always takes
				// the normal 2-column fullwidth path.
				pc98emu_text_put_fullwidth(x, y, codepoint, port_atrb);
				x++;
			}
		} else {
			pc98emu_text_put_halfwidth(x, y, *reinterpret_cast<const uint8_t *>(str++), port_atrb);
		}
		if(++x >= text_width()) {
			break;
		}
	}
#else
	while(str[0]) {
		if(_ismbblead(str[0])) {
			codepoint = _mbcjmstojis(
				(str[0] << 8) + static_cast<uint8_t>(str[1])
			);
			str += 2;
			if((codepoint >= 0x2921) && (codepoint <= 0x2B7E)) {
				tram_jis(p)[0] = (((codepoint >> 8) + 0xE0) | 0x80);
				tram_jis(p)[1] = codepoint;
				tram_atrb(p)[0] = hw_atrb;
			} else if(x == (text_width() - 1)) {
				reinterpret_cast<jis_t *>(tram_jis(p))[0] = ' ';
				tram_atrb(p)[0] = hw_atrb;
			} else {
				tram_jis(p)[0] = ((codepoint >> 8) + 0xE0);
				tram_jis(p)[1] = codepoint;
				tram_jis(p)[2] = (((codepoint >> 8) + 0xE0) | 0x80);
				tram_jis(p)[3] = codepoint;

				tram_atrb(p)[0] = hw_atrb;
				tram_atrb(p)[1] = hw_atrb;
				p += 2;
				x++;
			}
		} else {
			reinterpret_cast<jis_t *>(tram_jis(p))[0] = *reinterpret_cast<
				const uint8_t *
			>(str++);
			tram_atrb(p)[0] = hw_atrb;
		}
		p += 2;
		if(++x >= text_width()) {
			break;
		}
	}
#endif
}

void z_text_vputsa(
	tram_x_t x, tram_y_t y, int z_atrb, const sshiftjis_t *fmt, ...
)
{
	char str[256];
	va_list ap;

	va_start(ap, fmt);
	vsprintf(str, fmt, ap);
	z_text_putsa(x, y, z_atrb, str);
}

void z_text_print(const char *str)
{
	union REGS regs;
	regs.h.cl = 0x10;
	regs.h.ah = 0;
	while(*str) {
		regs.h.dl = *(str++);
		int86(0xDC, &regs, &regs);
	}
	outportb(0x64, 0); // VSync interrupt trigger
}
