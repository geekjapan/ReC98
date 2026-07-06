// tests/test_ppm
// --------------
// Unit test for the ppm golden-test helper itself: write/read roundtrip,
// exact-match and tolerance behavior, and mismatch/size-mismatch detection.
// Exits 0 on success, 1 on the first failed check (message on stderr).

#include "tests/ppm.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond) \
	do { \
		if(!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			g_failures++; \
		} \
	} while(0)

uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
		(static_cast<uint32_t>(b) << 8) | a;
}

} // namespace

int main() {
	const int w = 4, h = 2;
	const uint32_t pixels[w * h] = {
		rgba(0, 0, 0, 0xFF),     rgba(255, 0, 0, 0xFF), rgba(0, 255, 0, 0xFF), rgba(0, 0, 255, 0xFF),
		rgba(17, 34, 51, 0x00),  rgba(255, 255, 255, 0xFF), rgba(1, 2, 3, 0xFF), rgba(254, 253, 252, 0xFF),
	};

	ppm::Image img = ppm::from_rgba8888(w, h, pixels);
	CHECK(img.w == w);
	CHECK(img.h == h);
	CHECK(img.rgb.size() == static_cast<size_t>(w) * h * 3);
	CHECK(img.rgb[0] == 0 && img.rgb[1] == 0 && img.rgb[2] == 0); // alpha dropped
	CHECK(img.rgb[3] == 255 && img.rgb[4] == 0 && img.rgb[5] == 0);

	const std::string path = "test_ppm_roundtrip.ppm";
	std::string err;

	CHECK(ppm::write(path, img, &err));

	ppm::Image reread;
	CHECK(ppm::read(path, &reread, &err));
	CHECK(reread.w == img.w);
	CHECK(reread.h == img.h);

	ppm::Diff diff;
	CHECK(ppm::compare(img, reread, 0, &diff));

	// Corrupt one channel and confirm exact-tolerance comparison catches it,
	// while a wide-enough tolerance lets it through.
	ppm::Image corrupted = reread;
	corrupted.rgb[19] = static_cast<uint8_t>(corrupted.rgb[19] + 3); // pixel 6 (1,2,3) G=2, no overflow
	CHECK(!ppm::compare(img, corrupted, 0, &diff));
	CHECK(diff.x == 2 && diff.y == 1); // pixel index 6 (byte 19 -> pixel 19/3=6), channel 1 (G)
	CHECK(diff.channel == 1);
	CHECK(ppm::compare(img, corrupted, 3, nullptr));
	CHECK(!ppm::compare(img, corrupted, 2, nullptr));

	// Size mismatch is reported distinctly and never crashes.
	ppm::Image wrong_size = ppm::from_rgba8888(w + 1, h, pixels);
	CHECK(!ppm::compare(img, wrong_size, 255, &diff));
	CHECK(!diff.reason.empty());

	// Malformed / missing files fail cleanly instead of crashing.
	ppm::Image missing;
	CHECK(!ppm::read("test_ppm_does_not_exist.ppm", &missing, &err));
	CHECK(!err.empty());

	std::remove(path.c_str());

	if(g_failures > 0) {
		std::fprintf(stderr, "%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("test_ppm: OK\n");
	return 0;
}
