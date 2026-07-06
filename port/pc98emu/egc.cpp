#include "pc98emu/egc.hpp"
#include "pc98emu/vram.hpp"

#include <cassert>

namespace pc98emu {

egc_state_t g_egc_state = {};

} // namespace pc98emu

void egc_vram_read_latch(uint16_t offset) {
	int page = pc98emu_page_get_accessible();
	for(int p = 0; p < 4; p++) {
		const uint8_t *plane = pc98emu_planes[page][p];
		pc98emu::g_egc_state.pattern_latch[p] =
			static_cast<uint16_t>(plane[offset]) |
			(static_cast<uint16_t>(plane[offset + 1]) << 8);
	}
}

void egc_vram_write_latch(uint16_t offset) {
	int page = pc98emu_page_get_accessible();
	for(int p = 0; p < 4; p++) {
		uint8_t *plane = pc98emu_planes[page][p];
		uint16_t v = pc98emu::g_egc_state.pattern_latch[p];
		plane[offset] = static_cast<uint8_t>(v & 0xFF);
		plane[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
	}
}

void egc_copy_rect(uint16_t src_offset, uint16_t dst_offset, int row_words, int rows, int row_stride_words) {
	// Same-page copies only.
	// TODO: inter-page EGC copy (rect_interpage() in the real EGCCopy API)
	// is not implemented here.
	for(int row = 0; row < rows; row++) {
		uint16_t src_row = static_cast<uint16_t>(src_offset + row * row_stride_words * 2);
		uint16_t dst_row = static_cast<uint16_t>(dst_offset + row * row_stride_words * 2);
		for(int w = 0; w < row_words; w++) {
			uint16_t src_off = static_cast<uint16_t>(src_row + w * 2);
			uint16_t dst_off = static_cast<uint16_t>(dst_row + w * 2);
			egc_vram_read_latch(src_off);
			egc_vram_write_latch(dst_off);
		}
	}
}

void egc_set_mode(pc98emu::egc_mode_t mode) {
	switch(mode) {
	case pc98emu::EGC_MODE_NONE:
	case pc98emu::EGC_MODE_RECT_COPY:
		pc98emu::g_egc_state.mode = mode;
		break;
	default:
		// Bit-shifted/masked/ROP EGC modes (EGC_MODE_ROP_REG, EGC_MASKREG,
		// and the foreground/background color registers) are NOT
		// implemented by this minimal stub -- no real EGC-using game code
		// is linked yet.
		// TODO: not implemented
		assert(false && "EGC ROP/mask/shift modes are not implemented");
		break;
	}
}
