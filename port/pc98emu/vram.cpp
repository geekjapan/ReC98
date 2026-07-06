#include "pc98emu/vram.hpp"
#include "pc98emu/text.hpp"

#include <cstring>

uint8_t pc98emu_planes[2][4][32000];
uint8_t *VRAM_PLANE[4];
uint8_t *VRAM_PLANE_B;
uint8_t *VRAM_PLANE_R;
uint8_t *VRAM_PLANE_G;
uint8_t *VRAM_PLANE_E;

uint8_t pc98emu_pal[16][3];

namespace {

int g_accessible_page = 0;
int g_shown_page = 0;

void repoint_accessible(int page) {
	VRAM_PLANE[pc98emu::PLANE_B] = pc98emu_planes[page][pc98emu::PLANE_B];
	VRAM_PLANE[pc98emu::PLANE_R] = pc98emu_planes[page][pc98emu::PLANE_R];
	VRAM_PLANE[pc98emu::PLANE_G] = pc98emu_planes[page][pc98emu::PLANE_G];
	VRAM_PLANE[pc98emu::PLANE_E] = pc98emu_planes[page][pc98emu::PLANE_E];
	VRAM_PLANE_B = VRAM_PLANE[pc98emu::PLANE_B];
	VRAM_PLANE_R = VRAM_PLANE[pc98emu::PLANE_R];
	VRAM_PLANE_G = VRAM_PLANE[pc98emu::PLANE_G];
	VRAM_PLANE_E = VRAM_PLANE[pc98emu::PLANE_E];
}

} // namespace

void pc98emu_palette_set(int index, uint8_t r4, uint8_t g4, uint8_t b4) {
	pc98emu_pal[index][0] = r4 & 0x0F;
	pc98emu_pal[index][1] = g4 & 0x0F;
	pc98emu_pal[index][2] = b4 & 0x0F;
}

void pc98emu_page_set_accessible(int page) {
	g_accessible_page = page & 1;
	repoint_accessible(g_accessible_page);
}

void pc98emu_page_set_shown(int page) {
	g_shown_page = page & 1;
}

int pc98emu_page_get_accessible() {
	return g_accessible_page;
}

int pc98emu_page_get_shown() {
	return g_shown_page;
}

void pc98emu_vram_reset() {
	std::memset(pc98emu_planes, 0, sizeof(pc98emu_planes));
	std::memset(pc98emu_pal, 0, sizeof(pc98emu_pal));
	g_accessible_page = 0;
	g_shown_page = 0;
	repoint_accessible(0);
}

void pc98emu_render(uint32_t *out_rgba) {
	using namespace pc98emu;

	const uint8_t (&shown)[4][32000] = pc98emu_planes[g_shown_page];

	for(int y = 0; y < RES_Y; y++) {
		int row_off = y * ROW_SIZE;
		for(int x = 0; x < RES_X; x++) {
			int byte_off = row_off + (x / 8);
			int bit = 7 - (x % 8); // bit7 = leftmost dot in the byte

			int index =
				(((shown[PLANE_B][byte_off] >> bit) & 1) << 0) |
				(((shown[PLANE_R][byte_off] >> bit) & 1) << 1) |
				(((shown[PLANE_G][byte_off] >> bit) & 1) << 2) |
				(((shown[PLANE_E][byte_off] >> bit) & 1) << 3);

			uint8_t r = pc98emu_pal[index][0] * 17;
			uint8_t g = pc98emu_pal[index][1] * 17;
			uint8_t b = pc98emu_pal[index][2] * 17;
			uint8_t a = 0xFF;

			out_rgba[y * RES_X + x] =
				(static_cast<uint32_t>(r) << 24) |
				(static_cast<uint32_t>(g) << 16) |
				(static_cast<uint32_t>(b) << 8) |
				a;
		}
	}

	// Text overlay: real PC-98 hardware mixes the digital text plane over
	// the analog graphics output at the very end of the pipeline, so this
	// runs after (and on top of) the graphics compositing above.
	pc98emu_text_composite(out_rgba);
}
