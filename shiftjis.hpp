#ifndef SHIFTJIS_HPP
#define SHIFTJIS_HPP

#include "decomp.hpp"

// 1-byte JIS X 0201 codepoint (ASCII, numeric, or halfwidth katakana).
typedef uint8_t ank_t;

// 2-byte JIS X 0208 codepoint.
typedef uint16_t jis_t;

// Multibyte Shift-JIS string. Use as replacement for `unsigned char` in
// situations where the contents are assumed to be Shift-JIS.
#ifdef REC98_PORT
// REC98_PORT: countless th01/shiftjis/*.hpp (and other games') string-table
// macros expand to plain `"..."` literals (type `const char*`) assigned
// straight into `const shiftjis_t*`/arrays thereof -- valid under Borland
// (where these files are compiled with unsigned-by-default `char`, making
// `shiftjis_t` and the literal's element type the same thing), but
// ill-formed C++ between the distinct `char`/`unsigned char` types on a
// standard host compiler. Making `shiftjis_t` plain `char` restores that
// same-type relationship; th01op_core is built with -funsigned-char (see
// port/CMakeLists.txt) so it still carries unsigned byte semantics for the
// arithmetic/comparison call sites (_ismbblead() et al.) that assume that.
typedef char shiftjis_t;
typedef char sshiftjis_t; // MODDERS: Delete -- see shiftjis_t's REC98_PORT note
#else
typedef uint8_t shiftjis_t;
typedef int8_t sshiftjis_t; // MODDERS: Delete
#endif

// 2-byte/fullwidth Shift-JIS codepoint.
typedef uint16_t shiftjis_kanji_t;
typedef int8_t sshiftjis_kanji_t; // MODDERS: Delete

// ZUN bloat: Byte-swapped 2-byte/fullwidth Shift-JIS codepoint for bytewise
// access.
typedef uint16_t shiftjis_kanji_swapped_t;
typedef int16_t sshiftjis_kanji_swapped_t; // MODDERS: Delete

// Widths
typedef int shiftjis_ank_amount_t;
typedef int shiftjis_kanji_amount_t;
typedef unsigned int ushiftjis_kanji_amount_t;

#define kanji_swap(kanji) static_cast<shiftjis_kanji_swapped_t>( \
	(static_cast<shiftjis_kanji_t>(kanji) << 8) | \
	(static_cast<shiftjis_kanji_t>(kanji) >> 8) \
)

// Way too often, the games store fullwidth Shift-JIS codepoints in two
// 8-bit variables rather than a single 16-bit one.
typedef StupidBytewiseWrapperAround<shiftjis_kanji_t> ShiftJISKanji;

// Good luck deriving this from the StupidBytewiseWrapperAround template.
template <unsigned int Length> union ShiftJISKanjiBuffer {
	shiftjis_kanji_t t[Length];
	int8_t byte[sizeof(uint16_t) * Length];
	uint8_t ubyte[sizeof(uint16_t) * Length];
};

inline jis_t shiftjis_to_jis(shiftjis_kanji_t shiftjis) {
	return (0x1F21 +
		(((shiftjis >> 8) - (shiftjis >> 15) < 0x9E)
			? ((((shiftjis >> 8) - (shiftjis >> 15) - 0x40) & 0x1FF))
			: ((shiftjis >> 8) - (shiftjis >> 15) + 0x62)
		) + ((shiftjis & 0x3F) << 9)
	);
}

#endif /* SHIFTJIS_HPP */
