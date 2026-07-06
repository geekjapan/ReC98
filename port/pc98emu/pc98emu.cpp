#include "pc98emu/pc98emu.hpp"

void pc98emu_init() {
	pc98emu_vram_reset();
	pc98emu::g_egc_state = {};
	pc98emu_egc_deactivate();
	// GRCG state resets itself the next time port 0x7C is written; there is
	// no separate "reset" entry point since real hardware has none either.
	pc98emu_outportb(0x7C, pc98emu::GC_OFF);
	pc98emu_text_reset();
}

void pc98emu_reset() {
	pc98emu_init();
}
