// Unit test for th01/formats/ptn.cpp (uncompressed 16-color 32x32 sprite
// format, with alpha derived from color #15 transparency): builds a
// synthetic .PTN file inside a synthetic packfile, loads it through the
// real ptn_load(), and exercises ptn_put_noalpha_8()/ptn_put_8() to verify
// both the raw pixel decode and the color-#15 alpha masking against VRAM.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include "port/compat/borland.hpp"

#include "th01/formats/ptn.hpp"
#include "th01/formats/ptn_data.hpp"
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

// Builds a single-image synthetic .PTN file: ptn_header_t (4-byte magic,
// int8_t unused_one, int8_t image_count) + a 48-byte palette (skipped by
// ptn_load) + per image: a 1-byte unused_zero + Planar<ptn_plane_t>
// (4 planes * 32 rows * 4 bytes each, B/R/G/E order, little-endian
// per-row so on-disk byte k of a row lands at VRAM byte offset+k after a
// put).
std::vector<uint8_t> build_ptn(
	const uint8_t plane_b[32][4], const uint8_t plane_r[32][4],
	const uint8_t plane_g[32][4], const uint8_t plane_e[32][4]
) {
	std::vector<uint8_t> out;
	out.push_back('H');
	out.push_back('P');
	out.push_back('T');
	out.push_back('N');
	out.push_back(0); // unused_one
	out.push_back(1); // image_count
	for(int i = 0; i < 48; i++) out.push_back(0); // Palette4, skipped

	out.push_back(0); // per-image unused_zero
	auto push_plane = [&](const uint8_t rows[32][4]) {
		for(int y = 0; y < 32; y++) {
			for(int k = 0; k < 4; k++) {
				out.push_back(rows[y][k]);
			}
		}
	};
	push_plane(plane_b);
	push_plane(plane_r);
	push_plane(plane_g);
	push_plane(plane_e);
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

	uint8_t plane_b[32][4] = {};
	uint8_t plane_r[32][4] = {};
	uint8_t plane_g[32][4] = {};
	uint8_t plane_e[32][4] = {};
	// Row 0, byte 0: all 4 planes set -> color #15 (transparent).
	plane_b[0][0] = plane_r[0][0] = plane_g[0][0] = plane_e[0][0] = 0xFF;
	// Row 0, byte 1: only plane B set -> color #1 (opaque).
	plane_b[0][1] = 0xFF;
	// Everything else stays 0 (color #0, opaque black).

	pf_synth::Entry entry;
	entry.name = "TEST.PTN";
	entry.data = build_ptn(plane_b, plane_r, plane_g, plane_e);
	entry.compressed = false;

	write_file(pf_path, pf_synth::build({entry}, 0));

	arc_key = 0;
	arc_load(pf_path.c_str());

	ptn_error_t ret = ptn_load(PTN_SLOT_BOSS_1, "TEST.PTN");
	expect(ret == PE_OK, "ptn_load() returns PE_OK");

	int ptn_id = PTN_ID(PTN_SLOT_BOSS_1, 0);
	ptn_t *ptn = ptn_with_id(ptn_id);

	// Alpha byte-level check: alpha = ~(B & R & G & E). Byte 0 (color #15,
	// all planes 1) -> alpha byte 0x00 (transparent). Byte 1 (only B=1,
	// R/G/E=0 -> AND=0) -> alpha byte 0xFF (opaque).
	const uint8_t *alpha_bytes = reinterpret_cast<const uint8_t *>(&ptn->alpha[0]);
	expect(alpha_bytes[0] == 0x00, "alpha byte 0 (color #15) is transparent (0x00)");
	expect(alpha_bytes[1] == 0xFF, "alpha byte 1 (color #1) is opaque (0xFF)");

	// ptn_put_noalpha_8: raw decode, ignores alpha entirely.
	ptn_put_noalpha_8(0, 0, ptn_id);
	expect(VRAM_PLANE_B[0] == 0xFF, "noalpha put: plane B byte 0 decoded verbatim");
	expect(VRAM_PLANE_R[0] == 0xFF, "noalpha put: plane R byte 0 decoded verbatim");
	expect(VRAM_PLANE_B[1] == 0xFF, "noalpha put: plane B byte 1 decoded verbatim");
	expect(VRAM_PLANE_R[1] == 0x00, "noalpha put: plane R byte 1 decoded verbatim");

	// ptn_put_8: masked by alpha -- pre-fill VRAM with a background pattern,
	// then only non-transparent dots (byte 1's color #1) should change.
	pc98emu_vram_reset();
	VRAM_PLANE_B[0] = 0b01010101;
	VRAM_PLANE_R[0] = 0b01010101;
	VRAM_PLANE_B[1] = 0b01010101;
	VRAM_PLANE_R[1] = 0b01010101;

	ptn_put_8(0, 0, ptn_id);
	expect(VRAM_PLANE_B[0] == 0b01010101, "masked put: color-#15 byte leaves background untouched (B)");
	expect(VRAM_PLANE_R[0] == 0b01010101, "masked put: color-#15 byte leaves background untouched (R)");
	expect(VRAM_PLANE_B[1] == 0xFF, "masked put: color-#1 byte overwrites with sprite data (B)");
	expect(VRAM_PLANE_R[1] == 0x00, "masked put: color-#1 byte overwrites with sprite data (R)");

	ptn_free(PTN_SLOT_BOSS_1);
	arc_free();

	std::remove(pf_path.c_str());

	if(g_failures == 0) {
		std::printf("formats_ptn_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
