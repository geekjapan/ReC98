// port/compat/mbstring_shim
// -------------------------
// REC98_PORT replacement for Turbo C++'s <mbctype.h>/<mbstring.h> Shift-JIS
// helpers (`_ismbblead`, `_ismbbkana`, `_mbcjmstojis`), used by
// th01/hardware/{graph,grppsafx,ztext}.cpp to walk shiftjis_t strings byte
// by byte. Neither header exists outside Turbo C++'s DOS runtime, so this
// reimplements the three call sites' worth of behavior directly against the
// standard Shift-JIS byte ranges/conversion formula (this is generic,
// well-documented text encoding, not ReC98/PC-98-specific).

#ifndef PORT_COMPAT_MBSTRING_SHIM_HPP
#define PORT_COMPAT_MBSTRING_SHIM_HPP

#include <cstdint>

// True if `c` is a Shift-JIS lead byte (first byte of a 2-byte fullwidth
// JIS X0208 codepoint).
inline int _ismbblead(unsigned char c) {
	return (c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC);
}

// True if `c` is a halfwidth katakana byte (JIS X0201 kana row).
inline int _ismbbkana(unsigned char c) {
	return (c >= 0xA1 && c <= 0xDF);
}

// Converts a packed Shift-JIS 2-byte code ((lead << 8) | trail) to the
// equivalent packed JIS X0208 code ((row << 8) | col, both 0x21-0x7E) --
// the standard SJIS-to-JIS formula.
inline uint16_t _mbcjmstojis(uint16_t sjis) {
	uint8_t lead = static_cast<uint8_t>(sjis >> 8);
	uint8_t trail = static_cast<uint8_t>(sjis & 0xFF);
	uint8_t row;
	uint8_t col;

	if(trail < 0x9F) {
		row = static_cast<uint8_t>(
			(lead < 0xE0) ? (((lead - 0x81) << 1) + 0x21)
			              : (((lead - 0xE0) << 1) + 0x5F)
		);
		col = static_cast<uint8_t>(trail - ((trail < 0x7F) ? 0x1F : 0x20));
	} else {
		row = static_cast<uint8_t>(
			(lead < 0xE0) ? (((lead - 0x81) << 1) + 0x22)
			              : (((lead - 0xE0) << 1) + 0x60)
		);
		col = static_cast<uint8_t>(trail - 0x7E);
	}

	return static_cast<uint16_t>((row << 8) | col);
}

#endif // PORT_COMPAT_MBSTRING_SHIM_HPP
