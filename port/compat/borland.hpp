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
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include "pc98emu/pc98emu.hpp"

// Turbo C++'s <dos.h> millisecond busy-wait, used by th01/hardware/graph.cpp's
// palette fade loops between frames. A real sleep is a fine, simpler
// replacement here (unlike frame_delay()'s vsync-driven waits, no rendering
// needs to happen while this blocks).
inline void delay(unsigned int ms) {
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

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

// Turbo C++'s segment-qualified pointer keywords (`void __seg *`/`void __ds
// *`, used by master.lib's hmem_/resdata_/dos_free() family for DOS segment
// handles, and by libs/piloadc's PiLoad declarations for "current DS"
// pointers respectively). Not real clang keywords; meaningless in a flat
// address space, same as far/near above.
#define __seg
#define __ds

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
// Fake BIOS Data Area: op_01.cpp pokes/peeks segment 0 directly for a couple
// of BIOS work-area bytes (BIOS_FLAG at 0:0x500, the keyboard buffer
// head/tail/count at 0:0x524/0x526/0x528) as part of its keyboard-buffer
// housekeeping. None of that state is read back by anything meaningful on
// the port (there's no real BIOS keyboard buffer to clear), so a plain
// zero-initialized scratch buffer is enough to satisfy the read/write
// without asserting -- it just needs to not crash.
inline uint8_t *fake_bda() {
	static uint8_t bda[0x600] = {};
	return bda;
}

inline void *resolve_far(uint16_t seg, uint16_t off) {
	constexpr uint16_t SEG_PLANE_B_ = 0xA800;
	constexpr uint16_t SEG_PLANE_R_ = 0xB000;
	constexpr uint16_t SEG_PLANE_G_ = 0xB800;
	constexpr uint16_t SEG_PLANE_E_ = 0xE000;
	constexpr uint32_t PLANE_BYTES_ = 32000; // pc98emu::PLANE_SIZE

	if(seg == 0) {
		assert(off < 0x600 && "resolve_far(): BIOS Data Area offset out of "
		                      "range of the fake_bda() scratch buffer");
		return fake_bda() + off;
	}

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

// Turbo C++ intrinsic: 16-bit rotate right. th01/hardware/grppsafx.cpp's
// graph_putsa_fx() uses this (rotr by 8, i.e. a byte swap) to move a glyph
// row's bytes into the layout its VRAM-write path expects.
inline uint16_t __rotr__(uint16_t val, int shift) {
	shift &= 15;
	return static_cast<uint16_t>((val >> shift) | (val << (16 - shift)));
}

// Turbo C++ intrinsics: 32-bit rotate left/right (th01/hardware/grp2xscs.cpp).
inline uint32_t _lrotl(uint32_t val, int shift) {
	shift &= 31;
	return (shift == 0) ? val : ((val << shift) | (val >> (32 - shift)));
}
inline uint32_t _lrotr(uint32_t val, int shift) {
	shift &= 31;
	return (shift == 0) ? val : ((val >> shift) | (val << (32 - shift)));
}

// Borland RTL heap-introspection function (<alloc.h>, but reached without an
// #include anywhere in the original codebase -- old Turbo C++ apparently
// didn't require one). th01/main_01.cpp/th01/main/debug.cpp only ever read
// its result for debug-mode diagnostic printouts (coreleft_prev deltas), so
// an exact free-heap byte count isn't meaningful on a host process with its
// own virtual memory -- return a large constant so those deltas don't
// underflow.
inline unsigned long coreleft(void) {
	return 0x7FFFFFFFUL;
}

// Turbo C++ <conio.h>: non-blocking "is a key waiting" check, used only for
// a debug-mode status printout (th01/main/debug.cpp). No host equivalent
// wired up (nothing in the closure branches on it), so always report "no".
inline int kbhit(void) {
	return 0;
}

// Turbo C++ <alloc.h> heap-checking API. th01/main/debug.cpp's debug-mode-
// only diagnostics (interactive dev cheat menu, gated behind
// resident->debug_mode != DM_OFF) use these to print heap health; a host
// process's heap isn't the same kind of thing to validate, so heapcheck()/
// heapchecknode() unconditionally report "fine" rather than walking
// anything real.
enum {
	_BADNODE = -3,
	_HEAPCORRUPT = -2,
	_HEAPEMPTY = -1,
	_HEAPOK = 0,
	_FREEENTRY = 1,
	_USEDENTRY = 2,
};

inline int heapcheck(void) {
	return _HEAPOK;
}

inline int heapchecknode(void *) {
	return _HEAPOK;
}

// Turbo C++ <float.h> FPU control-word API. th01/main_01.cpp's main_main()
// calls _control87(MCW_EM, MCW_EM) once at startup to mask all FPU
// exceptions -- standard DOS FP-exception hygiene, not something a host
// process needs (IEEE 754 FP exceptions are masked by default on both
// x86_64 and arm64), so this is a no-op stub rather than a real
// <cfenv>-based reimplementation.
#define MCW_EM 0x3F

inline unsigned int _control87(unsigned int, unsigned int) {
	return 0;
}

// Turbo C++ <alloc.h> "far heap" allocator (able to allocate more than one
// 64 KiB segment's worth at once, unlike malloc()/calloc()). A host
// process's heap has no such segment limit to begin with, so these just
// forward to their standard-library equivalents.
inline void *farmalloc(unsigned long size) {
	return std::malloc(size);
}
inline void *farcalloc(unsigned long count, unsigned long size) {
	return std::calloc(count, size);
}
inline void farfree(void *ptr) {
	std::free(ptr);
}

// DOS Program Segment Prefix segment. th01/main/stage/stageobj.cpp's
// glitch-stage segment-relocation hack (emulating ZUN's famous
// uninitialized-memory reads) is the only reader; a real value doesn't
// exist without a DOS process, and it only affects the exact byte content
// of that emulated glitch data, not whether the code runs -- 0 is as good
// as any other placeholder.
static const uint16_t _psp = 0;

#endif // PORT_COMPAT_BORLAND_HPP
