// TH01 port -- milestone M0 demo.
//
// Draws a static test scene entirely through the emulated PC-98 hardware
// entry points (VRAM_PLANE pointers, pc98emu_outportb()/grcg_vram_write())
// and presents it via SDL3. Never pokes pc98emu_planes[] directly.

#include "pc98emu/pc98emu.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

using namespace pc98emu;

namespace {

// pc98emu_render() packs each output pixel as
// (R << 24) | (G << 16) | (B << 8) | A -- exactly SDL_PIXELFORMAT_RGBA8888's
// bit layout (bits 31-24 = R, ..., bits 7-0 = A). Keep this texture format
// in sync with vram.cpp's pc98emu_render().
constexpr SDL_PixelFormat kTextureFormat = SDL_PIXELFORMAT_RGBA8888;

void setup_palette() {
	// Approximate PC-98 default 16-color EGA-like palette. Components are
	// 4-bit (0-15).
	pc98emu_palette_set(0, 0, 0, 0);   // black
	pc98emu_palette_set(1, 0, 0, 8);   // blue
	pc98emu_palette_set(2, 8, 0, 0);   // red
	pc98emu_palette_set(3, 8, 0, 8);   // magenta
	pc98emu_palette_set(4, 0, 8, 0);   // green
	pc98emu_palette_set(5, 0, 8, 8);   // cyan
	pc98emu_palette_set(6, 8, 8, 0);   // yellow
	pc98emu_palette_set(7, 8, 8, 8);   // white (dim)
	pc98emu_palette_set(8, 4, 4, 4);   // bright black (gray)
	pc98emu_palette_set(9, 0, 0, 15);  // bright blue
	pc98emu_palette_set(10, 15, 0, 0); // bright red
	pc98emu_palette_set(11, 15, 0, 15); // bright magenta
	pc98emu_palette_set(12, 0, 15, 0); // bright green
	pc98emu_palette_set(13, 0, 15, 15); // bright cyan
	pc98emu_palette_set(14, 15, 15, 0); // bright yellow
	pc98emu_palette_set(15, 15, 15, 15); // bright white
}

// Draws 16 horizontal gradient bars (one per color index) spanning
// [y_top, y_top+bar_h*16) via direct plane writes through VRAM_PLANE[].
void draw_gradient_bars(int y_top, int bar_h) {
	for(int color = 0; color < COLOR_COUNT; color++) {
		int y0 = y_top + color * bar_h;
		for(int y = y0; y < y0 + bar_h; y++) {
			for(int byte_x = 0; byte_x < ROW_SIZE; byte_x++) {
				int off = y * ROW_SIZE + byte_x;
				// Full byte (8 dots) set to a uniform color: bit0->B,
				// bit1->R, bit2->G, bit3->E, expanded across the whole byte.
				VRAM_PLANE_B[off] = (color & 0x1) ? 0xFF : 0x00;
				VRAM_PLANE_R[off] = (color & 0x2) ? 0xFF : 0x00;
				VRAM_PLANE_G[off] = (color & 0x4) ? 0xFF : 0x00;
				VRAM_PLANE_E[off] = (color & 0x8) ? 0xFF : 0x00;
			}
		}
	}
}

// Draws a filled rectangle in color index 11 using the emulated GRCG in
// RMW mode, exactly mirroring GRCG::set_color_static()'s bit expansion.
void draw_grcg_rect(int x_byte0, int y0, int w_bytes, int h, int color) {
	pc98emu_outportb(0x7C, GC_RMW); // mode=RMW, plane_mask=0 (no exclusion)

	// GRCG::set_color_static(): tile registers cycle B, R, G, E.
	pc98emu_outportb(0x7E, (color & 0x1) ? 0xFF : 0x00); // B
	pc98emu_outportb(0x7E, (color & 0x2) ? 0xFF : 0x00); // R
	pc98emu_outportb(0x7E, (color & 0x4) ? 0xFF : 0x00); // G
	pc98emu_outportb(0x7E, (color & 0x8) ? 0xFF : 0x00); // E

	for(int y = y0; y < y0 + h; y++) {
		for(int bx = x_byte0; bx < x_byte0 + w_bytes; bx++) {
			int off = y * ROW_SIZE + bx;
			// 0xFF = full byte (all 8 dots) become `color`; plane_hint is
			// unused in RMW mode.
			grcg_vram_write(0, static_cast<uint16_t>(off), 0xFF, 8);
		}
	}

	pc98emu_outportb(0x7C, GC_OFF); // turn GRCG off
}

// Draws a diagonal line of single-bit dots in color index 10 via direct
// pointer read-modify-write (GRCG is off here, so this is the documented
// direct path -- ORing a single bit into the existing byte rather than
// overwriting it wholesale).
void draw_diagonal_line(int x0, int y0, int length, int color) {
	for(int i = 0; i < length; i++) {
		int x = x0 + i;
		int y = y0 + i;
		if(x < 0 || x >= RES_X || y < 0 || y >= RES_Y) {
			continue;
		}
		int off = y * ROW_SIZE + (x / 8);
		int bit = 7 - (x % 8); // bit7 = leftmost dot in the byte
		uint8_t mask = static_cast<uint8_t>(1 << bit);

		if(color & 0x1) VRAM_PLANE_B[off] |= mask;
		if(color & 0x2) VRAM_PLANE_R[off] |= mask;
		if(color & 0x4) VRAM_PLANE_G[off] |= mask;
		if(color & 0x8) VRAM_PLANE_E[off] |= mask;
	}
}

} // namespace

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;

	if(!SDL_Init(SDL_INIT_VIDEO)) {
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_Window *window = SDL_CreateWindow("TH01 Port -- M0 Demo", 1280, 800, 0);
	if(!window) {
		std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return 1;
	}

	SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
	if(!renderer) {
		std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	SDL_SetRenderLogicalPresentation(renderer, RES_X, RES_Y, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	if(!SDL_SetRenderVSync(renderer, 1)) {
		std::fprintf(stderr, "SDL_SetRenderVSync failed (falling back to SDL_Delay): %s\n", SDL_GetError());
	}

	SDL_Texture *texture = SDL_CreateTexture(renderer, kTextureFormat, SDL_TEXTUREACCESS_STREAMING, RES_X, RES_Y);
	if(!texture) {
		std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	pc98emu_init();
	setup_palette();

	// Static test scene, drawn once.
	draw_gradient_bars(0, 20); // rows 0..319, 16 bars of 20px
	draw_grcg_rect(10, 330, 20, 40, 11); // 160x40px filled rect, color 11
	draw_diagonal_line(0, 340, 400, 10); // diagonal, color 10

	static uint32_t framebuffer[RES_X * RES_Y];

	bool running = true;
	while(running) {
		SDL_Event event;
		while(SDL_PollEvent(&event)) {
			if(event.type == SDL_EVENT_QUIT) {
				running = false;
			} else if(event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
				running = false;
			}
		}

		pc98emu_render(framebuffer);
		SDL_UpdateTexture(texture, nullptr, framebuffer, RES_X * static_cast<int>(sizeof(uint32_t)));

		SDL_RenderClear(renderer);
		SDL_RenderTexture(renderer, texture, nullptr, nullptr);
		SDL_RenderPresent(renderer);
	}

	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
