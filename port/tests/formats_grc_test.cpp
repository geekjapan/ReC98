// Unit test for th01/formats/grc.cpp (uncompressed monochrome 8w x h sprite
// format): builds a synthetic .GRC file inside a synthetic packfile with
// pf_synth.hpp, loads it through the real grc_load(), and blits it through
// grc_put_8() to verify decoded pixel/VRAM state.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include "port/compat/borland.hpp"

#include "th01/formats/grc.hpp"
#include "th01/formats/pf.hpp"
#include "pc98emu/pc98emu.hpp"
#include "pf_synth.hpp"
#include "th01_grcg_test_stub.hpp"

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

// Builds a synthetic .GRC entry (grc_header_t layout: 4-byte magic,
// int16_t vram_w, int16_t h, 8-byte spriteformat_header_inner_t), followed
// by a 48-byte palette (skipped by grc_load) and then image_count *
// (vram_w * h) raw dot bytes.
std::vector<uint8_t> build_grc(
	int16_t vram_w, int16_t h, uint8_t image_count,
	const std::vector<std::vector<uint8_t>> &images
) {
	std::vector<uint8_t> out;
	out.push_back('G');
	out.push_back('R');
	out.push_back('C');
	out.push_back('G');
	auto push16 = [&](int16_t v) {
		out.push_back(static_cast<uint8_t>(v & 0xFF));
		out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	};
	push16(vram_w);
	push16(h);
	out.push_back(image_count); // spriteformat_header_inner_t::image_count
	for(int i = 0; i < 7; i++) out.push_back(0); // ::unknown[7]
	for(int i = 0; i < 48; i++) out.push_back(0); // Palette4, skipped
	for(const auto &img : images) {
		out.insert(out.end(), img.begin(), img.end());
	}
	return out;
}

} // namespace

int main() {
	pc98emu_init();

	if(chdir(temp_dir().c_str()) != 0) {
		std::fprintf(stderr, "FAIL: could not chdir to temp dir\n");
		return 1;
	}
	// arc_load()'s packfile name is stored/matched via a fixed-size
	// PF_FN_LEN (13, "8.3 + null") buffer -- a full temp-directory path
	// would silently truncate. Use a short 8.3-style name in the CWD.
	const std::string pf_path = "T.PAR";

	const int16_t vram_w = 6; // 48 dots wide (matches grc_sloppy_unput's assumed 48)
	const int16_t h = 4;
	const uint8_t image_count = 2;

	std::vector<uint8_t> image0(vram_w * h, 0);
	// A single lit dot column at (byte 0, bit 0 = leftmost) on row 0.
	image0[0] = 0x80;
	std::vector<uint8_t> image1(vram_w * h, 0xFF); // fully lit image

	pf_synth::Entry entry;
	entry.name = "TEST.GRC";
	entry.data = build_grc(vram_w, h, image_count, {image0, image1});
	entry.compressed = false;

	write_file(pf_path, pf_synth::build({entry}, 0));

	arc_key = 0;
	arc_load(pf_path.c_str());

	int ret = grc_load(GRC_SLOT_BOSS_1, "TEST.GRC");
	expect(ret == 0, "grc_load() returns success");

	// grc_put_8() at (0, 0) with color #1 (VRAM plane B only).
	grc_put_8(0, 0, GRC_SLOT_BOSS_1, 0, 1);
	expect((VRAM_PLANE_B[0] & 0x80) != 0, "grc_put_8 image0's lit dot sets VRAM plane B");
	expect((VRAM_PLANE_R[0] & 0x80) == 0, "grc_put_8 with color #1 leaves plane R clear");
	expect(VRAM_PLANE_B[1] == 0, "grc_put_8 leaves the rest of the row untouched");

	pc98emu_vram_reset();
	grc_put_8(0, 0, GRC_SLOT_BOSS_1, 1, 1);
	expect(VRAM_PLANE_B[0] == 0xFF, "grc_put_8 image1 (fully lit) sets the whole first byte");
	expect(VRAM_PLANE_B[vram_w - 1] == 0xFF, "grc_put_8 image1 sets the last byte of the row");

	grc_free(GRC_SLOT_BOSS_1);
	arc_free();

	std::remove(pf_path.c_str());

	if(g_failures == 0) {
		std::printf("formats_grc_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
