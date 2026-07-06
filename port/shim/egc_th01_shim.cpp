// port/shim/egc_th01_shim.cpp
// -----------------------------------------------------------------------
// th01/hardware/egc.h's egc_copy_rect_1_to_0_16() (VRAM page 1 -> page 0,
// same position, 16-dot-aligned rectangle copy) called by th01/formats/
// ptn.hpp's ptn_sloppy_unput_16()/ptn_sloppy_unput_quarter_16() macros and
// grc.hpp's grc_sloppy_unput() macro. th01/hardware/egc.cpp itself (the
// original ZUN implementation) is out of scope for th01/formats (T8); this
// is a real, not fake, implementation built on top of pc98emu's own EGC
// model (port/pc98emu/egc.{hpp,cpp}, T7 egc-full), which already provides
// exactly this operation as egc_copy_rect_interpage_same_pos(..., src_page).

#include "port/compat/borland.hpp"

#include "th01/hardware/egc.h"
#include "pc98emu/egc.hpp"

void egc_copy_rect_1_to_0_16(
	screen_x_t left, vram_y_t top, pixel_t w, pixel_t h
) {
	egc_copy_rect_interpage_same_pos(left, top, w, h, /*src_page=*/1);
}
