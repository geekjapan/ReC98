// Unit tests for pc98emu/egc.{hpp,cpp}: the EGC rectangle-copy paths used by
// ReC98's EGCCopy::rect() / rect_interpage(). No test framework dependency --
// failures are tallied and reported at the end; CTest just checks exit code.

#include "pc98emu/egc.hpp"
#include "pc98emu/vram.hpp"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

#define CHECK(cond) \
	do { \
		if(!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			g_failures++; \
		} \
	} while(0)

// PC-98 VRAM bit numbering: bit 7 of a byte is its leftmost dot. Kept
// independent of egc.cpp's internal get_bit()/set_bit() on purpose, so a bug
// in one wouldn't be masked by the same bug in the other.
bool read_dot(int page, int plane, int x, int y) {
	int byte_offset = (x / 8) + y * pc98emu::ROW_SIZE;
	int bit = 7 - (x % 8);
	return (pc98emu_planes[page][plane][byte_offset] >> bit) & 1;
}

void write_dot(int page, int plane, int x, int y, bool value) {
	int byte_offset = (x / 8) + y * pc98emu::ROW_SIZE;
	int bit = 7 - (x % 8);
	uint8_t &byte = pc98emu_planes[page][plane][byte_offset];
	if(value) {
		byte = static_cast<uint8_t>(byte | (1 << bit));
	} else {
		byte = static_cast<uint8_t>(byte & ~(1 << bit));
	}
}

void reset_all() {
	pc98emu::g_egc_state = {};
	std::memset(pc98emu_planes, 0, sizeof(pc98emu_planes));
	pc98emu_page_set_accessible(0);
	pc98emu_page_set_shown(0);
}

// Fills every byte of both pages/planes with 0xAA (10101010) so any bit
// EGC touches outside its documented range is easy to spot.
void poison_all() {
	std::memset(pc98emu_planes, 0xAA, sizeof(pc98emu_planes));
}

// Arbitrary, position-dependent boolean pattern used to fill source dots so
// a copy can be checked bit-for-bit rather than just "all set/all clear".
bool pattern_bit(int i, int row) {
	return ((i + row * 7) % 3) != 0;
}

// Expected value, per read_dot()'s bit numbering, of dot x in a plane filled
// with poison_all()'s 0xAA (10101010) byte pattern.
bool poison_bit(int x) {
	return (0xAA >> (7 - (x % 8))) & 1;
}

// ---------------------------------------------------------------------------

void test_byte_aligned_same_page() {
	reset_all();
	poison_all();

	const int left = 16, top = 5, w = 32, h = 1;
	for(int i = 0; i < w; i++) {
		write_dot(0, pc98emu::PLANE_B, left + i, top, pattern_bit(i, 0));
	}
	egc_copy_rect(left, top, left + 64, top, w, h);

	for(int i = 0; i < w; i++) {
		CHECK(read_dot(0, pc98emu::PLANE_B, left + 64 + i, top) == pattern_bit(i, 0));
	}
	// Untouched neighborhood keeps the poison pattern.
	CHECK(read_dot(0, pc98emu::PLANE_B, left + 64 - 1, top) == poison_bit(left + 64 - 1));
	CHECK(read_dot(0, pc98emu::PLANE_B, left + 64 + w, top) == poison_bit(left + 64 + w));
}

// Shifted copy: source and destination start at different, non-16-aligned
// bit offsets, so the copy crosses byte boundaries at a different point on
// each side. This is the case ReC98's copy_width()/rect() shifter logic
// exists for.
void test_shifted_copy_crosses_byte_boundary() {
	reset_all();
	poison_all();

	const int src_left = 3, dst_left = 11, top = 2, w = 20, h = 3;
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			write_dot(0, pc98emu::PLANE_B, src_left + i, top + row, pattern_bit(i, row));
			write_dot(0, pc98emu::PLANE_E, src_left + i, top + row, !pattern_bit(i, row));
		}
	}

	egc_copy_rect(src_left, top, dst_left, top, w, h);

	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			CHECK(read_dot(0, pc98emu::PLANE_B, dst_left + i, top + row) == pattern_bit(i, row));
			CHECK(read_dot(0, pc98emu::PLANE_E, dst_left + i, top + row) == !pattern_bit(i, row));
		}
		// Bits just past the copied range must retain the poison pattern --
		// not be clobbered by the shift. (No check on the leading side here:
		// src_left=3/dst_left=11/w=20 makes the source and destination bit
		// ranges land in the same underlying bytes -- an in-place shift --
		// so bits just before dst_left were already overwritten by the
		// *source* pattern setup above, not left as poison. Non-overlapping
		// edge preservation is covered by test_byte_aligned_same_page() and
		// test_non_multiple_of_8_width().)
		for(int i = w; i < w + 8; i++) {
			CHECK(read_dot(0, pc98emu::PLANE_B, dst_left + i, top + row) == poison_bit(dst_left + i));
		}
	}
}

