// tests/text_test
// ----------------
// Unit + golden test for pc98emu/text: writes a known deterministic string
// into TRAM (halfwidth "AB" + one fullwidth kanji glyph, plus a
// reverse-video block to exercise ATRB_REVERSE), renders it via
// pc98emu_render(), and checks it two ways:
//
//   1. Pointwise assertions against hand-derived expected pixels for a few
//      dots of the 'A' glyph and the reverse-video block, so a failure here
//      points at exactly what went wrong.
//   2. A full-frame byte-exact comparison against a checked-in PPM fixture
//      (same "golden" pattern as tests/test_render_golden.cpp).
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

constexpr uint8_t ATRB_WHITE = ATRB_DISP | ATRB_RED | ATRB_GREEN | ATRB_BLUE;

void render_scene() {
	pc98emu_init(); // black background: zeroed VRAM + palette

	pc98emu_text_put_halfwidth(0, 0, 'A', ATRB_WHITE);
	pc98emu_text_put_halfwidth(1, 0, 'B', ATRB_WHITE);
	pc98emu_text_put_fullwidth(2, 0, 0x3021, ATRB_WHITE); // JIS row 16 col 1, "亜"

	// Reverse-video halfwidth space: since the space glyph is all-blank,
	// this paints a solid ATRB_RED block over the whole 8x16 cell.
	pc98emu_text_put_halfwidth(0, 1, ' ', ATRB_DISP | ATRB_RED | ATRB_REVERSE);
}

bool expect_pixel(const uint32_t *fb, int x, int y, uint32_t expected, const char *what) {
	uint32_t got = fb[y * RES_X + x];
	if(got != expected) {
		std::fprintf(stderr, "%s: pixel (%d,%d) = 0x%08X, expected 0x%08X\n", what, x, y, got, expected);
		return false;
	}
	return true;
}

} // namespace

int main(int argc, char **argv) {
	bool generate = (argc > 1 && std::strcmp(argv[1], "--generate") == 0);

	render_scene();

	static uint32_t framebuffer[RES_X * RES_Y];
	pc98emu_render(framebuffer);

	constexpr uint32_t kWhite = 0xFFFFFFFF;
	constexpr uint32_t kBlack = 0x000000FF;
	constexpr uint32_t kRed = 0xFF0000FF;

	bool ok = true;

	// 'A' glyph row 1 is 0x10 (binary 00010000): only column 3 (of the 8
	// dots making up this halfwidth cell at TRAM column 0) is set.
	ok &= expect_pixel(framebuffer, 3, 1, kWhite, "'A' glyph dot");
	ok &= expect_pixel(framebuffer, 0, 1, kBlack, "'A' glyph background dot (left)");
	ok &= expect_pixel(framebuffer, 7, 1, kBlack, "'A' glyph background dot (right)");

	// Far from any glyph: still the black background.
	ok &= expect_pixel(framebuffer, 400, 200, kBlack, "untouched background");

	// Reverse-video space at TRAM (0,1) -> pixel rows [16,32), cols [0,8):
	// every dot in the cell should be solid red.
	ok &= expect_pixel(framebuffer, 0, 16, kRed, "reverse-video block corner");
	ok &= expect_pixel(framebuffer, 7, 31, kRed, "reverse-video block corner");

	if(!ok) {
		return 1;
	}

	ppm::Image img = ppm::from_rgba8888(RES_X, RES_Y, framebuffer);
	std::string golden_path = std::string(PORT_TESTS_GOLDEN_DIR) + "/text_render.ppm";
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
		std::fprintf(stderr, "text render mismatch vs '%s': %s at (%d,%d) channel %d: got %d expected %d\n",
			golden_path.c_str(), diff.reason.c_str(), diff.x, diff.y, diff.channel, diff.a, diff.b);
		return 1;
	}

	std::printf("pc98emu_text_test: OK\n");
	return 0;
}
