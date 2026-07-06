// pc98emu
// -------
// Software emulation core for the PC-98 planar VRAM / GRCG / EGC hardware
// used by the original TH01 game code. Umbrella header: pulls in the
// individual VRAM, GRCG, and EGC emulation headers, plus init/reset.

#ifndef PC98EMU_PC98EMU_HPP
#define PC98EMU_PC98EMU_HPP

#include "pc98emu/vram.hpp"
#include "pc98emu/grcg.hpp"
#include "pc98emu/egc.hpp"
#include "pc98emu/text.hpp"

// Zeroes all VRAM (both pages, all planes), resets the palette to black,
// resets GRCG/EGC state, and sets both the accessible and shown page to 0.
void pc98emu_init();

// Equivalent to pc98emu_init() -- provided as a separate entry point for
// callers that want to re-initialize mid-run.
void pc98emu_reset();

#endif // PC98EMU_PC98EMU_HPP
