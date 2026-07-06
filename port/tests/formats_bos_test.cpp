// Unit test for th01/formats/bos.cpp (uncompressed, 5-plane, 16-color +
// alpha, arbitrary-size sprite format): builds a synthetic .BOS file inside
// a synthetic packfile, loads it through the real BOS::load(), and verifies
// the decoded alpha + B/R/G/E plane byte arrays.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include "port/compat/borland.hpp"

#include "th01/formats/bos.hpp"
#include "th01/formats/pf.hpp"
#include "pf_synth.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

std::string temp_dir() {
#ifdef PORT_TESTS_TMP_DIR
	return std::string(PORT_TESTS_TMP_DIR);
#else
	return std::string("/tmp");
#endif
}

void write_file(const std::string &path, const std::vector<uint8_t> &bytes) {
	std::FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

// Builds a synthetic .BOS file: bos_header_t (4-byte magic, uint8_t
// vram_w, int8_t zero, uint8_t h, int8_t unknown, 8-byte
// spriteformat_header_inner_t) + a 48-byte palette (skipped by BOS::load)
// + per image: alpha, then B/R/G/E planes, each vram_w*h bytes.
std::vector<uint8_t> build_bos(
	uint8_t vram_w, uint8_t h, uint8_t image_count,
	const std::vector<std::vector<uint8_t>> &alphas,
	const std::vector<std::vector<uint8_t>> &planes_b,
	const std::vector<std::vector<uint8_t>> &planes_r,
	const std::vector<std::vector<uint8_t>> &planes_g,
	const std::vector<std::vector<uint8_t>> &planes_e
) {
	std::vector<uint8_t> out;
	out.push_back('B');
	out.push_back('O');
	out.push_back('S');
	out.push_back('S');
	out.push_back(vram_w);
	out.push_back(0); // zero
	out.push_back(h);
	out.push_back(0); // unknown
	out.push_back(image_count); // inner.image_count
	for(int i = 0; i < 7; i++) out.push_back(0); // inner.unknown[7]
	for(int i = 0; i < 48; i++) out.push_back(0); // Palette4, skipped

	for(int i = 0; i < image_count; i++) {
		out.insert(out.end(), alphas[i].begin(), alphas[i].end());
		out.insert(out.end(), planes_b[i].begin(), planes_b[i].end());
		out.insert(out.end(), planes_r[i].begin(), planes_r[i].end());
		out.insert(out.end(), planes_g[i].begin(), planes_g[i].end());
		out.insert(out.end(), planes_e[i].begin(), planes_e[i].end());
	}
	return out;
}

} // namespace

int main() {
	if(chdir(temp_dir().c_str()) != 0) {
		std::fprintf(stderr, "FAIL: could not chdir to temp dir\n");
		return 1;
	}
	// arc_load()'s packfile name is stored/matched via a fixed-size
	// PF_FN_LEN (13, "8.3 + null") buffer -- a full temp-directory path
	// would silently truncate. Use a short 8.3-style name in the CWD.
	const std::string pf_path = "T.PAR";

	const uint8_t vram_w = 2;
	const uint8_t h = 3;
	const uint8_t image_count = 2;
	const int plane_size = vram_w * h;

	std::vector<std::vector<uint8_t>> alphas = {
		{0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
		{0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF},
	};
	std::vector<std::vector<uint8_t>> planes_b = {
		{1, 2, 3, 4, 5, 6}, {10, 20, 30, 40, 50, 60},
	};
	std::vector<std::vector<uint8_t>> planes_r = {
		{7, 8, 9, 10, 11, 12}, {70, 80, 90, 100, 110, 120},
	};
	std::vector<std::vector<uint8_t>> planes_g = {
		{13, 14, 15, 16, 17, 18}, {130, 140, 150, 160, 170, 180},
	};
	std::vector<std::vector<uint8_t>> planes_e = {
		{19, 20, 21, 22, 23, 24}, {190, 200, 210, 220, 230, 240},
	};

	pf_synth::Entry entry;
	entry.name = "TEST.BOS";
	entry.data = build_bos(vram_w, h, image_count, alphas, planes_b, planes_r, planes_g, planes_e);
	entry.compressed = false;

	write_file(pf_path, pf_synth::build({entry}, 0));

	arc_key = 0;
	arc_load(pf_path.c_str());

	BOS bos;
	bos_image_t images[2] = {};
	vram_byte_amount_t returned_plane_size = bos.load(images, "TEST.BOS");

	expect(returned_plane_size == plane_size, "BOS::load returns the correct plane size");
	expect(bos.vram_w == vram_w, "BOS::load decodes vram_w");
	expect(bos.h == h, "BOS::load decodes h");
	expect(bos.bos_image_count == image_count, "BOS::load decodes image_count");

	for(int i = 0; i < image_count; i++) {
		expect(std::memcmp(images[i].alpha, alphas[i].data(), plane_size) == 0,
		       "BOS::load decodes alpha plane verbatim");
		expect(std::memcmp(images[i].planes.B, planes_b[i].data(), plane_size) == 0,
		       "BOS::load decodes B plane verbatim");
		expect(std::memcmp(images[i].planes.R, planes_r[i].data(), plane_size) == 0,
		       "BOS::load decodes R plane verbatim");
		expect(std::memcmp(images[i].planes.G, planes_g[i].data(), plane_size) == 0,
		       "BOS::load decodes G plane verbatim");
		expect(std::memcmp(images[i].planes.E, planes_e[i].data(), plane_size) == 0,
		       "BOS::load decodes E plane verbatim");
	}

	for(int i = 0; i < image_count; i++) {
		delete[] images[i].alpha;
		delete[] images[i].planes.B;
		delete[] images[i].planes.R;
		delete[] images[i].planes.G;
		delete[] images[i].planes.E;
	}
	arc_free();

	std::remove(pf_path.c_str());

	if(g_failures == 0) {
		std::printf("formats_bos_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
