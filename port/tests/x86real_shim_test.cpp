// Unit test for port/compat/x86real_shim: the int86()/outportb()/inportb()
// definitions original ReC98 code (th01/op_01.cpp's keygroup_sense(),
// th01/hardware/graph.cpp's grcg_setcolor_rmw(), ...) calls directly.

#include <array>
#include <cstdio>

#include <SDL3/SDL.h>

// x86real.h needs far/near/interrupt/etc predefined (normally via `-include`
// on REC98_PORT compile targets, see port/CMakeLists.txt's th01op_core) --
// pull compat/borland.hpp in explicitly here instead, since this test
// target doesn't otherwise compile any original ReC98 sources.
#include "port/compat/borland.hpp"

#include "pc98emu/grcg.hpp"
#include "pc98emu/pc98emu.hpp"
#include "platform/x86real/pc98/keyboard.hpp"
#include "port/shim/input.hpp"
#include "x86real.h"

using pc98emu::GC_OFF;
using pc98emu::GC_RMW;

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

} // namespace

int main() {
	// int86(0x18, ah=0x04, al=group) must dispatch to key_sense(group) and
	// return the bitmap in out.h.ah, exactly like op_01.cpp's
	// keygroup_sense()/ok_shot_cancel_sense() expect.
	std::array<bool, SDL_SCANCODE_COUNT> keys{};
	keys[SDL_SCANCODE_RETURN] = true;
	port::input_test_set_keys(keys.data());

	REGS in{};
	REGS out{};
	in.h.ah = 0x04;
	in.h.al = 3; // keygroup 3 (OK/Return)
	int86(0x18, &in, &out);
	expect(out.h.ah == K3_RETURN, "int86(0x18, ah=0x04) senses keygroup 3");

	port::input_test_set_keys(nullptr);

	// ah=0x03 (keyboard init, op_01.cpp's main_op() startup) must not crash
	// and should report success (AH=0 -- ignored by the caller anyway).
	REGS in2{};
	REGS out2{};
	in2.h.ah = 0x03;
	int86(0x18, &in2, &out2);
	expect(out2.h.ah == 0, "int86(0x18, ah=0x03) keyboard init no-ops cleanly");

	// outportb(0x7C/0x7E, ...) (grcg_setcolor_rmw()/GRCG's RMW mode set) must
	// reach pc98emu's GRCG emulation, not just silently vanish.
	pc98emu_init();
	outportb(0x7C, GC_RMW); // mode=RMW, plane_mask=0
	outportb(0x7E, 0xFF); // tile B
	outportb(0x7E, 0x00); // tile R
	outportb(0x7E, 0x00); // tile G
	outportb(0x7E, 0x00); // tile E
	grcg_vram_write(0, 0, 0xFF, 8); // full-mask RMW write at offset 0
	expect(
		grcg_vram_read(0, 0, 8) == 0xFF,
		"outportb(0x7C/0x7E) GRCG RMW tile write reaches plane B"
	);
	expect(
		grcg_vram_read(1, 0, 8) == 0x00,
		"outportb(0x7C/0x7E) GRCG RMW tile write leaves plane R untouched"
	);
	outportb(0x7C, GC_OFF);

	if(g_failures == 0) {
		std::printf("x86real_shim_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
