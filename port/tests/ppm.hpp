// tests/ppm
// ---------
// Minimal binary PPM (P6, maxval 255) reader/writer and exact comparator,
// shared by the golden-test infrastructure. Golden tests render into an
// RGBA8888 buffer (pc98emu_render() layout: (R<<24)|(G<<16)|(B<<8)|A),
// dump it as PPM, and compare against a checked-in golden file with
// tolerance 0 by default.

#ifndef PORT_TESTS_PPM_HPP
#define PORT_TESTS_PPM_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace ppm {

struct Image {
	int w = 0;
	int h = 0;
	std::vector<uint8_t> rgb; // w * h * 3 bytes, row-major
};

// Builds an Image from `w * h` packed RGBA8888 pixels ((R<<24)|(G<<16)|
// (B<<8)|A). The alpha channel is dropped; PPM has no alpha.
Image from_rgba8888(int w, int h, const uint32_t *rgba);

// Writes `img` as a binary P6 PPM. Returns false on I/O error or if the
// image dimensions don't match the pixel buffer size.
bool write(const std::string &path, const Image &img, std::string *err = nullptr);

// Reads a binary P6 PPM (maxval 255, '#' comments allowed in the header).
// Returns false and fills *err on parse or I/O failure.
bool read(const std::string &path, Image *out, std::string *err = nullptr);

// First point of divergence between two images, for diagnostics.
struct Diff {
	int x = -1;
	int y = -1;
	int channel = -1; // 0=R, 1=G, 2=B
	int a = -1; // channel value in image a
	int b = -1; // channel value in image b
	std::string reason; // human-readable summary (also covers size mismatch)
};

// Compares two images channel-by-channel. Returns true if the dimensions
// match and every channel difference is <= tolerance (golden tests use 0).
// On mismatch, fills *diff with the first divergent pixel or the size
// mismatch reason.
bool compare(const Image &a, const Image &b, int tolerance, Diff *diff = nullptr);

} // namespace ppm

#endif // PORT_TESTS_PPM_HPP
