// port/shim/egc_copy_shim
// -------------------------
// platform/x86real/pc98/egc.hpp's EGCCopy class -- used directly by
// th01/main/boss/b20m.cpp's sariel_entrance() and b20j.cpp's
// konngara_load_and_entrance() -- backed by pc98emu's own EGC model
// (port/pc98emu/egc.{hpp,cpp}, T7 egc-full) instead of the original
// implementation (platform/x86real/pc98/egc.cpp), which reconstructs raw
// segment:offset addressing via _ES/_SI/_DI pseudoregisters and `__es`
// far-pointer casts -- real 16-bit x86 segment arithmetic with no direct
// host equivalent. Same rationale as port/shim/egc_th01_shim.cpp's
// egc_copy_rect_1_to_0_16() (T8), just for the EGCCopy class API instead of
// th01/hardware/egc.h's free functions; pc98emu/egc.hpp's own comments
// note it already mirrors EGCCopy::rect()/rect_interpage() line-for-line.
#include "port/compat/borland.hpp"

#include "platform/x86real/pc98/egc.hpp"
#include "pc98emu/egc.hpp"

EGCCopy::EGCCopy() {
	pc98emu_egc_activate();
}

EGCCopy::~EGCCopy() {
	pc98emu_egc_deactivate();
}

void EGCCopy::rect(
	screen_x_t src_left, vram_y_t src_top, screen_x_t dst_left,
	vram_y_t dst_top, pixel_t w, pixel_t h
) {
	egc_copy_rect(src_left, src_top, dst_left, dst_top, w, h);
}

void EGCCopy::rect_interpage(
	screen_x_t src_left, vram_y_t src_top, screen_x_t dst_left,
	vram_y_t dst_top, pixel_t w, pixel_t h, page_t src_page
) {
	egc_copy_rect_interpage(
		src_left, src_top, dst_left, dst_top, w, h, static_cast<int>(src_page)
	);
}

void EGCCopy::rect_interpage(
	screen_x_t left, vram_y_t top, pixel_t w, pixel_t h, page_t src_page
) {
	egc_copy_rect_interpage_same_pos(left, top, w, h, static_cast<int>(src_page));
}
