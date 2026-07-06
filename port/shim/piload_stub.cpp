// port/shim/piload_stub.cpp
// -----------------------------------------------------------------------
// Stand-in for libs/piloadc's PiBlitL(), a real 16-bit assembly .PI/.GRP
// decoder (see libs/piloadc/piloadc.asm, piloadm.asm, and
// https://mooncore.eu/bunny/txt/pi-pic.htm for the actual, asset-compatible
// RLE + differential compression format). Reimplementing that byte-exactly
// is out of scope for th01/formats compilation + testing (T8): grp.cpp only
// needs *some* PiBlitL to link against so its own wrapper logic (buffer
// paragraph alignment, palette extraction at buf+0x100, GPF_COLORKEY
// handling, direct-to-VRAM decode) can be exercised with synthetic test
// data. This does NOT read real ZUN .GRP assets -- if a later milestone
// needs that (see SPEC.md M1), replace this with an actual port of
// piloadc.asm/piloadm.asm.
//
// Synthetic wire format read via `read_func` (test-only, NOT the real .PI
// format):
//   uint8_t magic[2]       -- ignored (kept only for symmetry with the
//                             real format's 2-byte magic)
//   int8_t  palette[16][3] -- raw Palette4 bytes (r,g,b per color)
//   uint8_t plane_b[32000]
//   uint8_t plane_r[32000]
//   uint8_t plane_g[32000]
//   uint8_t plane_e[32000] -- 640x400 planar pixel data, B/R/G/E order
// Total: 2 + 48 + 4*32000 = 128050 bytes.

#include "port/compat/borland.hpp"

#include <cstdint>

#include "libs/piloadc/piloadm.hpp"
#include "pc98emu/vram.hpp"

namespace {

constexpr int kPlaneSize = pc98emu::PLANE_SIZE; // 32000
constexpr int kPaletteBytes = 16 * 3; // Palette4: 16 colors * 3 int8_t components
constexpr int kColorKeyFlag = 0x40;

} // namespace

int PiBlitL(
	void *buff, uint16_t Size, int X, int Y, int option,
	int (*read_func)(void *buf, unsigned size)
) {
	(void)Size;

	uint8_t magic[2];
	read_func(magic, sizeof(magic));

	// grp.cpp reads the palette back from a *paragraph-aligned* (16-byte)
	// rounding of `buff`, matching real PiLoad's documented behavior of
	// writing header/palette data at a paragraph-aligned position within
	// the caller's buffer. Replicate the same rounding here so both sides
	// agree on the address.
	uint8_t *aligned = reinterpret_cast<uint8_t *>(
		(reinterpret_cast<uintptr_t>(buff) + 15) & ~static_cast<uintptr_t>(15));
	read_func(aligned + 0x100, kPaletteBytes);

	static uint8_t plane_b[kPlaneSize];
	static uint8_t plane_r[kPlaneSize];
	static uint8_t plane_g[kPlaneSize];
	static uint8_t plane_e[kPlaneSize];
	read_func(plane_b, kPlaneSize);
	read_func(plane_r, kPlaneSize);
	read_func(plane_g, kPlaneSize);
	read_func(plane_e, kPlaneSize);

	const bool colorkey = (option & kColorKeyFlag) != 0;
	const uint8_t colorkey_color = static_cast<uint8_t>((option >> 8) & 0xFF);

	for(int y = 0; y < pc98emu::RES_Y; y++) {
		int vram_y = y + Y;
		if(vram_y < 0 || vram_y >= pc98emu::RES_Y) {
			continue;
		}
		for(int col = 0; col < pc98emu::ROW_SIZE; col++) {
			int vram_x_byte = col + (X / 8);
			if(vram_x_byte < 0 || vram_x_byte >= pc98emu::ROW_SIZE) {
				continue;
			}
			int src_off = (y * pc98emu::ROW_SIZE) + col;
			int dst_off = (vram_y * pc98emu::ROW_SIZE) + vram_x_byte;

			uint8_t bb = plane_b[src_off];
			uint8_t rr = plane_r[src_off];
			uint8_t gg = plane_g[src_off];
			uint8_t ee = plane_e[src_off];

			if(!colorkey) {
				VRAM_PLANE_B[dst_off] = bb;
				VRAM_PLANE_R[dst_off] = rr;
				VRAM_PLANE_G[dst_off] = gg;
				VRAM_PLANE_E[dst_off] = ee;
				continue;
			}

			// Per-dot colorkey masking: color index bit layout matches
			// pc98emu/SPEC.md's VRAM scheme (B=bit0, R=bit1, G=bit2, E=bit3).
			// Skip (leave existing VRAM alone) any dot whose 4-plane color
			// equals colorkey_color.
			uint8_t keep_mask = 0;
			for(int bit = 0; bit < 8; bit++) {
				uint8_t dot_mask = static_cast<uint8_t>(0x80 >> bit);
				uint8_t color = 0;
				if(bb & dot_mask) color |= 1;
				if(rr & dot_mask) color |= 2;
				if(gg & dot_mask) color |= 4;
				if(ee & dot_mask) color |= 8;
				if(color != colorkey_color) {
					keep_mask |= dot_mask;
				}
			}
			VRAM_PLANE_B[dst_off] = static_cast<uint8_t>(
				(VRAM_PLANE_B[dst_off] & ~keep_mask) | (bb & keep_mask));
			VRAM_PLANE_R[dst_off] = static_cast<uint8_t>(
				(VRAM_PLANE_R[dst_off] & ~keep_mask) | (rr & keep_mask));
			VRAM_PLANE_G[dst_off] = static_cast<uint8_t>(
				(VRAM_PLANE_G[dst_off] & ~keep_mask) | (gg & keep_mask));
			VRAM_PLANE_E[dst_off] = static_cast<uint8_t>(
				(VRAM_PLANE_E[dst_off] & ~keep_mask) | (ee & keep_mask));
		}
	}

	return 0;
}
