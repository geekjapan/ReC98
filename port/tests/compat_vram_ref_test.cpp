// port/tests
// ----------
// Acceptance test for the REC98_PORT compat layer (compat/borland.hpp +
// compat/vram_ref.hpp + the #ifdef REC98_PORT additions to platform.h /
// planar.h): builds a TU that includes the *real* ReC98 pc98.h + planar.h
// headers under -DREC98_PORT and drives VRAM_PUT()/grcg_put()/VRAM_SNAP()
// through to pc98emu state, checking the results against what
// grcg_vram_write()/grcg_vram_read() would do directly.
//
// compat/borland.hpp must be included before any ReC98 header -- it defines
// far/near/pascal, MK_FP()/peekb()/pokeb(), and the pseudo registers that
// those headers assume already exist.
#include "compat/borland.hpp"

#include "pc98.h"
#include "planar.h"

#include <cstdio>
#include <cstdlib>

using namespace pc98emu;

namespace {

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		std::exit(1);
	}
}

} // namespace

int main() {
	pc98emu_init();

	// GRCG off: VRAM_PUT(B, ...) is a plain write to plane B only.
	VRAM_PUT(B, 0, 0xAB, 8);
	expect(VRAM_PLANE_B[0] == 0xAB, "VRAM_PUT plain write hits plane B");
	expect(VRAM_PLANE_R[0] == 0x00, "VRAM_PUT plain write leaves plane R untouched");

	// grcg_put() is VRAM_PUT(B, ...) under the hood -- must behave the same.
	grcg_put(2, 0xCD, 8);
	expect(VRAM_PLANE_B[2] == 0xCD, "grcg_put plain write (GRCG off)");

	// GRCG RMW: VRAM_PUT(B, ...) must fan out to all four non-excluded
	// planes via each plane's tile register, exactly like a direct
	// grcg_vram_write() call -- VRAM_CHUNK and grcg_put() share one code
	// path (VramRef), so this also covers grcg_put().
	pc98emu_outportb(0x7C, GC_RMW); // mode=RMW, plane_mask=0 (nothing excluded)
	pc98emu_outportb(0x7E, 0x11); // tile B
	pc98emu_outportb(0x7E, 0x22); // tile R
	pc98emu_outportb(0x7E, 0x33); // tile G
	pc98emu_outportb(0x7E, 0x44); // tile E

	VRAM_PLANE_B[10] = 0x0F;
	VRAM_PLANE_R[10] = 0x0F;
	VRAM_PLANE_G[10] = 0x0F;
	VRAM_PLANE_E[10] = 0x0F;

	VRAM_PUT(B, 10, 0xF0, 8); // src's 1-bits select the tile-sourced dots

	expect(VRAM_PLANE_B[10] == static_cast<uint8_t>((0x0F & ~0xF0) | (0x11 & 0xF0)), "RMW fan-out plane B");
	expect(VRAM_PLANE_R[10] == static_cast<uint8_t>((0x0F & ~0xF0) | (0x22 & 0xF0)), "RMW fan-out plane R");
	expect(VRAM_PLANE_G[10] == static_cast<uint8_t>((0x0F & ~0xF0) | (0x33 & 0xF0)), "RMW fan-out plane G");
	expect(VRAM_PLANE_E[10] == static_cast<uint8_t>((0x0F & ~0xF0) | (0x44 & 0xF0)), "RMW fan-out plane E");

	pc98emu_outportb(0x7C, GC_OFF);

	// VRAM_SNAP (VRAM_CHUNK read path) round-trips what was just written.
	dots8_t snapped;
	VRAM_SNAP(snapped, B, 2, 8);
	expect(snapped == 0xCD, "VRAM_SNAP read-back");

	// peekb()/MK_FP() resolve SEG_PLANE_B through the same pc98emu buffer.
	expect(peekb(SEG_PLANE_B, 2) == 0xCD, "peekb() resolves SEG_PLANE_B to pc98emu VRAM");

	std::printf("compat_vram_ref_test: OK\n");
	return 0;
}
