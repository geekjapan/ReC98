// port/shim/blitter_stub
// ------------------------
// Port of platform/x86real/pc98/blitter.cpp's byte-aligned sprite blitter
// (originally raw pseudoregister/segment arithmetic: _AX/_BX/_SI/_DI/_ES/_DS,
// `__es`-qualified pointer casts reconstructing real 16-bit segment:offset
// addressing, REP MOVSB/MOVSW/MOVSD) onto pc98emu.
//
// Scope: only the BLITTER_FUNCS default pair (write_def/write_offscreen_def
// in the original) is ported, not blit_low.hpp's width-specialized
// DisplacedRW/MarchUW/MarchSW variants. Those are registered exclusively by
// th01/pc98/blitters.cpp's blitter_setup(), which -- per T9's dependency-
// closure survey (OP_DEPS.md) -- is never called anywhere in this port's
// closure (port/app/main_op.cpp and main_reiiden.cpp call main_op()/
// main_main() directly rather than reproducing the DOS multi-entrypoint
// dispatcher that would run blitter_setup()'s `#pragma startup`). That means
// the original blitter_funcs_init() always leaves every BLITTER_FUNCS[]
// entry at its default (it only overwrites a slot "if(!p->write)"), so
// porting the default pair is a complete, faithful port of what's actually
// reachable -- not a partial one.
//
// th01/main/bullet/pellet.cpp is the only reachable call site
// (`blitter_init_clip()` + `b->write(SEG_PLANE_B)`), so `write()` and
// `blitter_init_clip()` below are fully ported and tested.
// `blitter_init_noclip()` is also ported (cheap, and shares all its logic
// with `blitter_init_clip()`) even though its only caller,
// platform/x86real/pc98/grp_surf.cpp, isn't compiled into either port
// executable (confirmed via `grep -rn blitter_init_noclip` across th01/,
// platform/, port/ -- absent from port/CMakeLists.txt's source lists).
//
// `write_offscreen()` and `Blitter::snap()` are NOT ported -- see their
// definitions/absence below for why.
//
// blit_source.dots_start is a Turbo C++ far-pointer union of a {off, seg}
// pair and a flat `void far*` (`far` expands to nothing under REC98_PORT).
// On real 16-bit x86 those alias exactly (a far pointer *is* a seg:off
// pair); on a 64-bit host they can't -- a 64-bit pointer doesn't fit the
// {uint16_t, uint16_t} layout the way it did on 16-bit x86. The only
// reachable writer (pellet.cpp) already sets `.fp` to a plain host array
// pointer (`&sPELLET[cel][...]`), so this port reads `.dots_start.fp`
// exclusively and never reconstructs a segment:offset pair from `.part`.
// grp_surf.cpp's `.part.seg`-based usage (VRAM-to-VRAM snapshotting via
// `snap()`) is unreached here and would need a different addressing model
// than the one below anyway.
#include <cassert>
#include <cstdint>

#include "port/compat/borland.hpp"

#include "pc98emu/grcg.hpp"
#include "platform/grp_clip.hpp"
#include "platform/x86real/pc98/blitter.hpp"

blit_source_t blit_source;
blit_state_t blit_state;

// Declared the same way platform/x86real/pc98/blitter.cpp (not compiled
// here) itself declares it -- platform/grp_clip.hpp only declares
// Grp_SetClip(), not the GrpClip variable it maintains.
extern LTRB<vram_x_t, screen_y_t> GrpClip;

namespace {

unsigned plane_index_from_seg(seg_t plane_seg) {
	switch(plane_seg) {
	case SEG_PLANE_B: return PL_B;
	case SEG_PLANE_R: return PL_R;
	case SEG_PLANE_G: return PL_G;
	case SEG_PLANE_E: return PL_E;
	}
	assert(false && "blitter: plane_seg is not one of SEG_PLANE_B/R/G/E");
	return PL_B;
}

// Shared byte-copy loop behind write_def(): writes blit_state.h_clipped rows
// of blit_state.w_clipped bytes each, from the sprite source
// (blit_source.dots_start.fp, advanced by blit_source.stride per row) to the
// VRAM plane named by plane_seg starting at blit_state.vo (advancing by
// ROW_SIZE per row -- real VRAM rows are always ROW_SIZE(80) bytes apart,
// regardless of how many of those bytes are actually clipped-visible).
// Routed through pc98emu's GRCG-aware grcg_vram_write() one byte at a time:
// real hardware's GRCG intercepts every VRAM-segment bus write regardless of
// whether the CPU batched it into a MOVSB/MOVSW/MOVSD, so a per-byte
// grcg_vram_write() sequence reproduces the same RMW/TDW/OFF tile fan-out as
// the original REP MOVS instructions.
void blit_rows(seg_t plane_seg, vram_offset_t dst_vo) {
	const unsigned plane = plane_index_from_seg(plane_seg);
	const uint8_t *src_base = static_cast<const uint8_t *>(blit_source.dots_start.fp);
	uint16_t src_offset = blit_state.sprite_offset;

	for(upixel_t row = 0; row < blit_state.h_clipped; row++) {
		const uint8_t *src_row = src_base + src_offset;
		for(vram_byte_amount_t col = 0; col < blit_state.w_clipped; col++) {
			grcg_vram_write(plane, static_cast<uint16_t>(dst_vo + col), src_row[col], 8);
		}
		src_offset = static_cast<uint16_t>(src_offset + blit_source.stride);
		dst_vo = static_cast<vram_offset_t>(dst_vo + ROW_SIZE);
	}
}

} // namespace

void __fastcall write_def(seg_t plane_seg) {
	blit_rows(plane_seg, blit_state.vo);
}

