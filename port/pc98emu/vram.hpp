// pc98emu
// -------
// Software emulation of PC-98 planar VRAM: two pages, four bitplanes
// (B, R, G, E) of 32,000 bytes each, plus the 16-color palette and the
// pointer-based accessible-page API mirroring ReC98's VRAM_PLANE[] globals.

#ifndef PC98EMU_VRAM_HPP
#define PC98EMU_VRAM_HPP

#include <cstdint>

namespace pc98emu {

constexpr int RES_X = 640;
constexpr int RES_Y = 400;
constexpr int BYTE_DOTS = 8;
constexpr int ROW_SIZE = RES_X / BYTE_DOTS; // 80
constexpr int PLANE_SIZE = ROW_SIZE * RES_Y; // 32000
constexpr int PLANE_COUNT = 4;
constexpr int PAGE_COUNT = 2;
constexpr int COLOR_COUNT = 16;

enum plane_index_t { PLANE_B = 0, PLANE_R = 1, PLANE_G = 2, PLANE_E = 3 };

} // namespace pc98emu

// Two pages of four planes, 32000 bytes each. Exposed at namespace scope
// (not pc98emu::) so grcg.cpp / egc.cpp can reach it directly, same as the
// VRAM_PLANE pointers below.
extern uint8_t pc98emu_planes[2][4][32000];

// Mirrors ReC98's planar.h VRAM_PLANE / VRAM_PLANE_B/R/G/E globals, minus the
// `far` qualifier (flat address space on the host). These always point into
// pc98emu_planes[accessible_page][...] and are re-pointed by
// pc98emu_page_set_accessible().
extern uint8_t *VRAM_PLANE[4];
extern uint8_t *VRAM_PLANE_B;
extern uint8_t *VRAM_PLANE_R;
extern uint8_t *VRAM_PLANE_G;
extern uint8_t *VRAM_PLANE_E;

// 16-color palette, each entry storing 4-bit R, G, B components in [0,15].
// (Plane order is B,R,G,E, but the palette itself stores components r,g,b.)
extern uint8_t pc98emu_pal[16][3];

void pc98emu_palette_set(int index, uint8_t r4, uint8_t g4, uint8_t b4);

// Switches which page the VRAM_PLANE* pointers above address (the page CPU
// writes/reads go through) and which page pc98emu_render() reads from.
// Defaults to page 0 for both.
void pc98emu_page_set_accessible(int page);
void pc98emu_page_set_shown(int page);
int pc98emu_page_get_accessible();
int pc98emu_page_get_shown();

// Resets both VRAM pages (all planes) to zero and the palette to black, and
// re-points VRAM_PLANE* at page 0.
void pc98emu_vram_reset();

// Renders the SHOWN page's 640x400 planar image to `out_rgba`
// (640*400 uint32_t elements). Each element is packed as
// (R << 24) | (G << 16) | (B << 8) | A, matching SDL_PIXELFORMAT_RGBA8888's
// bit layout as used by port/demo/main.cpp.
void pc98emu_render(uint32_t *out_rgba);

#endif // PC98EMU_VRAM_HPP
