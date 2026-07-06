// Unit test for th01/formats/grz.cpp (.GRZ/.GRX -- run-length-encoded,
// 16-color + alpha image format with a decoupled RLE command stream):
// builds a synthetic .GRZ file directly (not inside a packfile -- grz.cpp
// reads straight from a plain file via file_ropen/file_read/file_seek),
// loads a single image with grz_load_single(), and verifies grx_put()'s
// direct-to-VRAM decode of the RLE command stream + planar pixel stream.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "port/compat/borland.hpp"

#include "th01/formats/grz.h"
#include "pc98emu/pc98emu.hpp"

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

void write_file(const std::string &path, const std::vector<uint8_t> &bytes) {
	std::FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

// Unqualified, not pc98emu::PLANE_SIZE: th01/formats/grz.h pulls in pc98.h,
// which #defines an identically-named, identically-valued macro at global
// scope -- that would rewrite the qualified name into garbage before the
// compiler ever sees it (see port/shim/masterlib/gfx.cpp's identical note).
constexpr int kPlaneSize = PLANE_SIZE;

void push_u16(std::vector<uint8_t> &out, uint16_t v) {
	out.push_back(static_cast<uint8_t>(v & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void push_i32(std::vector<uint8_t> &out, int32_t v) {
	out.push_back(static_cast<uint8_t>(v & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// Builds a single-image synthetic .GRZ file: a grz_header_t (96 bytes) with
// one image at offset 96, followed by a grx_header_t (64 bytes, palette
// included and left zeroed/unused), an RLE command stream (one byte per
// VRAM plane position -- 0x00 = GC_PUT, anything else = skip; GC_RUN is
// never used here, keeping the encoding trivially position-addressable),
// and a planar pixel stream (4 bytes -- B,R,G,E -- per PUT position, in
// stream order).
std::vector<uint8_t> build_grz(
	const std::vector<uint8_t> &rle_stream, const std::vector<uint8_t> &planar_stream
) {
	std::vector<uint8_t> out;
	// grz_header_t
	out.push_back('H'); out.push_back('G'); out.push_back('R'); out.push_back('Z');
	out.push_back(1); // image_count
	out.push_back(0); out.push_back(0); out.push_back(0); // padding[3]
	push_i32(out, 96); // offsets[0]: right after the 96-byte grz_header_t
	for(int i = 1; i < 16; i++) push_i32(out, 0); // offsets[1..15], unused
	push_i32(out, 0); // total_size, unused by grz_load_single
	for(int i = 0; i < 20; i++) out.push_back(0); // unknown[20]
	// (4+1+3+64+4+20 == 96)

	// grx_header_t at offset 96
	out.push_back('H'); out.push_back('G'); out.push_back('R'); out.push_back('X');
	out.push_back(1); // planar_stream_count
	out.push_back(0); out.push_back(0); out.push_back(0); // unused_1[3]
	push_u16(out, static_cast<uint16_t>(rle_stream.size()));
	push_u16(out, static_cast<uint16_t>(planar_stream.size()));
	for(int i = 0; i < 4; i++) out.push_back(0); // unused_2[4]
	for(int i = 0; i < 48; i++) out.push_back(0); // Palette4, unused here
	// (4+1+3+2+2+4+48 == 64)

	out.insert(out.end(), rle_stream.begin(), rle_stream.end());
	out.insert(out.end(), planar_stream.begin(), planar_stream.end());
	return out;
}

} // namespace

int main() {
	pc98emu_init();

	const std::string grz_path = temp_path("formats_grz_test.grz");

	std::vector<uint8_t> rle_stream(kPlaneSize, 0xFF); // default: skip everywhere
	rle_stream[0] = GC_PUT;
	rle_stream[2] = GC_PUT;

	std::vector<uint8_t> planar_stream = {
		0xAA, 0xBB, 0xCC, 0xDD, // position 0: B,R,G,E
		0x11, 0x22, 0x33, 0x44, // position 2: B,R,G,E
	};

	write_file(grz_path, build_grz(rle_stream, planar_stream));

	int ret = grz_load_single(0, grz_path.c_str(), 0);
	expect(ret == 0, "grz_load_single() returns success");

	grx_put(0);

	expect(VRAM_PLANE_B[0] == 0xAA, "grx_put decodes position 0 plane B");
	expect(VRAM_PLANE_R[0] == 0xBB, "grx_put decodes position 0 plane R");
	expect(VRAM_PLANE_G[0] == 0xCC, "grx_put decodes position 0 plane G");
	expect(VRAM_PLANE_E[0] == 0xDD, "grx_put decodes position 0 plane E");

	expect(VRAM_PLANE_B[1] == 0x00, "grx_put leaves skipped position 1 untouched");

	expect(VRAM_PLANE_B[2] == 0x11, "grx_put decodes position 2 plane B");
	expect(VRAM_PLANE_R[2] == 0x22, "grx_put decodes position 2 plane R");
	expect(VRAM_PLANE_G[2] == 0x33, "grx_put decodes position 2 plane G");
	expect(VRAM_PLANE_E[2] == 0x44, "grx_put decodes position 2 plane E");

	expect(VRAM_PLANE_B[3] == 0x00, "grx_put leaves trailing skipped positions untouched");

	grx_free(0);

	std::remove(grz_path.c_str());

	if(g_failures == 0) {
		std::printf("formats_grz_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
