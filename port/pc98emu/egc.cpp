#include "pc98emu/egc.hpp"
#include "pc98emu/vram.hpp"

#include <cassert>
#include <vector>

namespace pc98emu {

egc_state_t g_egc_state = {};

} // namespace pc98emu

namespace {

using pc98emu::EGC_REGISTER_DOTS;
using pc98emu::EGC_REGISTER_MASK;

// Byte offset of an 8-dot-aligned X/Y position on a VRAM plane, relative to
// the start of the plane. Mirrors planar.h's vram_offset_shift(), kept
// self-contained here since pc98emu doesn't depend on ReC98 headers.
int vram_offset(int x, int y) {
	return (x / 8) + y * pc98emu::ROW_SIZE;
}

// PC-98 VRAM bit numbering: bit 7 of a byte is its leftmost dot.
bool get_bit(const uint8_t *plane, int bit_index) {
	int byte = bit_index / 8;
	int bit = 7 - (bit_index % 8);
	return (plane[byte] >> bit) & 1;
}

void set_bit(uint8_t *plane, int bit_index, bool value) {
	int byte = bit_index / 8;
	int bit = 7 - (bit_index % 8);
	if(value) {
		plane[byte] = static_cast<uint8_t>(plane[byte] | (1 << bit));
	} else {
		plane[byte] = static_cast<uint8_t>(plane[byte] & ~(1 << bit));
	}
}

// EGC_MASKREG is a 16-bit stipple that repeats every destination word,
// independent of the shift amount: dot 0 (leftmost) of any 16-dot-aligned
// word is gated by mask bit 15, dot 15 (rightmost) by mask bit 0.
bool mask_allows_dot(int dst_first, int dot_index_in_copy) {
	int dot_in_word = (dst_first + dot_index_in_copy) % EGC_REGISTER_DOTS;
	int bit_in_reg = EGC_REGISTER_MASK - dot_in_word;
	return (pc98emu::g_egc_state.mask >> bit_in_reg) & 1;
}

// Copies the `num_dots`-bit range starting at `src_bit_base` to the
// `num_dots`-bit range starting at `dst_bit_base`, for one plane. Bits of
// partially-touched edge bytes outside that range are left untouched, and
// bits gated off by EGC_MASKREG are skipped -- both matching real EGC
// boundary/mask behavior.
//
// A same-page rect() copy where |dst_first - src_first| < w has source and
// destination bit ranges landing in the SAME underlying word(s) -- e.g. an
// in-place horizontal shift/scroll. Real EGC hardware handles this via its
// internal read-ahead pipeline; we get the same result by reading the whole
// source range into a buffer before writing anything, which sidesteps the
// read-after-write hazard regardless of overlap direction (a plain
// increasing-index loop would otherwise clobber not-yet-read source bits).
void copy_row_bits(
	const uint8_t *src_plane, int src_bit_base,
	uint8_t *dst_plane, int dst_bit_base,
	int dst_first, int num_dots
) {
	std::vector<uint8_t> bits(static_cast<size_t>(num_dots));
	for(int i = 0; i < num_dots; i++) {
		bits[i] = get_bit(src_plane, src_bit_base + i);
	}
	for(int i = 0; i < num_dots; i++) {
		if(!mask_allows_dot(dst_first, i)) {
			continue;
		}
		set_bit(dst_plane, dst_bit_base + i, bits[i]);
	}
}

void copy_rect_generic(
	uint8_t *const src_planes[4], int src_left, int src_top,
	uint8_t *const dst_planes[4], int dst_left, int dst_top,
	int w, int h
) {
	if(w <= 0 || h <= 0) {
		return;
	}

	const int src_first = src_left & EGC_REGISTER_MASK;
	const int dst_first = dst_left & EGC_REGISTER_MASK;

	pc98emu::g_egc_state.address = static_cast<uint16_t>(src_first | (dst_first << 4));
	pc98emu::g_egc_state.bit_length = static_cast<uint16_t>(w - 1);

	for(int row = 0; row < h; row++) {
		const int src_word = vram_offset(src_left, src_top + row) & ~1;
		const int dst_word = vram_offset(dst_left, dst_top + row) & ~1;
		const int src_bit_base = src_word * 8 + src_first;
		const int dst_bit_base = dst_word * 8 + dst_first;

		for(int p = 0; p < 4; p++) {
			if((pc98emu::g_egc_state.active_plane >> p) & 1) {
				continue; // plane excluded from write
			}
			copy_row_bits(src_planes[p], src_bit_base, dst_planes[p], dst_bit_base, dst_first, w);
		}
	}

	const int last_src_word = vram_offset(src_left, src_top + h - 1) & ~1;
	for(int p = 0; p < 4; p++) {
		pc98emu::g_egc_state.pattern_latch[p] =
			static_cast<uint16_t>(src_planes[p][last_src_word]) |
			(static_cast<uint16_t>(src_planes[p][last_src_word + 1]) << 8);
	}
}

} // namespace

