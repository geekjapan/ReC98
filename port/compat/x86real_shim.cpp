// port/compat/x86real_shim
// ------------------------
// REC98_PORT definitions for the handful of x86real.h primitives that
// compat/borland.hpp doesn't already cover (MK_FP/peek*/poke* are handled
// there instead): int86(), outportb()/inportb() (via their __outportb__/
// __inportb__ backing calls), getvect()/setvect(), and __emit__(). x86real.h
// declares all of these unconditionally (real Turbo C++ prototypes), so a
// REC98_PORT build still needs *some* definition wherever original ReC98
// code calls them -- this file is that definition, kept separate from
// borland.hpp (a header, included everywhere) so these become ordinary
// single-definition globals instead of accidentally-ODR'd inline functions.
//
// Scope: only the interrupt/port combinations actually reached by the
// TH01 OP.EXE closure (port/CMakeLists.txt's th01op_core) are handled with
// real semantics; everything else asserts loudly rather than silently
// misbehaving, per SPEC.md's "unimplemented paths assert + TODO" convention.

#include "x86real.h"

#include "pc98emu/grcg.hpp"
#include "pc98emu/vram.hpp"
#include "port/shim/input.hpp"

#include <cassert>
#include <cstdio>

extern "C" {

// int 0x18 (PC-98 BIOS video/keyboard) and int 0xDC (legacy ANSI console
// text driver, only reached from th01/hardware/ztext.cpp's z_text_print(),
// itself unreached by the OP.EXE title/menu closure) are the only vectors
// any TU in the closure raises.
int int86(int intno, union REGS *in, union REGS *out) {
	if(intno == 0x18) {
		switch(in->h.ah) {
		case 0x03: // Keyboard: initialize / clear buffer (op_01.cpp, ztext.cpp)
		case 0x0C: // Text: hide display area (ztext.cpp z_text_hide, unreached)
		case 0x0D: // Text: show display area (ztext.cpp z_text_show, unreached)
		case 0x0E: // Text: set single display area (ztext.cpp z_text_init)
		case 0x40: // Graphics: show display (graph.cpp z_graph_show, unreached)
		case 0x41: // Graphics: hide display (graph.cpp z_graph_hide, unreached)
		case 0x42: // Graphics: set mode/lines (graph.cpp z_graph_400line)
			if(out) {
				out->h.ah = 0;
			}
			return 0;

		case 0x04: { // Keyboard: sense keygroup (op_01.cpp keygroup_sense)
			int bits = key_sense(in->h.al);
			if(out) {
				out->h.ah = static_cast<uint8_t>(bits);
			}
			return 0;
		}

		default:
			assert(false && "int86(0x18): unhandled AH subfunction");
			return 0;
		}
	}

	if(intno == 0xDC) {
		// Cosmetic legacy console text output; no host equivalent, and no
		// path in the OP.EXE closure reaches it at runtime.
		return 0;
	}

	assert(false && "int86(): unhandled interrupt number");
	return 0;
}

void (*getvect(int))(...) {
	// No real ISR table on a host process. th01/core/initexit.cpp's
	// getvect(6)/setvect(6, ...) pair (an int06/invalid-opcode safety net,
	// not needed once vsync.cpp's REC98_PORT branch removes the real ISR
	// this was guarding against) is the only call site in the closure.
	return nullptr;
}

void setvect(int, void (*)(...)) {
	// See getvect() above.
}

} // extern "C"

uint8_t __inportb__(uint16_t port) {
	switch(port) {
	case 0x60: // Keyboard scancode status (vsync.cpp's z_vsync_wait, unreached
	           // -- see vsync.hpp/vsync.cpp's REC98_PORT branch)
	case 0x02: // 8259 PIC interrupt mask (vsync.cpp ISR setup, unreached)
		return 0;
	default:
		assert(false && "inportb(): unhandled port");
		return 0;
	}
}

uint8_t __outportb__(uint16_t port, uint8_t value) {
	// Analog palette register write sequence (z_palette_show_single()):
	// 0xA8 latches the color index, then 0xAA/0xAC/0xAE latch the
	// green/red/blue components in that fixed order -- the write to 0xAE
	// always arrives last and is what actually commits the color, so it's
	// the one that calls pc98emu_palette_set().
	static uint8_t s_pal_index = 0;
	static uint8_t s_pal_g = 0;
	static uint8_t s_pal_r = 0;

	switch(port) {
	case 0x7C: // GRCG mode + plane mask
	case 0x7E: // GRCG tile register (cycles B/R/G/E)
		pc98emu_outportb(port, value);
		return value;

	case 0x68: // CG-ROM dot/code access mode toggle (grppsafx.cpp). Bypassed
	           // entirely: platform/x86real/pc98/font.cpp's REC98_PORT
	           // font_read() reads glyph data straight out of pc98emu's
	           // vendored font tables instead of the CG-ROM port protocol,
	           // so these mode-toggle writes are inert either way.
	case 0x6A: // Graphics mode/line-count register (graph.cpp, unreached) and
	           // CG-ROM access-mode companion port (grppsafx.cpp, inert, see
	           // above -- same port number, two different unreached/inert
	           // callers)
	case 0x64: // VSync interrupt trigger ack (vsync.cpp ISR, unreached)
	case 0x02: // 8259 PIC interrupt mask (vsync.cpp ISR setup, unreached)
		return value;

	case 0xA8: // Analog palette: color register select
		s_pal_index = value;
		return value;
	case 0xAA: // ... green,
		s_pal_g = value;
		return value;
	case 0xAC: // ... red,
		s_pal_r = value;
		return value;
	case 0xAE: // ... blue -- commits the color latched above.
		pc98emu_palette_set(s_pal_index, s_pal_r, s_pal_g, value);
		return value;

	default:
		assert(false && "outportb(): unhandled port");
		return value;
	}
}

// 16-bit port I/O: th01/main/hud/hud.cpp inlines `outport(0xA6, page)`
// (accessible-VRAM-page select) directly instead of going through
// port/page.hpp's page_access() macro -- same port, same pc98emu backing
// call, just reached via the 16-bit x86real.h intrinsic instead of the
// 8-bit one.
uint16_t __outportw__(uint16_t port, uint16_t value) {
	switch(port) {
	case 0xA6: // Accessible VRAM page select (th01/main/hud/hud.cpp)
		pc98emu_page_set_accessible(static_cast<int>(value));
		return value;

	default:
		assert(false && "outportw(): unhandled port");
		return value;
	}
}

uint16_t __inportw__(uint16_t port) {
	assert(false && "inportw(): unhandled port");
	return 0;
}

void __emit__(uint8_t, ...) {
	// Raw x86 opcode emission (SBB/OUT/IN via inline machine code) has no
	// host equivalent. Only reached via platform/x86real/pc98/grcg.hpp's
	// GRCG constructor/destructor (`_outportb_(0x7C, ...)`, redefined below
	// to route through outportb() under REC98_PORT instead -- see
	// x86real.h's "Alternate versions" section) and
	// tile_register_from_carry() (unused: GRCG::set_color() has its own
	// REC98_PORT rewrite in platform/x86real/pc98/grcg.cpp that doesn't call
	// it). Kept as a safety-net no-op rather than removed, since it's still
	// declared and could be reached by future closure growth.
}
