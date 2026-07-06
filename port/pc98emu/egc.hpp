// pc98emu
// -------
// Software model of the PC-98 EGC (Enhanced Graphics Charger) 16-dot
// pipeline, scoped to the one usage pattern ReC98 actually drives it with:
// EGCCopy::rect() / rect_interpage() (platform/x86real/pc98/egc.hpp:71-118),
// a pure bit-shifted plane copy (no ROP combination with pattern/destination
// data). Semantics reimplemented from first principles against Neko
// Project 2's mem/memegc.c and io/egc.c as the primary reference (register
// layout, address/length/mask semantics) -- no code copied from there.
//
// Real EGC hardware is driven by CPU MOV instructions into memory-mapped
// VRAM while a byte-at-a-time barrel shifter carries state across accesses.
// We don't emulate a CPU here, so instead of replicating that byte-oriented
// state machine, egc_copy_rect() etc. below perform the equivalent
// bit-for-bit result directly: for each plane, the `w`-dot bit range
// starting at the source position is copied to the `w`-dot bit range
// starting at the destination position, leaving any other bits of the
// (possibly non-16-aligned) first/last touched bytes untouched -- which is
// exactly what the real shifter's boundary masking achieves.
//
// Any EGC mode other than this pure copy (pattern fill, ROP combination
// with destination, TCR/compare) is unimplemented and asserts; no ReC98
// code linked so far drives those paths.

#ifndef PC98EMU_EGC_HPP
#define PC98EMU_EGC_HPP

#include <cstdint>

namespace pc98emu {

constexpr int EGC_REGISTER_DOTS = 16;
constexpr int EGC_REGISTER_SIZE = EGC_REGISTER_DOTS / 8; // 2 bytes
constexpr int EGC_REGISTER_MASK = EGC_REGISTER_DOTS - 1; // 15

// I/O port addresses, numerically matching platform/x86real/pc98/egc.hpp's
// egc_register_t (and, in turn, real EGC hardware / Neko Project 2's
// io/egc.c register file).
enum egc_port_t {
	EGC_ACTIVEPLANEREG = 0x04A0, // bit0-3: 1 = plane (B,R,G,E) excluded from write
	EGC_READPLANEREG   = 0x04A2, // bits 8-9: which plane a CPU read returns
	EGC_MODE_ROP_REG   = 0x04A4, // ROP/mode select; only EGC_ROP_COPY is implemented
	EGC_FGCOLORREG     = 0x04A6, // pattern fill foreground color -- NOT implemented
	EGC_MASKREG        = 0x04A8, // 16-bit per-word bit mask (stipple); 1 = bit is written
	EGC_BGCOLORREG     = 0x04AA, // pattern fill background color -- NOT implemented
	EGC_ADDRRESSREG    = 0x04AC, // (src_bit & 0xF) | ((dst_bit & 0xF) << 4)
	EGC_BITLENGTHREG   = 0x04AE, // dot_count - 1
};

// The one ROP value EGCCopy ever programs: a pure shifted-copy passthrough
// (no logical combination with pattern or destination data). See
// EGCCopy::EGCCopy() in platform/x86real/pc98/egc.cpp:5-13.
constexpr uint16_t EGC_ROP_COPY = 0x29F0;

struct egc_state_t {
	bool active = false;
	uint16_t active_plane = 0xFFF0; // EGC_ACTIVEPLANEREG
	uint16_t read_plane = 0x00FF;   // EGC_READPLANEREG
	uint16_t rop = EGC_ROP_COPY;    // EGC_MODE_ROP_REG
	uint16_t mask = 0xFFFF;         // EGC_MASKREG
	uint16_t address = 0;           // EGC_ADDRRESSREG
	uint16_t bit_length = 0x000F;   // EGC_BITLENGTHREG

	// Last 16 dots read from VRAM per plane (B,R,G,E), updated as a side
	// effect of egc_copy_rect() et al. (the final row's source word).
	uint16_t pattern_latch[4] = {};
};

extern egc_state_t g_egc_state;

} // namespace pc98emu

// Emulates a word-sized (outport-style) write to one of the pc98emu::egc_port_t
// I/O ports above. Byte-sized (outportb-style) writes to these ports aren't
// modeled: ReC98's EGCCopy exclusively uses word I/O (outport()).
//
// Writing EGC_MODE_ROP_REG with anything other than EGC_ROP_COPY, or writing
// EGC_FGCOLORREG/EGC_BGCOLORREG at all, asserts -- those select EGC modes
// this model doesn't implement.
void pc98emu_egc_outport(uint16_t port, uint16_t value);

// Programs the register defaults EGCCopy::EGCCopy() sets up (active_plane =
// 0xFFF0, read_plane = 0x00FF, mask = 0xFFFF, address = 0, rop =
// EGC_ROP_COPY) and marks the EGC active. egc_deactivate() clears the active
// flag; neither is required by egc_copy_rect() et al. below, which work
// regardless -- provided for parity with EGCCopy's RAII activation.
void pc98emu_egc_activate();
void pc98emu_egc_deactivate();

// Rectangle copy within the currently-accessible VRAM page, across all
// non-excluded planes. Mirrors EGCCopy::rect() (egc.cpp:41-84). Coordinates
// are in dots; left/top need not be 16-dot aligned, and w/h need not be
// multiples of 16 -- arbitrary bit-level shifts crossing byte and word
// boundaries are supported. No-op if w <= 0 or h <= 0.
void egc_copy_rect(
	int src_left, int src_top, int dst_left, int dst_top, int w, int h
);

// Rectangle copy from `src_page` to the other page, with independent
// source/destination positions. Mirrors the 7-argument
// EGCCopy::rect_interpage() (egc.cpp:86-136).
void egc_copy_rect_interpage(
	int src_left, int src_top, int dst_left, int dst_top, int w, int h,
	int src_page
);

// Rectangle copy from `src_page` to the other page, at the SAME position on
// both. Mirrors the 5-argument EGCCopy::rect_interpage() (egc.cpp:138-161).
void egc_copy_rect_interpage_same_pos(int left, int top, int w, int h, int src_page);

#endif // PC98EMU_EGC_HPP
