#include "port/app/host.hpp"

#include "pc98emu/pc98emu.hpp"
#include "port/shim/input.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

namespace port {
namespace {

// pc98emu_render() packs each output pixel as (R<<24)|(G<<16)|(B<<8)|A --
// exactly SDL_PIXELFORMAT_RGBA8888's bit layout. Keep in sync with
// pc98emu/vram.cpp's pc98emu_render().
constexpr SDL_PixelFormat kTextureFormat = SDL_PIXELFORMAT_RGBA8888;

SDL_Window *g_window = nullptr;
SDL_Renderer *g_renderer = nullptr;
SDL_Texture *g_texture = nullptr;
uint32_t g_framebuffer[pc98emu::RES_X * pc98emu::RES_Y];

// TH01_DUMP_FRAME=<path-prefix>: headless pixel verification support. When
// set, every 30th presented frame is written out as "<prefix><frame>.ppm"
// (binary PPM, RGB, no alpha) so a test can decode the title screen without
// a display.
bool g_dump_frame_checked = false;
const char *g_dump_frame_prefix = nullptr;
int g_dump_frame_counter = 0;

void dump_frame_if_requested() {
	if(!g_dump_frame_checked) {
		g_dump_frame_checked = true;
		g_dump_frame_prefix = std::getenv("TH01_DUMP_FRAME");
	}
	if(g_dump_frame_prefix == nullptr) {
		return;
	}
	if((g_dump_frame_counter % 30) == 0) {
		char path[1024];
		std::snprintf(
			path, sizeof(path), "%s%06d.ppm", g_dump_frame_prefix,
			g_dump_frame_counter
		);
		if(FILE *f = std::fopen(path, "wb")) {
			std::fprintf(
				f, "P6\n%d %d\n255\n", pc98emu::RES_X, pc98emu::RES_Y
			);
			for(int i = 0; i < pc98emu::RES_X * pc98emu::RES_Y; i++) {
				uint32_t px = g_framebuffer[i];
				uint8_t rgb[3] = {
					static_cast<uint8_t>((px >> 24) & 0xFF),
					static_cast<uint8_t>((px >> 16) & 0xFF),
					static_cast<uint8_t>((px >> 8) & 0xFF),
				};
				std::fwrite(rgb, 1, 3, f);
			}
			std::fclose(f);
		}
	}
	g_dump_frame_counter++;
}

} // namespace

bool host_init(const char *window_title) {
	if(!SDL_Init(SDL_INIT_VIDEO)) {
		std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}

	g_window = SDL_CreateWindow(window_title, 1280, 800, 0);
	if(!g_window) {
		std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}

	g_renderer = SDL_CreateRenderer(g_window, nullptr);
	if(!g_renderer) {
		std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(g_window);
		g_window = nullptr;
		SDL_Quit();
		return false;
	}

	SDL_SetRenderLogicalPresentation(
		g_renderer, pc98emu::RES_X, pc98emu::RES_Y, SDL_LOGICAL_PRESENTATION_LETTERBOX
	);
	if(!SDL_SetRenderVSync(g_renderer, 1)) {
		std::fprintf(
			stderr, "SDL_SetRenderVSync failed (falling back to unthrottled "
			"presentation): %s\n", SDL_GetError()
		);
	}

	g_texture = SDL_CreateTexture(
		g_renderer, kTextureFormat, SDL_TEXTUREACCESS_STREAMING,
		pc98emu::RES_X, pc98emu::RES_Y
	);
	if(!g_texture) {
		std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
		SDL_DestroyRenderer(g_renderer);
		SDL_DestroyWindow(g_window);
		g_renderer = nullptr;
		g_window = nullptr;
		SDL_Quit();
		return false;
	}

	return true;
}

void host_shutdown() {
	if(g_texture) {
		SDL_DestroyTexture(g_texture);
		g_texture = nullptr;
	}
	if(g_renderer) {
		SDL_DestroyRenderer(g_renderer);
		g_renderer = nullptr;
	}
	if(g_window) {
		SDL_DestroyWindow(g_window);
		g_window = nullptr;
	}
	SDL_Quit();
}

void host_present_frame() {
	SDL_Event event;
	while(SDL_PollEvent(&event)) {
		if(event.type == SDL_EVENT_QUIT) {
			host_shutdown();
			std::exit(0);
		}
	}

	input_poll();

	pc98emu_render(g_framebuffer);
	dump_frame_if_requested();
	SDL_UpdateTexture(
		g_texture, nullptr, g_framebuffer,
		pc98emu::RES_X * static_cast<int>(sizeof(uint32_t))
	);
	SDL_RenderClear(g_renderer);
	SDL_RenderTexture(g_renderer, g_texture, nullptr, nullptr);
	SDL_RenderPresent(g_renderer);
}

} // namespace port
