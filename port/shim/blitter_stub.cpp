// port/shim/blitter_stub
// ------------------------
// platform/x86real/pc98/blitter.hpp's optimized byte-aligned sprite blitter
// (Blitter::write/write_offscreen/snap, blitter_init_clip(), the
// BLITTER_FUNCS lookup table) is built entirely from raw pseudoregister/
// segment arithmetic (_AX/_BX/_SI/_DI/_ES/_DS, `__es`-qualified pointer
// casts reconstructing real 16-bit segment:offset addressing) with no
// direct host equivalent -- unlike the GRCG/EGC hardware models, this one
// hasn't been reimplemented in pc98emu yet.
//
// th01/main/bullet/pellet.cpp's only two call sites (blitter_init_clip(),
// blit_source field writes) are both nullptr-safe: `const Blitter *b =
// blitter_init_clip(...); if(b && ...) { ...; b->write(...); }`. Returning
// nullptr unconditionally here is honest about that gap (a real
// implementation is future work, not attempted) while still satisfying the
// "no assets" bring-up's build+link+run requirement: the only visible
// effect is that this one pellet-decay sprite blit is always skipped,
// which cannot be observed without the original game's data files anyway.
#include "port/compat/borland.hpp"

#include "platform/x86real/pc98/blitter.hpp"

blit_source_t blit_source;

const Blitter __ds *__fastcall blitter_init_clip(vram_x_t, vram_y_t) {
	return nullptr;
}
