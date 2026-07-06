// Headless pixel verification for M1 (SPEC.md): decodes TH01's real
// REIIDEN2.GRP title-screen asset through the real PiBlitL() (port/shim/
// piload.cpp), same code path th01op's title_init() uses, and compares the
// rendered framebuffer against port/tests/golden/title_reference.ppm.
//
// Skipped (not failed) when the real assets aren't available -- this repo
// ships without them (see SPEC.md), so CI/other machines shouldn't fail on
// their absence. Set TH01_ASSETS_DIR to point at a directory containing the
// original TH01 data files to run this for real.

#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "port/compat/borland.hpp"

#include "port/app/assets_dir.hpp"
#include "th01/formats/grp.h"
#include "pc98emu/pc98emu.hpp"

namespace {

// See tests/*_test.cpp convention elsewhere and port/app/main_op.cpp: the
// same default path SPEC.md's asset layout documents, overridable via
// TH01_ASSETS_DIR for machines that keep the assets elsewhere.
constexpr const char *DEFAULT_ASSETS_DIR =
	"/Users/geekjapan/dev/projects/touhou/assets/th01";
constexpr const char *ASSET_CANARY = "REIIDEN2.GRP";

constexpr const char *GOLDEN_PATH =
#ifdef PORT_TESTS_GOLDEN_DIR
	PORT_TESTS_GOLDEN_DIR "/title_reference.ppm";
#else
	"port/tests/golden/title_reference.ppm";
#endif

// SKIP (not fail) autotools-style: CMake's SKIP_RETURN_CODE test property
// (set in CMakeLists.txt for this test) maps this exit code to "skipped".
constexpr int EXIT_SKIP = 77;

std::vector<uint8_t> read_file(const std::string &path, bool &ok) {
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if(!f) {
		ok = false;
		return {};
	}
	std::fseek(f, 0, SEEK_END);
	long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> data(static_cast<size_t>(size));
	ok = (std::fread(data.data(), 1, data.size(), f) == data.size());
	std::fclose(f);
	return data;
}

// Parses just enough of a binary PPM (P6) to get at the raw RGB bytes --
// this test only ever reads back what it (or a prior run's dump) wrote, so
// no need for a general-purpose parser.
bool parse_ppm(const std::vector<uint8_t> &bytes, int &w, int &h, const uint8_t *&pixels) {
	if(bytes.size() < 2 || bytes[0] != 'P' || bytes[1] != '6') {
		return false;
	}
	size_t pos = 2;
	int fields[3];
	for(int &field : fields) {
		while(pos < bytes.size() && std::isspace(bytes[pos])) {
			pos++;
		}
		if(pos < bytes.size() && bytes[pos] == '#') {
			while(pos < bytes.size() && bytes[pos] != '\n') {
				pos++;
			}
			continue;
		}
		int value = 0;
		bool any = false;
		while(pos < bytes.size() && std::isdigit(bytes[pos])) {
			value = (value * 10) + (bytes[pos] - '0');
			pos++;
			any = true;
		}
		if(!any) {
			return false;
		}
		field = value;
	}
	pos++; // single whitespace byte after maxval, per the P6 spec
	w = fields[0];
	h = fields[1];
	if(pos + static_cast<size_t>(w) * h * 3 > bytes.size()) {
		return false;
	}
	pixels = bytes.data() + pos;
	return true;
}

} // namespace

int main() {
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, ASSET_CANARY
	);
	std::string canary_path = assets_dir + "/" + ASSET_CANARY;
	if(!port::file_exists(canary_path)) {
		std::printf(
			"title_screen_golden: skipping -- no real TH01 assets at '%s' "
			"(set TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	pc98emu_init();
	int ret = grp_put((assets_dir + "/REIIDEN2.GRP").c_str(), GPF_PALETTE_SHOW);
	if(ret != 0) {
		std::fprintf(stderr, "title_screen_golden: grp_put() failed: %d\n", ret);
		return 1;
	}

	std::vector<uint32_t> fb(static_cast<size_t>(RES_X) * RES_Y);
	pc98emu_render(fb.data());

	bool read_ok = false;
	std::vector<uint8_t> golden_bytes = read_file(GOLDEN_PATH, read_ok);
	if(!read_ok) {
		std::fprintf(stderr,
			"title_screen_golden: couldn't read golden file '%s'\n", GOLDEN_PATH);
		return 1;
	}
	int gw = 0, gh = 0;
	const uint8_t *gpix = nullptr;
	if(!parse_ppm(golden_bytes, gw, gh, gpix)) {
		std::fprintf(stderr, "title_screen_golden: couldn't parse golden PPM\n");
		return 1;
	}
	if(gw != RES_X || gh != RES_Y) {
		std::fprintf(stderr,
			"title_screen_golden: golden size %dx%d != decoded %dx%d\n",
			gw, gh, RES_X, RES_Y);
		return 1;
	}

	long mismatches = 0;
	for(int i = 0; i < RES_X * RES_Y; i++) {
		uint32_t px = fb[static_cast<size_t>(i)];
		uint8_t r = static_cast<uint8_t>((px >> 24) & 0xFF);
		uint8_t g = static_cast<uint8_t>((px >> 16) & 0xFF);
		uint8_t b = static_cast<uint8_t>((px >> 8) & 0xFF);
		const uint8_t *gp = gpix + (static_cast<size_t>(i) * 3);
		if(r != gp[0] || g != gp[1] || b != gp[2]) {
			mismatches++;
		}
	}

	if(mismatches != 0) {
		std::fprintf(stderr,
			"title_screen_golden: FAIL -- %ld/%d pixels differ from golden\n",
			mismatches, RES_X * RES_Y);
		return 1;
	}

	std::printf("title_screen_golden: decoded title screen matches golden reference\n");
	return 0;
}