void pc98emu_egc_outport(uint16_t port, uint16_t value) {
	using namespace pc98emu;
	switch(port) {
	case EGC_ACTIVEPLANEREG:
		g_egc_state.active_plane = value;
		break;
	case EGC_READPLANEREG:
		g_egc_state.read_plane = value;
		break;
	case EGC_MODE_ROP_REG:
		g_egc_state.rop = value;
		assert(value == EGC_ROP_COPY && "EGC ROP modes other than pure copy are not implemented");
		break;
	case EGC_FGCOLORREG:
	case EGC_BGCOLORREG:
		// TODO: pattern-color fill modes are not implemented.
		assert(false && "EGC pattern fill (fg/bg color) registers are not implemented");
		break;
	case EGC_MASKREG:
		g_egc_state.mask = value;
		break;
	case EGC_ADDRRESSREG:
		g_egc_state.address = value;
		break;
	case EGC_BITLENGTHREG:
		g_egc_state.bit_length = value;
		break;
	default:
		break; // not an EGC port
	}
}

void pc98emu_egc_activate() {
	using namespace pc98emu;
	g_egc_state.active = true;
	g_egc_state.active_plane = 0xFFF0;
	g_egc_state.read_plane = 0x00FF;
	g_egc_state.mask = 0xFFFF;
	g_egc_state.address = 0;
	g_egc_state.rop = EGC_ROP_COPY;
}

void pc98emu_egc_deactivate() {
	pc98emu::g_egc_state.active = false;
}

void egc_copy_rect(
	int src_left, int src_top, int dst_left, int dst_top, int w, int h
) {
	int page = pc98emu_page_get_accessible();
	uint8_t *planes[4] = {
		pc98emu_planes[page][0], pc98emu_planes[page][1],
		pc98emu_planes[page][2], pc98emu_planes[page][3],
	};
	copy_rect_generic(planes, src_left, src_top, planes, dst_left, dst_top, w, h);
}

void egc_copy_rect_interpage(
	int src_left, int src_top, int dst_left, int dst_top, int w, int h,
	int src_page
) {
	const int dst_page = src_page ^ 1;
	uint8_t *src_planes[4] = {
		pc98emu_planes[src_page][0], pc98emu_planes[src_page][1],
		pc98emu_planes[src_page][2], pc98emu_planes[src_page][3],
	};
	uint8_t *dst_planes[4] = {
		pc98emu_planes[dst_page][0], pc98emu_planes[dst_page][1],
		pc98emu_planes[dst_page][2], pc98emu_planes[dst_page][3],
	};
	copy_rect_generic(src_planes, src_left, src_top, dst_planes, dst_left, dst_top, w, h);
}

void egc_copy_rect_interpage_same_pos(int left, int top, int w, int h, int src_page) {
	egc_copy_rect_interpage(left, top, left, top, w, h, src_page);
}
