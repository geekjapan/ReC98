#include "pc98emu/grcg.hpp"
#include "pc98emu/vram.hpp"

namespace {

uint8_t g_mode = pc98emu::GC_OFF;
uint8_t g_plane_mask = 0; // bit0=B..bit3=E, set = plane excluded from RMW/TDW
uint8_t g_tile[4] = {0, 0, 0, 0}; // B, R, G, E
int g_tile_rotation = 0;

uint8_t byte_of(uint32_t src, int i) {
	return static_cast<uint8_t>((src >> (i * 8)) & 0xFF);
}

} // namespace

void pc98emu_outportb(uint16_t port, uint8_t value) {
	switch(port) {
	case 0x7C:
		g_mode = value & 0xC0;
		g_plane_mask = value & 0x0F;
		g_tile_rotation = 0;
		break;
	case 0x7E:
		g_tile[g_tile_rotation] = value;
		g_tile_rotation = (g_tile_rotation + 1) % 4;
		break;
	default:
		break; // no-op: not a GRCG port
	}
}

void grcg_vram_write(unsigned plane_hint, uint16_t offset, uint32_t src, int bit_count) {
	int page = pc98emu_page_get_accessible();
	int byte_count = bit_count / 8;

	if(g_mode == pc98emu::GC_RMW) {
		for(int p = 0; p < 4; p++) {
			if(g_plane_mask & (1 << p)) {
				continue; // excluded plane
			}
			uint8_t *plane = pc98emu_planes[page][p];
			for(int i = 0; i < byte_count; i++) {
				uint8_t byte = byte_of(src, i);
				plane[offset + i] = (plane[offset + i] & ~byte) | (g_tile[p] & byte);
			}
		}
	} else if(g_mode == pc98emu::GC_TDW) {
		for(int p = 0; p < 4; p++) {
			if(g_plane_mask & (1 << p)) {
				continue; // excluded plane
			}
			uint8_t *plane = pc98emu_planes[page][p];
			for(int i = 0; i < byte_count; i++) {
				plane[offset + i] = g_tile[p]; // src ignored as data
			}
		}
	} else { // GC_OFF: plain direct write of the single named plane
		uint8_t *plane = pc98emu_planes[page][plane_hint];
		for(int i = 0; i < byte_count; i++) {
			plane[offset + i] = byte_of(src, i);
		}
	}
}

uint32_t grcg_vram_read(unsigned plane, uint16_t offset, int bit_count) {
	int page = pc98emu_page_get_accessible();
	int byte_count = bit_count / 8;
	uint32_t result = 0;

	if(g_mode == pc98emu::GC_TDW) {
		// GC_TDW == GC_TCR (0x80): treat as tile-compare-read from plane B.
		// TODO: implement real TCR compare-read semantics
		const uint8_t *plane_b = pc98emu_planes[page][pc98emu::PLANE_B];
		for(int i = 0; i < byte_count; i++) {
			result |= static_cast<uint32_t>(plane_b[offset + i]) << (i * 8);
		}
	} else if(g_mode == pc98emu::GC_RMW) {
		// RMW isn't a normal documented read op.
		// TODO: fall back to plain read of the named plane for now.
		const uint8_t *p = pc98emu_planes[page][plane];
		for(int i = 0; i < byte_count; i++) {
			result |= static_cast<uint32_t>(p[offset + i]) << (i * 8);
		}
	} else { // GC_OFF: plain read of the named plane
		const uint8_t *p = pc98emu_planes[page][plane];
		for(int i = 0; i < byte_count; i++) {
			result |= static_cast<uint32_t>(p[offset + i]) << (i * 8);
		}
	}

	return result;
}