void __fastcall write_offscreen_def(seg_t) {
	// Not ported: nothing in this port's closure calls ->write_offscreen()
	// (only the width-specialized MarchUW/MarchSW variants registered by
	// th01/pc98/blitters.cpp's unreachable blitter_setup() ever would have).
	// Its real behavior also isn't a simple parameter change on blit_rows()
	// above: with `offscreen=true`, the original's shared copy loop skips
	// the "reset destination to a ROW_SIZE-aligned row start" step, so
	// successive rows pack contiguously (by width, not by ROW_SIZE) into a
	// scratch buffer -- and pc98emu's plane arrays (port/pc98emu/vram.hpp's
	// pc98emu_planes[2][4][32000]) are sized to exactly PLANE_SIZE with no
	// such offscreen scratch region the way real hardware's oversized VRAM
	// segment provided one past the visible RES_Y rows. A real port would
	// need to add that scratch region to pc98emu first; asserting here is
	// honest about the gap rather than silently corrupting VRAM.
	assert(false && "blitter: write_offscreen() has no offscreen VRAM scratch region in this port (unreached in this port's closure)");
}

// Blitter::snap() is intentionally left undefined. It's declared in
// platform/x86real/pc98/blitter.hpp but never referenced by anything this
// port compiles (only platform/x86real/pc98/grp_surf.cpp calls `.snap()`,
// and that TU isn't in port/CMakeLists.txt's source lists), so no definition
// is required for the link to succeed. It's also not just a parameter
// variation on write_def()/blit_rows() above: snap() captures VRAM *into* a
// buffer, so unlike write()'s "source = blit_source.dots_start.fp, dest =
// named plane" split, its DS:SI source is the VRAM segment itself (bound via
// blit_source.dots_start.part.seg, e.g. SEG_PLANE_B) while blit_source's
// stride/w/h describe the *destination* capture buffer -- the opposite of
// what write()'s (and this port's) .fp-only sourcing model supports.

Blitter BLITTER_FUNCS[ROW_SIZE + 1];

namespace {

// Runs at static-init time -- equivalent to the original's
// `#pragma startup blitter_funcs_init` (which ran unconditionally before
// main() on real hardware, since nothing in this port's closure ever
// registers a specialized width variant to pre-fill a slot and be skipped).
struct BlitterFuncsInit {
	BlitterFuncsInit() {
		for(vram_byte_amount_t i = 0; i <= ROW_SIZE; i++) {
			BLITTER_FUNCS[i].write = write_def;
			BLITTER_FUNCS[i].write_offscreen = write_offscreen_def;
		}
	}
} g_blitter_funcs_init;

} // namespace

// Initialization
// --------------
// Direct, non-asm translation of platform/x86real/pc98/blitter.cpp's
// blitter_init_clip()/blitter_init_noclip() pseudoregister arithmetic --
// same clipping formulas, plain `int` locals instead of _AX/_BX/_CX/_DX.

const Blitter __ds *__fastcall blitter_init_clip(vram_x_t left, vram_y_t top) {
	int sprite_left = left;
	int sprite_top = top;

	// Top and bottom edges
	if(sprite_top < GrpClip.top) {
		const int shift = sprite_top - GrpClip.top; // <= 0
		const int h = blit_source.h + shift;
		if(h <= 0) {
			return nullptr;
		}
		blit_state.h_clipped = static_cast<upixel_t>(h);
		blit_state.sprite_offset = static_cast<uint16_t>(
			blit_source.offset + (-shift * blit_source.stride)
		);
		sprite_top = GrpClip.top;
	} else {
		const int visible = GrpClip.bottom - sprite_top;
		if(visible < blit_source.h) {
			if(visible <= 0) {
				return nullptr;
			}
			blit_state.h_clipped = static_cast<upixel_t>(visible);
		} else {
			blit_state.h_clipped = static_cast<upixel_t>(blit_source.h);
		}
		blit_state.sprite_offset = static_cast<uint16_t>(blit_source.offset);
	}

	blit_state.vo = static_cast<vram_offset_t>(sprite_top * ROW_SIZE);

	// Left and right edges
	if(sprite_left < GrpClip.left) {
		const int shift = sprite_left - GrpClip.left; // <= 0
		const int w = blit_source.w + shift;
		if(w <= 0) {
			return nullptr;
		}
		blit_state.sprite_offset = static_cast<uint16_t>(blit_state.sprite_offset - shift);
		blit_state.vo = static_cast<vram_offset_t>(blit_state.vo + GrpClip.left);
		blit_state.w_clipped = static_cast<vram_byte_amount_t>(w);
		return &BLITTER_FUNCS[w];
	} else {
		const int visible = GrpClip.right - sprite_left;
		if(visible < blit_source.w) {
			if(visible <= 0) {
				return nullptr;
			}
			blit_state.vo = static_cast<vram_offset_t>(blit_state.vo + sprite_left);
			blit_state.w_clipped = static_cast<vram_byte_amount_t>(visible);
			return &BLITTER_FUNCS[visible];
		} else {
			blit_state.vo = static_cast<vram_offset_t>(blit_state.vo + sprite_left);
			blit_state.w_clipped = static_cast<vram_byte_amount_t>(blit_source.w);
			return &BLITTER_FUNCS[blit_source.w];
		}
	}
}

const Blitter __ds &__fastcall blitter_init_noclip(vram_x_t left, vram_y_t top) {
	blit_state.vo = static_cast<vram_offset_t>((top * ROW_SIZE) + left);
	blit_state.sprite_offset = static_cast<uint16_t>(blit_source.offset);
	blit_state.h_clipped = static_cast<upixel_t>(blit_source.h);
	blit_state.w_clipped = static_cast<vram_byte_amount_t>(blit_source.w);
	return BLITTER_FUNCS[blit_source.w];
}
// --------------