// Same as above but with source bit offset > destination bit offset (the
// "src_first > dst_first" branch that EGCCopy::rect() special-cases with a
// prefetch; our bit-indexed implementation needs no such special case, but
// the result must still be correct).
void test_shifted_copy_source_ahead_of_dest() {
	reset_all();
	poison_all();

	const int src_left = 13, dst_left = 2, top = 0, w = 25, h = 2;
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			write_dot(0, pc98emu::PLANE_G, src_left + i, top + row, pattern_bit(i, row));
		}
	}

	egc_copy_rect(src_left, top, dst_left, top, w, h);

	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			CHECK(read_dot(0, pc98emu::PLANE_G, dst_left + i, top + row) == pattern_bit(i, row));
		}
	}
}

// Width not a multiple of 8/16 dots -- exercises the partial last byte.
void test_non_multiple_of_8_width() {
	reset_all();
	poison_all();

	const int left = 4, top = 0, w = 5, h = 1;
	for(int i = 0; i < w; i++) {
		write_dot(0, pc98emu::PLANE_R, left + i, top, true);
	}
	egc_copy_rect(left, top, left + 40, top, w, h);

	for(int i = 0; i < w; i++) {
		CHECK(read_dot(0, pc98emu::PLANE_R, left + 40 + i, top) == true);
	}
	// The rest of that last partial byte must be untouched (poison).
	for(int i = w; i < 8; i++) {
		int x = left + 40 + i;
		CHECK(read_dot(0, pc98emu::PLANE_R, x, top) == poison_bit(x));
	}
}

void test_noop_on_non_positive_dims() {
	reset_all();
	poison_all();
	uint8_t before[pc98emu::PLANE_SIZE];
	std::memcpy(before, pc98emu_planes[0][pc98emu::PLANE_B], sizeof(before));

	egc_copy_rect(0, 0, 100, 100, 0, 5);
	egc_copy_rect(0, 0, 100, 100, 5, 0);
	egc_copy_rect(0, 0, 100, 100, -3, 5);

	CHECK(std::memcmp(before, pc98emu_planes[0][pc98emu::PLANE_B], sizeof(before)) == 0);
}

void test_multi_row_stride() {
	reset_all();
	const int left = 0, top = 10, w = 16, h = 4;
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			write_dot(0, pc98emu::PLANE_B, left + i, top + row, (row % 2) == 0);
		}
	}
	egc_copy_rect(left, top, left, top + 100, w, h);
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			CHECK(read_dot(0, pc98emu::PLANE_B, left + i, top + 100 + row) == ((row % 2) == 0));
		}
	}
}

void test_interpage_independent_positions() {
	reset_all();
	poison_all();

	const int src_left = 5, src_top = 1, dst_left = 40, dst_top = 8, w = 18, h = 2;
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			write_dot(0, pc98emu::PLANE_B, src_left + i, src_top + row, pattern_bit(i, row));
		}
	}
	uint8_t page0_before[pc98emu::PLANE_SIZE];
	std::memcpy(page0_before, pc98emu_planes[0][pc98emu::PLANE_B], sizeof(page0_before));

	egc_copy_rect_interpage(src_left, src_top, dst_left, dst_top, w, h, /*src_page=*/0);

	// Source page (0) must be untouched by an interpage copy.
	CHECK(std::memcmp(page0_before, pc98emu_planes[0][pc98emu::PLANE_B], sizeof(page0_before)) == 0);
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			CHECK(read_dot(1, pc98emu::PLANE_B, dst_left + i, dst_top + row) == pattern_bit(i, row));
		}
	}
}

void test_interpage_same_position() {
	reset_all();
	const int left = 7, top = 3, w = 22, h = 2;
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			write_dot(1, pc98emu::PLANE_E, left + i, top + row, pattern_bit(i, row));
		}
	}
	egc_copy_rect_interpage_same_pos(left, top, w, h, /*src_page=*/1);
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			CHECK(read_dot(0, pc98emu::PLANE_E, left + i, top + row) == pattern_bit(i, row));
		}
	}
}

void test_active_plane_exclusion() {
	reset_all();
	poison_all();

	const int left = 0, top = 0, w = 16, h = 1;
	for(int i = 0; i < w; i++) {
		write_dot(0, pc98emu::PLANE_B, left + i, top, true);
		write_dot(0, pc98emu::PLANE_R, left + i, top, true);
	}
	uint8_t plane_r_before[pc98emu::PLANE_SIZE];
	std::memcpy(plane_r_before, pc98emu_planes[0][pc98emu::PLANE_R], sizeof(plane_r_before));

	pc98emu_egc_activate();
	pc98emu_egc_outport(pc98emu::EGC_ACTIVEPLANEREG, 0x0002); // exclude plane R (bit1)

	egc_copy_rect(left, top, left + 64, top, w, h);

	for(int i = 0; i < w; i++) {
		CHECK(read_dot(0, pc98emu::PLANE_B, left + 64 + i, top) == true);
	}
	// Plane R was excluded from the write -- destination stays poison, and
	// the source-side copy of plane R (dest region) is untouched entirely.
	CHECK(std::memcmp(plane_r_before, pc98emu_planes[0][pc98emu::PLANE_R], sizeof(plane_r_before)) == 0);

	pc98emu_egc_deactivate();
}

