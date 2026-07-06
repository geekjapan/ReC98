#include "platform.h"
#include "x86real.h"
#include "planar.h"
#include "platform/x86real/pc98/grcg.hpp"

#ifdef REC98_PORT
// REC98_PORT: the original body (below) relies on Borland's __fastcall
// convention passing `col` in the AL register and then reading it back via
// the `_AL` pseudo-register global, rather than via the (unnamed) parameter
// -- meaningless once __fastcall is emptied out for a flat, single-calling-
// convention host build (the parameter would just be read normally, and
// `_AL` would hold whatever thread_local garbage was last written to it).
// tile_register_from_carry() has the same problem one level down (relies on
// the x86 carry flag from a real SHR instruction). Same semantics, no
// register/flag reliance.
void GRCG::set_color(vc_t col)
{
	outportb(0x7E, ((col & 0x1) ? 0xFF : 0x00));
	outportb(0x7E, ((col & 0x2) ? 0xFF : 0x00));
	outportb(0x7E, ((col & 0x4) ? 0xFF : 0x00));
	outportb(0x7E, ((col & 0x8) ? 0xFF : 0x00));
}
#else
void __fastcall GRCG::set_color(vc_t /* _AL */)
{
	// By shifting each successive bit of [col] into the carry flag and using
	// tile_register_from_carry(), we get the minimum of three instructions
	// (SHR, SBB, OUT) per bitplane, with no register spills. Quite a beautiful
	// optimization!
	_AH = _AL;
	outportb(0x7E, tile_register_from_carry(_AH >>= 1));
	outportb(0x7E, tile_register_from_carry(_AH >>= 1));
	outportb(0x7E, tile_register_from_carry(_AH >>= 1));
	outportb(0x7E, tile_register_from_carry(_AH >>= 1));
}
#endif
