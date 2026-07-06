// Unit test for th01/formats/grp.cpp (.GRP -- a .PI variant, decoded
// directly to VRAM by PiLoad's PiBlitL()). Since real PiLoad is a 16-bit
// assembly library (see port/shim/piload_stub.cpp), this drives grp_put()
// through the test-only synthetic decode format that stub implements,
// verifying grp.cpp's own wrapper logic: palette extraction into
// grp_palette, direct-to-VRAM pixel decode, and GPF_COLORKEY masking.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "port/compat/borland.hpp"

#include "th01/formats/grp.h"
#include "pc98emu/pc98emu.hpp"
#include "th01_palette_test_stub.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

std::string temp_path(const char *name) {
#ifdef PORT_TESTS_TMP_DIR
	return std::string(PORT_TESTS_TMP_DIR) + "/" + name;
#else
	return std::string("/tmp/") + name;
#endif
}

// Unqualified, not pc98emu::PLANE_SIZE: th01/formats/grp.h pulls in pc98.h,
// which #defines an identically-named, identically-valued macro at global
// scope -- that would rewrite the qualified name into garbage before the
// compiler ever sees it (see port/shim/masterlib/gfx.cpp's identical note).
constexpr int kPlaneSize = PLANE_SIZE;

// Builds a synthetic .GRP file matching port/shim/piload_stub.cpp's
// documented test wire format: 2-byte magic, 48-byte raw Palette4, then
// four 32000-byte planes (B, R, G, E).
std::vector<uint8_t> build_grp(
	const int8_t palette_rgb[16][3],
	const std::vector<uint8_t> &plane_b,
	const std::vector<uint8_t> &plane_r,
	const std::vector<uint8_t> &plane_g,
	const std::vector<uint8_t> &plane_e
) {
	std::vector<uint8_t> out;
	out.push_back('Z');
	out.push_back('N');
	for(int c = 0; c < 16; c++) {
		for(int comp = 0; comp < 3; comp++) {
			out.push_back(static_cast<uint8_t>(palette_rgb[c][comp]));
		}
	}
	out.insert(out.end(), plane_b.begin(), plane_b.end());
	out.insert(out.end(), plane_r.begin(), plane_r.end());
	out.insert(out.end(), plane_g.begin(), plane_g.end());
	out.insert(out.end(), plane_e.begin(), plane_e.end());
	return out;
}

void write_file(const std::string &path, const std::vector<uint8_t> &bytes) {
	std::FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

} // namespace

int main() {
	const std::string grp_path = temp_path("formats_grp_test.grp");

	int8_t palette[16][3] = {};
	for(int c = 0; c < 16; c++) {
		palette[c][0] = static_cast<int8_t>(c);
		palette[c][1] = static_cast<int8_t>(15 - c);
		palette[c][2] = static_cast<int8_t>(c % 4);
	}

	// -- Plain decode (no colorkey): first row all color #3 (B=1,R=1,G=0,E=0
	// -> bit0|bit1 = 3), rest zero.
	{
		pc98emu_init();

		std::vector<uint8_t> plane_b(kPlaneSize, 0);
		std::vector<uint8_t> plane_r(kPlaneSize, 0);
		std::vector<uint8_t> plane_g(kPlaneSize, 0);
		std::vector<uint8_t> plane_e(kPlaneSize, 0);
		plane_b[0] = 0xFF; // color bit0 set for the first 8 dots
		plane_r[0] = 0xFF; // color bit1 set too -> color index 3 for those dots

		write_file(grp_path, build_grp(palette, plane_b, plane_r, plane_g, plane_e));

		int ret = grp_put(grp_path.c_str(), GPF_PALETTE_KEEP);
		expect(ret == 0, "grp_put() returns success");

		for(int c = 0; c < 16; c++) {
			expect(grp_palette[c].c.r == palette[c][0], "grp_palette.r matches header");
			expect(grp_palette[c].c.g == palette[c][1], "grp_palette.g matches header");
			expect(grp_palette[c].c.b == palette[c][2], "grp_palette.b matches header");
		}

		expect(VRAM_PLANE_B[0] == 0xFF, "VRAM plane B decoded at offset 0");
		expect(VRAM_PLANE_R[0] == 0xFF, "VRAM plane R decoded at offset 0");
		expect(VRAM_PLANE_G[0] == 0x00, "VRAM plane G stays clear at offset 0");
		expect(VRAM_PLANE_B[1] == 0x00, "VRAM plane B clear past the first byte");
	}

	// -- GPF_COLORKEY: color #15 (all planes set) must be skipped, leaving
	// pre-existing VRAM content in place; other dots overwrite normally.
	{
		pc98emu_init();
		// Pre-fill VRAM offset 0 with a known "background" pattern.
		VRAM_PLANE_B[0] = 0b10101010;
		VRAM_PLANE_R[0] = 0b10101010;
		VRAM_PLANE_G[0] = 0b10101010;
		VRAM_PLANE_E[0] = 0b10101010;

		std::vector<uint8_t> plane_b(kPlaneSize, 0);
		std::vector<uint8_t> plane_r(kPlaneSize, 0);
		std::vector<uint8_t> plane_g(kPlaneSize, 0);
		std::vector<uint8_t> plane_e(kPlaneSize, 0);
		// Dot 0 (MSB, mask 0x80): color #15 (all planes 1) -> transparent,
		// must be skipped. Dot 1 (mask 0x40): color #1 (B=1 only) -> opaque,
		// must overwrite.
		plane_b[0] = 0b11000000;
		plane_r[0] = 0b10000000;
		plane_g[0] = 0b10000000;
		plane_e[0] = 0b10000000;

		write_file(grp_path, build_grp(palette, plane_b, plane_r, plane_g, plane_e));

		int ret = grp_put(grp_path.c_str(), GPF_PALETTE_KEEP | GPF_COLORKEY);
		expect(ret == 0, "grp_put() with GPF_COLORKEY returns success");

		expect((VRAM_PLANE_B[0] & 0x80) == (0b10101010 & 0x80),
		       "colorkeyed dot (color #15) leaves pre-existing VRAM B bit untouched");
		expect((VRAM_PLANE_R[0] & 0x80) == (0b10101010 & 0x80),
		       "colorkeyed dot leaves pre-existing VRAM R bit untouched");

		expect((VRAM_PLANE_B[0] & 0x40) == 0x40, "opaque dot writes VRAM B bit");
		expect((VRAM_PLANE_R[0] & 0x40) == 0x00, "opaque dot (color #1) leaves R bit clear");
		expect((VRAM_PLANE_G[0] & 0x40) == 0x00, "opaque dot leaves G bit clear");
		expect((VRAM_PLANE_E[0] & 0x40) == 0x00, "opaque dot leaves E bit clear");
	}

	std::remove(grp_path.c_str());

	if(g_failures == 0) {
		std::printf("formats_grp_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