void test_mask_register_gates_writes() {
	reset_all();
	poison_all();

	const int left = 0, top = 0, w = 16, h = 1;
	for(int i = 0; i < w; i++) {
		write_dot(0, pc98emu::PLANE_B, left + i, top, true);
	}
	pc98emu_egc_activate();
	pc98emu_egc_outport(pc98emu::EGC_MASKREG, 0xF0F0);

	egc_copy_rect(left, top, left + 32, top, w, h);

	for(int i = 0; i < w; i++) {
		bool allowed = (0xF0F0 >> (15 - i)) & 1;
		bool expected = allowed ? true : poison_bit(left + 32 + i);
		CHECK(read_dot(0, pc98emu::PLANE_B, left + 32 + i, top) == expected);
	}

	pc98emu_egc_outport(pc98emu::EGC_MASKREG, 0xFFFF); // restore full mask
	pc98emu_egc_deactivate();
}

void test_pattern_latch_reflects_last_row_source() {
	reset_all();
	const int left = 0, top = 0, w = 16, h = 3;
	for(int row = 0; row < h; row++) {
		for(int i = 0; i < w; i++) {
			write_dot(0, pc98emu::PLANE_B, left + i, top + row, pattern_bit(i, row));
		}
	}
	egc_copy_rect(left, top, left + 32, top, w, h);

	// pattern_latch is the raw little-endian word (byte0 | byte1 << 8) of the
	// last row's source bytes, each byte using bit7 = leftmost dot -- so dot
	// n (0..7) lands at bit (7-n) of byte0, and dot n (8..15) at bit
	// (7-(n-8)) of byte1. NOT a simple "bit (15-n)" across the whole word.
	uint8_t byte0 = 0, byte1 = 0;
	for(int n = 0; n < 8; n++) {
		if(pattern_bit(n, h - 1)) {
			byte0 = static_cast<uint8_t>(byte0 | (1 << (7 - n)));
		}
	}
	for(int n = 8; n < 16; n++) {
		if(pattern_bit(n, h - 1)) {
			byte1 = static_cast<uint8_t>(byte1 | (1 << (7 - (n - 8))));
		}
	}
	uint16_t expected = static_cast<uint16_t>(byte0 | (byte1 << 8));
	CHECK(pc98emu::g_egc_state.pattern_latch[pc98emu::PLANE_B] == expected);
}

void test_activate_sets_egccopy_defaults() {
	reset_all();
	pc98emu_egc_activate();
	CHECK(pc98emu::g_egc_state.active == true);
	CHECK(pc98emu::g_egc_state.active_plane == 0xFFF0);
	CHECK(pc98emu::g_egc_state.read_plane == 0x00FF);
	CHECK(pc98emu::g_egc_state.mask == 0xFFFF);
	CHECK(pc98emu::g_egc_state.address == 0);
	CHECK(pc98emu::g_egc_state.rop == pc98emu::EGC_ROP_COPY);
	pc98emu_egc_deactivate();
	CHECK(pc98emu::g_egc_state.active == false);
}

void test_outport_updates_registers() {
	reset_all();
	pc98emu_egc_outport(pc98emu::EGC_ADDRRESSREG, 0x00A5);
	CHECK(pc98emu::g_egc_state.address == 0x00A5);
	pc98emu_egc_outport(pc98emu::EGC_BITLENGTHREG, 0x000F);
	CHECK(pc98emu::g_egc_state.bit_length == 0x000F);
	pc98emu_egc_outport(pc98emu::EGC_MODE_ROP_REG, pc98emu::EGC_ROP_COPY);
	CHECK(pc98emu::g_egc_state.rop == pc98emu::EGC_ROP_COPY);
}

} // namespace

int main() {
	test_byte_aligned_same_page();
	test_shifted_copy_crosses_byte_boundary();
	test_shifted_copy_source_ahead_of_dest();
	test_non_multiple_of_8_width();
	test_noop_on_non_positive_dims();
	test_multi_row_stride();
	test_interpage_independent_positions();
	test_interpage_same_position();
	test_active_plane_exclusion();
	test_mask_register_gates_writes();
	test_pattern_latch_reflects_last_row_source();
	test_activate_sets_egccopy_defaults();
	test_outport_updates_registers();

	if(g_failures == 0) {
		std::printf("egc_test: all checks passed\n");
		return 0;
	}
	std::fprintf(stderr, "egc_test: %d check(s) FAILED\n", g_failures);
	return 1;
}
