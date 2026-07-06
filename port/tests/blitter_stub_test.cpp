// Unit test for port/shim/blitter_stub.cpp's port of
// platform/x86real/pc98/blitter.cpp: blitter_init_clip()'s clipping
// arithmetic (top/bottom/left/right, both clipped and fully-off-screen
// cases) and write()'s GRCG-routed byte copy, including that it correctly
// skips over blit_source.stride padding between source rows.
//
// Expected VRAM contents for the write() tests are computed independently
// of grcg_vram_write() (port/pc98emu/grcg.cpp), directly from the documented
// GRCG RMW formula (SPEC.md: "p[off] = (p[off] & ~src) | (tile & src)"), not
// by calling into the function under test.

#include <cstdio>
#include <cstring>

#include "port/compat/borland.hpp"

#include "pc98emu/grcg.hpp"
#include "pc98emu/vram.hpp"
#include "platform/grp_clip.hpp"
#include "platform/x86real/pc98/blitter.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

void reset_clip_to_full_screen() {
	Grp_SetClip(nullptr); // GrpClip = {0, 0, ROW_SIZE, RES_Y}
}

} // namespace

int main() {
	// blit_source/blit_state are plain writable globals.
	blit_source.stride = 42;
	blit_source.offset = 7;
	blit_source.w = 16;
	blit_source.h = 8;
	expect(blit_source.stride == 42, "blit_source.stride is writable");
	expect(blit_source.offset == 7, "blit_source.offset is writable");
	expect(blit_source.w == 16, "blit_source.w is writable");
	expect(blit_source.h == 8, "blit_source.h is writable");

	// --- blitter_init_clip(): fully visible sprite --------------------
	reset_clip_to_full_screen();
	blitter_set_source_region(0, 0, 3, 4, 3); // w=3, h=4, stride=3, offset=0
	{
		const Blitter *b = blitter_init_clip(10, 5);
		expect(b != nullptr, "fully visible sprite returns a non-null blitter");
		expect(blit_state.w_clipped == 3, "fully visible: w_clipped == source w");
		expect(blit_state.h_clipped == 4, "fully visible: h_clipped == source h");
		expect(blit_state.sprite_offset == 0, "fully visible: sprite_offset unchanged");
		expect(blit_state.vo == (5 * ROW_SIZE + 10), "fully visible: vo == top*ROW_SIZE + left");
	}

	// --- blitter_init_clip(): top-clipped ------------------------------
	Grp_SetClip(nullptr);
	{
		LTRB<screen_x_t, screen_y_t> clip = {0, 50, RES_X, RES_Y};
		Grp_SetClip(&clip); // GrpClip.top = 50 (region->left/right are in pixels, >>3'd to bytes)
	}
	blitter_set_source_region(0, 0, 4, 10, 4); // w=4, h=10, stride=4
	{
		// top=45 is 5 rows above GrpClip.top=50: the first 5 source rows are
		// clipped away.
		const Blitter *b = blitter_init_clip(0, 45);
		expect(b != nullptr, "top-clipped sprite (still partially visible) returns non-null");
		expect(blit_state.h_clipped == 5, "top-clipped: h_clipped == source h - rows clipped above");
		expect(blit_state.sprite_offset == (0 + 5 * 4), "top-clipped: sprite_offset advances by clipped_rows*stride");
		expect(blit_state.vo == (50 * ROW_SIZE), "top-clipped: vo uses GrpClip.top, not the original top");
	}
	{
		// Entirely above the clip region: h(10) + shift(45-50-10=-15) <= 0.
		const Blitter *b = blitter_init_clip(0, -10);
		expect(b == nullptr, "sprite entirely above GrpClip.top returns nullptr");
	}

	// --- blitter_init_clip(): bottom-clipped ---------------------------
	{
		LTRB<screen_x_t, screen_y_t> clip = {0, 0, RES_X, 20};
		Grp_SetClip(&clip); // GrpClip.bottom = 20
	}
	blitter_set_source_region(0, 0, 4, 10, 4); // w=4, h=10, stride=4
	{
		// top=15, source h=10 -> would reach row 25, but only 5 rows (15..19)
		// are visible before GrpClip.bottom=20.
		const Blitter *b = blitter_init_clip(0, 15);
		expect(b != nullptr, "bottom-clipped sprite (still partially visible) returns non-null");
		expect(blit_state.h_clipped == 5, "bottom-clipped: h_clipped == visible rows before GrpClip.bottom");
		expect(blit_state.sprite_offset == 0, "bottom-clipped: sprite_offset unchanged (top edge not clipped)");
		expect(blit_state.vo == (15 * ROW_SIZE), "bottom-clipped: vo uses the real (unclipped) top");
	}
	{
		// Entirely below the clip region.
		const Blitter *b = blitter_init_clip(0, 25);
		expect(b == nullptr, "sprite entirely below GrpClip.bottom returns nullptr");
	}

	// --- blitter_init_clip(): left/right-clipped ------------------------
	reset_clip_to_full_screen();
	blitter_set_source_region(0, 0, 5, 1, 5); // w=5 bytes, h=1, stride=5
	{
		// left=-2: 2 clipped-off byte-columns on the left.
		const Blitter *b = blitter_init_clip(-2, 0);
		expect(b != nullptr, "left-clipped sprite (still partially visible) returns non-null");
		expect(blit_state.w_clipped == 3, "left-clipped: w_clipped == source w - clipped-off columns");
		expect(blit_state.sprite_offset == 2, "left-clipped: sprite_offset advances by clipped-off columns");
		expect(blit_state.vo == 0, "left-clipped: vo clamped to GrpClip.left");
	}
	{
		// right=GrpClip.right(=ROW_SIZE=80) - 2, sprite w=5 -> only 2 bytes visible.
		const Blitter *b = blitter_init_clip(ROW_SIZE - 2, 0);
		expect(b != nullptr, "right-clipped sprite (still partially visible) returns non-null");
		expect(blit_state.w_clipped == 2, "right-clipped: w_clipped == visible columns before GrpClip.right");
		expect(blit_state.sprite_offset == 0, "right-clipped: sprite_offset unchanged (left edge not clipped)");
		expect(blit_state.vo == (ROW_SIZE - 2), "right-clipped: vo uses the real (unclipped) left");
	}
	{
		// Entirely off the right edge.
		const Blitter *b = blitter_init_clip(ROW_SIZE + 10, 0);
		expect(b == nullptr, "sprite entirely past GrpClip.right returns nullptr");
	}

	// --- write(): GRCG-routed byte copy, independent-formula check -----
	reset_clip_to_full_screen();
	pc98emu_page_set_accessible(0);
	std::memset(pc98emu_planes[0][pc98emu::PLANE_B], 0, PLANE_SIZE);
	// Seed VRAM with a non-zero pattern so the RMW "(p & ~src) | (tile &
	// src)" formula is actually exercised (a zeroed destination can't
	// distinguish RMW from a plain overwrite).
	std::memset(pc98emu_planes[0][pc98emu::PLANE_B] + (3 * ROW_SIZE), 0xFF, ROW_SIZE);

	// 2 source rows of 2 bytes each, with a stride of 3 (i.e. one byte of
	// padding per source row) to prove write() steps by [stride], not just
	// [w_clipped], between rows.
	static const uint8_t src_bytes[6] = {
		0b10101010, 0b01010101, 0xCC, // row 0: 2 live bytes + 1 padding byte
		0b11110000, 0b00001111, 0xCC, // row 1: 2 live bytes + 1 padding byte
	};
	blitter_set_source_region(0, 0, 2, 2, 3); // w=2, h=2, stride=3
	blit_source.dots_start.fp = src_bytes;

	pc98emu_outportb(0x7C, 0xC0); // GC_RMW, no plane excluded
	pc98emu_outportb(0x7E, 0b11110000); // tile B
	pc98emu_outportb(0x7E, 0); // tile R
	pc98emu_outportb(0x7E, 0); // tile G
	pc98emu_outportb(0x7E, 0); // tile E

	{
		const Blitter *b = blitter_init_clip(0, 3); // left=0 (byte-aligned), top=3
		expect(b != nullptr, "write() setup: blitter_init_clip returns non-null for an on-screen sprite");
		expect(b->write != nullptr, "write() setup: BLITTER_FUNCS entry has a non-null write");
		b->write(SEG_PLANE_B);
	}

	const uint8_t tile = 0b11110000;
	const uint8_t *row3 = pc98emu_planes[0][pc98emu::PLANE_B] + (3 * ROW_SIZE);
	const uint8_t *row4 = pc98emu_planes[0][pc98emu::PLANE_B] + (4 * ROW_SIZE);
	expect(
		row3[0] == static_cast<uint8_t>((0xFF & ~src_bytes[0]) | (tile & src_bytes[0])),
		"write() row 0 byte 0 matches the RMW formula computed independently"
	);
	expect(
		row3[1] == static_cast<uint8_t>((0xFF & ~src_bytes[1]) | (tile & src_bytes[1])),
		"write() row 0 byte 1 matches the RMW formula computed independently"
	);
	expect(
		row3[2] == 0xFF,
		"write() does not touch past w_clipped (padding byte's destination is untouched)"
	);
	expect(
		row4[0] == static_cast<uint8_t>((0x00 & ~src_bytes[3]) | (tile & src_bytes[3])),
		"write() row 1 correctly skips the stride padding byte and reads row 1's real data"
	);
	expect(
		row4[1] == static_cast<uint8_t>((0x00 & ~src_bytes[4]) | (tile & src_bytes[4])),
		"write() row 1 byte 1 matches the RMW formula computed independently"
	);

	pc98emu_outportb(0x7C, 0x00); // GC_OFF, leave GRCG in a clean state

	if(g_failures == 0) {
		std::printf("blitter_stub_test: all checks passed\n");
		return 0;
	}
	std::fprintf(stderr, "blitter_stub_test: %d check(s) FAILED\n", g_failures);
	return 1;
}
