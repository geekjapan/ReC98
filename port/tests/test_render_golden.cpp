// tests/test_render_golden
// ------------------------
// Golden test for pc98emu_render(): draws a small deterministic scene
// through the emulated VRAM/GRCG hardware entry points (mirroring
// port/demo/main.cpp's approach), renders it, and compares the result
// byte-for-byte (tolerance 0) against a checked-in PPM fixture.
//
// Run with --generate to (re)write the fixture after an intentional
// rendering change; see port/README.md for the regeneration workflow.

#include "pc98emu/pc98emu.hpp"
#include "tests/ppm.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace pc98emu;

namespace {

#ifndef PORT_TESTS_GOLDEN_DIR
#define PORT_TESTS_GOLDEN_DIR "."
#endif

void setup_palette() {
	pc98emu_palette_set(0, 0, 0, 0);
	pc98emu_palette_set(10, 15, 0, 0);  // bright red
	pc98emu_palette_set(11, 15, 0, 15); // bright magenta
	pc98emu_palette_set(12, 0, 15, 0);  // bright green
}

// Direct plane writes: a solid green rectangle.
void draw_direct_rect(int x_byte0, int y0, int w_bytes, int h) {
	for(int y = y0; y < y0 + h; y++) {
		for(int bx = x_byte0; bx < x_byte0 + w_bytes; bx++) {
			int off = y * ROW_SIZE + bx;
			VRAM_PLANE_G[off] = 0xFF;
		}
	}
}

// GRCG RMW: a solid magenta rectangle, mirroring demo/main.cpp's approach.
void draw_grcg_rect(int x_byte0, int y0, int w_bytes, int h, int color) {
	pc98emu_outportb(0x7C, GC_RMW);
	pc98emu_outportb(0x7E, (color & 0x1) ? 0xFF : 0x00);
	pc98emu_outportb(0x7E, (color & 0x2) ? 0xFF : 0x00);
	pc98emu_outportb(0x7E, (color & 0x4) ? 0xFF : 0x00);
	pc98emu_outportb(0x7E, (color & 0x8) ? 0xFF : 0x00);

	for(int y = y0; y < y0 + h; y++) {
		for(int bx = x_byte0; bx < x_byte0 + w_bytes; bx++) {
			int off = y * ROW_SIZE + bx;
			grcg_vram_write(0, static_cast<uint16_t>(off), 0xFF, 8);
		}
	}

	pc98emu_outportb(0x7C, GC_OFF);
}

void render_scene() {
	pc98emu_init();
	setup_palette();
	draw_direct_rect(0, 0, 10, 20);       // green block, top-left
	draw_grcg_rect(20, 0, 10, 20, 11);    // magenta block via GRCG RMW
}

} // namespace

int main(int argc, char **argv) {
	bool generate = (argc > 1 && std::strcmp(argv[1], "--generate") == 0);
	std::string golden_path = std::string(PORT_TESTS_GOLDEN_DIR) + "/render_basic.ppm";

	render_scene();

	static uint32_t framebuffer[RES_X * RES_Y];
	pc98emu_render(framebuffer);
	ppm::Image img = ppm::from_rgba8888(RES_X, RES_Y, framebuffer);

	std::string err;

	if(generate) {
		if(!ppm::write(golden_path, img, &err)) {
			std::fprintf(stderr, "failed to write golden '%s': %s\n", golden_path.c_str(), err.c_str());
			return 1;
		}
		std::printf("wrote golden: %s\n", golden_path.c_str());
		return 0;
	}

	ppm::Image golden;
	if(!ppm::read(golden_path, &golden, &err)) {
		std::fprintf(stderr, "failed to read golden '%s': %s\n", golden_path.c_str(), err.c_str());
		std::fprintf(stderr, "(run this binary with --generate to create it)\n");
		return 1;
	}

	ppm::Diff diff;
	if(!ppm::compare(img, golden, 0, &diff)) {
		std::fprintf(stderr, "render mismatch vs '%s': %s at (%d,%d) channel %d: got %d expected %d\n",
			golden_path.c_str(), diff.reason.c_str(), diff.x, diff.y, diff.channel, diff.a, diff.b);
		return 1;
	}

	std::printf("test_render_golden: OK\n");
	return 0;
}
