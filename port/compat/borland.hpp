// port/compat
// -----------
// Borland/Turbo C++ language shim for the REC98_PORT build. Include this
// BEFORE any original ReC98 header (pc98.h, planar.h, platform.h, ...) --
// those headers assume `far`/`near`/`pascal`/MK_FP()/peekb()/the pseudo
// registers are already available, exactly like they would be under Turbo
// C++'s <dos.h>.
//
// `#pragma option` / `#pragma codeseg` (used in a few original headers) are
// simply unknown to clang and are silenced via -Wno-unknown-pragmas on the
// REC98_PORT compile targets (see port/CMakeLists.txt), not handled here.

#ifndef PORT_COMPAT_BORLAND_HPP
#define PORT_COMPAT_BORLAND_HPP

#if !defined(REC98_PORT)
#error compat/borland.hpp is only meant to be included in REC98_PORT builds
#endif

#include <cassert>
#include <cstdint>

#include "pc98emu/pc98emu.hpp"

// Borland distance/calling-convention keywords -- meaningless in a flat,
// single-calling-convention host build.
#define far
#define near
#define pascal
#define __fastcall

// x86real.h's non-Turbo-C fallback branch (taken under REC98_PORT, since
// none of __TURBOC__/__MSDOS__/__DOS_H are defined) spells the same
// distance/calling-convention keywords with a leading double underscore,
// plus Turbo C++'s `interrupt` ISR-linkage keyword for its getvect()/
// setvect() declarations. Same treatment: empty, meaningless in a flat
// address space with one calling convention.
#define __far
#define __near
#define __cdecl
#define interrupt

// Turbo C++'s segment-qualified pointer keyword (`void __seg *`, used by
// master.lib's hmem_/resdata_/dos_free() family for DOS segment handles).
// Not a real clang keyword; meaningless in a flat address space, same as
// far/near above.
#define __seg

// Pseudo registers
// ----------------
// Turbo C++ exposes the real CPU registers as global lvalues for inline-asm
// -adjacent code generation tricks. ReC98 code uses them purely for that
// purpose (see decomp.hpp), never relying on _AL/_AH actually aliasing into
// _AX, so plain independent thread_local storage preserves the semantics
// the original code depends on.
namespace port {

inline thread_local uint16_t _AX, _BX, _CX, _DX, _SI, _DI, _BP, _SP;
inline thread_local uint8_t _AL, _AH, _BL, _BH, _CL, _CH, _DL, _DH;

} // namespace port

using port::_AX;
using port::_BX;
using port::_CX;
using port::_DX;
using port::_SI;
using port::_DI;
using port::_BP;
using port::_SP;
using port::_AL;
using port::_AH;
using port::_BL;
using port::_BH;
using port::_CL;
using port::_CH;
using port::_DL;
using port::_DH;
// ----------------

namespace port {

// Resolves a Turbo C++-style far seg:off address to a host pointer into the
// pc98emu VRAM buffers. Only SEG_PLANE_B/R/G/E (from pc98.h) are currently
// backed by pc98emu state; SEG_TRAM_JIS/ATRB still assert.
//
// pc98emu/text.hpp (T6) now exists, but deliberately does NOT expose a raw
// byte-addressable TRAM buffer matching real hardware's SEG_TRAM_JIS/ATRB
// layout: the exact encoding ReC98 pokes into that region for fullwidth
// glyphs (th01/hardware/tram_x16.cpp's TRAMCursor::putkanji(), via
// ztext.cpp's inline lead-byte trick) is still flagged upstream as
// unverified ("more research and proof forthcoming"), and that byte pattern
// happens to be ambiguous with halfwidth katakana codepoints too. Whoever
// wires up peekb/pokeb-based TRAM access (T3/T9) should instead translate
// at this layer into pc98emu_text_put_halfwidth()/put_fullwidth()'s clean,
// unambiguous (code, atrb, half) cell API.
//
// The segment constants are hardware constants (real PC-98 memory map) and
// are duplicated here as literals -- rather than referencing pc98.h's
// SEG_PLANE_* macros -- because this header must be includable *before*
// pc98.h. Keep in sync with pc98.h's "Memory segments" section if it ever
// changes.
inline void *resolve_far(uint16_t seg, uint16_t off) {
	constexpr uint16_t SEG_PLANE_B_ = 0xA800;
	constexpr uint16_t SEG_PLANE_R_ = 0xB000;
	constexpr uint16_t SEG_PLANE_G_ = 0xB800;
	constexpr uint16_t SEG_PLANE_E_ = 0xE000;
	constexpr uint32_t PLANE_BYTES_ = 32000; // pc98emu::PLANE_SIZE

	const uint32_t linear = (static_cast<uint32_t>(seg) << 4) + off;

	const struct { uint16_t seg; uint8_t *base; } planes[4] = {
		{SEG_PLANE_B_, VRAM_PLANE_B},
		{SEG_PLANE_R_, VRAM_PLANE_R},
		{SEG_PLANE_G_, VRAM_PLANE_G},
		{SEG_PLANE_E_, VRAM_PLANE_E},
	};

	for(const auto &plane : planes) {
		const uint32_t base_linear = static_cast<uint32_t>(plane.seg) << 4;
		if(linear >= base_linear && (linear - base_linear) < PLANE_BYTES_) {
			return plane.base + (linear - base_linear);
		}
	}

	assert(false && "resolve_far(): segment not backed by pc98emu "
	                "(SEG_TRAM_JIS/ATRB pending the T6 text-font component, "
	                "or out-of-range plane offset)");
	return nullptr;
}

} // namespace port

inline void *MK_FP(uint16_t seg, uint16_t off) {
	return port::resolve_far(seg, off);
}

inline uint8_t peekb(uint16_t seg, uint16_t off) {
	return *static_cast<uint8_t *>(MK_FP(seg, off));
}

inline void pokeb(uint16_t seg, uint16_t off, uint8_t value) {
	*static_cast<uint8_t *>(MK_FP(seg, off)) = value;
}

inline uint16_t peek(uint16_t seg, uint16_t off) {
	return *static_cast<uint16_t *>(MK_FP(seg, off));
}

inline void poke(uint16_t seg, uint16_t off, uint16_t value) {
	*static_cast<uint16_t *>(MK_FP(seg, off)) = value;
}

#endif // PORT_COMPAT_BORLAND_HPP
