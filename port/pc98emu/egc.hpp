// pc98emu
// -------
// Minimal software stub of the PC-98 EGC (Enhanced Graphics Charger). Only
// the pattern-latch read/write and a same-page rectangle copy convenience
// helper are implemented; no real game code driving EGC ROP/mask/shift
// modes is linked yet, so those paths are left as documented TODOs.

#ifndef PC98EMU_EGC_HPP
#define PC98EMU_EGC_HPP

#include <cstdint>

namespace pc98emu {

enum egc_mode_t {
	EGC_MODE_NONE = 0,
	EGC_MODE_RECT_COPY, // placeholder: rectangle-copy mode, see egc_copy_rect()
};

struct egc_state_t {
	uint16_t pattern_latch[4]; // last-read 16 dots, one per plane (B,R,G,E)
	uint8_t read_plane_mask;
	uint8_t write_plane_mask;
	uint16_t bit_address; // EGC_ADDRRESSREG equivalent: byte offset
	uint16_t bit_length; // EGC_BITLENGTHREG equivalent
	egc_mode_t mode;
};

extern egc_state_t g_egc_state;

} // namespace pc98emu

// Reads 16 dots (2 bytes at offset/offset+1) from all four planes of the
// accessible page into pc98emu::g_egc_state.pattern_latch[plane].
void egc_vram_read_latch(uint16_t offset);

// Writes pattern_latch[plane] (16 bits, little-endian, 2 bytes) back to all
// four planes of the accessible page at offset/offset+1.
void egc_vram_write_latch(uint16_t offset);

// Convenience helper: copies a `row_words`-wide by `rows`-tall block of
// 16-dot words from src_offset to dst_offset, both within the SAME page
// (same-page copies only; inter-page EGC copy is a TODO).
// row_stride_words is the word (2-byte) stride between rows -- 40 for a
// full-width (80-byte) row.
void egc_copy_rect(uint16_t src_offset, uint16_t dst_offset, int row_words, int rows, int row_stride_words);

// Sets the current EGC mode. Only EGC_MODE_NONE and EGC_MODE_RECT_COPY are
// recognized; anything else would be a bit-shifted/masked/ROP EGC mode,
// which this minimal stub does NOT implement (asserts instead).
void egc_set_mode(pc98emu::egc_mode_t mode);

#endif // PC98EMU_EGC_HPP
