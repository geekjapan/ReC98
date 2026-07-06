// pc98emu
// -------
// Software emulation of the PC-98 GRCG (Graphics Charger), reached in the
// original hardware via outportb(0x7C, ...) / outportb(0x7E, ...).

#ifndef PC98EMU_GRCG_HPP
#define PC98EMU_GRCG_HPP

#include <cstdint>

namespace pc98emu {

enum grcg_mode_t { GC_OFF = 0x00, GC_TDW = 0x80, GC_TCR = 0x80, GC_RMW = 0xC0 };

} // namespace pc98emu

// Emulates writes to the GRCG's I/O ports (0x7C: mode + plane mask, 0x7E:
// tile register, cycling B,R,G,E). Other ports are a no-op.
void pc98emu_outportb(uint16_t port, uint8_t value);

// Performs a GRCG-mediated VRAM write of `bit_count` bits (8/16/32) at byte
// `offset` in the accessible page, honoring the current mode/plane_mask/tile
// state. `src` holds bit_count/8 little-endian bytes (byte0 = src & 0xFF at
// offset, byte1 = (src >> 8) & 0xFF at offset+1, ...).
// `plane_hint` (0=B,1=R,2=G,3=E) only matters in GC_OFF mode, where it names
// the single plane being written directly (as if the GRCG weren't involved).
void grcg_vram_write(unsigned plane_hint, uint16_t offset, uint32_t src, int bit_count);

// Performs a GRCG-mediated VRAM read of `bit_count` bits (8/16/32) from
// plane `plane` (0=B,1=R,2=G,3=E) at byte `offset` in the accessible page,
// honoring the current mode. Reassembled little-endian.
uint32_t grcg_vram_read(unsigned plane, uint16_t offset, int bit_count);

#endif // PC98EMU_GRCG_HPP
