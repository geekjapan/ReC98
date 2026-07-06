// Unit test for th01/formats/grp.cpp (.GRP -- a .PI variant, decoded
// directly to VRAM by PiLoad's PiBlitL()). Real PiLoad is now a real C++
// port of the compressed Pi format (port/shim/piload.cpp), not a synthetic
// stand-in, so this test builds actual compressed bitstreams: a tiny
// hand-rolled "encoder" mirrors piload.cpp's move-to-front cache
// (mtf_decode_stage()) in reverse, searching for each desired pixel value
// and emitting the bits piload.cpp would need to read to reproduce it. This
// verifies grp.cpp's own wrapper logic (palette extraction into
// grp_palette, direct-to-VRAM pixel decode, GPF_COLORKEY masking) through
// the real decoder, not just a synthetic format.
//
// The image content itself is a minimal 8x4 test pattern -- see
// port/tests/golden/title_reference.ppm (and the real-asset ctest that
// compares against it) for end-to-end verification against actual TH01
// title-screen assets.

#include <algorithm>
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

void write_file(const std::string &path, const std::vector<uint8_t> &bytes) {
	std::FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

// -- Bit-level Pi encoder -----------------------------------------------
// Mirrors port/shim/piload.cpp's BitReader/mtf_decode_stage exactly, just
// in "search + emit" direction instead of "read + apply".

class BitWriter {
public:
	void put(int bit) {
		cur_ = static_cast<uint8_t>((cur_ << 1) | (bit & 1));
		nbits_++;
		if(nbits_ == 8) {
			bytes_.push_back(cur_);
			cur_ = 0;
			nbits_ = 0;
		}
	}

	std::vector<uint8_t> finish() {
		while(nbits_ != 0) {
			put(0);
		}
		return bytes_;
	}

private:
	std::vector<uint8_t> bytes_;
	uint8_t cur_ = 0;
	int nbits_ = 0;
};

// Returns the bit sequence piload.cpp's general (rank 2..15) path would
// read to decode `target_cx`, found by brute-force since the search space
// per shape is tiny (matches mtf_decode_stage's cx=1;shift/or ladder).
std::vector<int> encode_general_cx(int target_cx) {
	for(int b4 = 0; b4 <= 1; b4++) {
		for(int b5 = 0; b5 <= 1; b5++) {
			int cx = 1;
			cx = (cx << 1) | b4;
			if(cx == target_cx) {
				return {0, b4};
			}
		}
	}
	for(int b2 = 0; b2 <= 1; b2++) {
		for(int b3 = 0; b3 <= 1; b3++) {
			int cx = 1;
			cx = (cx << 1) | b2;
			cx = (cx << 1) | b3;
			if(cx == target_cx) {
				return {1, 0, b2, b3};
			}
		}
	}
	for(int b3 = 0; b3 <= 1; b3++) {
		for(int b4 = 0; b4 <= 1; b4++) {
			for(int b5 = 0; b5 <= 1; b5++) {
				int cx = 1;
				cx = (cx << 1) | b3;
				cx = (cx << 1) | b4;
				cx = (cx << 1) | b5;
				if(cx == target_cx) {
					return {1, 1, b3, b4, b5};
				}
			}
		}
	}
	std::fprintf(stderr, "encode_general_cx: no shape produces cx=%d\n", target_cx);
	std::abort();
}

// One move-to-front cache lookup+promotion step, in reverse: finds `target`
// at rank 0, 1, or 2..15 relative to `seed`, emits the matching bits, and
// applies the identical promotion piload.cpp's mtf_decode_stage() would.
void mtf_encode_stage(uint8_t *mtf, BitWriter &bw, int seed, uint8_t target) {
	if(mtf[seed] == target) {
		bw.put(1);
		bw.put(0);
		return;
	}
	if(mtf[seed + 1] == target) {
		bw.put(1);
		bw.put(1);
		std::swap(mtf[seed], mtf[seed + 1]);
		return;
	}
	for(int cx = 2; cx <= 15; cx++) {
		if(mtf[seed + cx] != target) {
			continue;
		}
		bw.put(0);
		for(int b : encode_general_cx(cx)) {
			bw.put(b);
		}
		int s = seed + cx;
		uint8_t value = mtf[s];
		for(int i = 0; i < cx; i++) {
			mtf[s - i] = mtf[s - i - 1];
		}
		mtf[seed] = value;
		return;
	}
	std::fprintf(stderr, "mtf_encode_stage: target %d not found near seed %d\n",
		target, seed);
	std::abort();
}

// Encodes one literal pixel pair (piload.cpp's pi_color()); returns the
// pair's high byte, which chains as the next call's seed.
uint8_t pi_color_encode(uint8_t *mtf, BitWriter &bw, uint8_t seed, uint8_t lo, uint8_t hi) {
	mtf_encode_stage(mtf, bw, seed, lo);
	mtf_encode_stage(mtf, bw, lo, hi);
	return hi;
}

// Same seeding piload.cpp's mtf_init() uses.
void mtf_init(uint8_t *mtf) {
	uint8_t al = 0;
	int di = 0;
	for(int outer = 0; outer < 16; outer++) {
		for(int inner = 0; inner < 16; inner++) {
			mtf[di++] = al;
			al = static_cast<uint8_t>(al - 16);
		}
		al = static_cast<uint8_t>(al + 16);
	}
}

// Builds a real (compressed) 8x4 .GRP file whose decoded pixels are:
// row 0 = colors[0], row 1 = colors[1], row 2 = colors[2], row 3 = colors[3]
// (each row's 8 pixels all the same color, 0-15). This exercises the exact
// same header + move-to-front + "flat run" decode path th01op reaches with
// real assets, just at a size simple enough to hand-verify.
std::vector<uint8_t> build_grp(const int8_t palette_rgb[16][3], const int row_colors[4]) {
	constexpr int kXWid = 8;
	constexpr int kYWid = 4;

	std::vector<uint8_t> out;
	out.push_back('Z');
	out.push_back('N'); // ZUN's "NZ" magic (bytes 'Z','N' in file order)
	out.push_back(0); // empty comment (NUL terminator only)
	out.push_back(0); // palette flag: bit7 clear -> palette bytes present
	out.push_back(0); // dot ratio (unused), 2 bytes
	out.push_back(0);
	out.push_back(4); // plane count
	out.insert(out.end(), {'P', 'C', '9', '8'}); // target-machine tag
	out.push_back(0); // extension record length (big-endian), 0 = none
	out.push_back(0);
	out.push_back(0); // x_wid, big-endian
	out.push_back(kXWid);
	out.push_back(0); // y_wid, big-endian
	out.push_back(kYWid);
	for(int c = 0; c < 16; c++) {
		for(int comp = 0; comp < 3; comp++) {
			out.push_back(static_cast<uint8_t>(palette_rgb[c][comp]));
		}
	}

	uint8_t mtf[256];
	mtf_init(mtf);
	BitWriter bw;

	// Pixel targets, as raw mtf-table bytes (color << 4): pixels 0,1 come
	// from color(0)'s seed word (copied into gbuf's pre-history, then
	// pulled forward by the first main-loop token -- a "copy the last 2
	// bytes" flat-run/plain token, 3 bits: bit8=0,bit9=0,length=0).
	auto colv = [&](int row) { return static_cast<uint8_t>(row_colors[row] << 4); };

	pi_color_encode(mtf, bw, 0, colv(0), colv(0));
	bw.put(0);
	bw.put(0);
	bw.put(0); // iter1: bit8=0, bit9=0, bjmp length=0 (copy history into gbuf)

	// iter2: bit8=0, bit9=0 -- always lands back in the same "distance
	// marker" as iter1 (-4), so decode falls straight into literal mode
	// (nopres) for the remaining 15 pairs (2 rows' worth minus the first
	// pair, since bufbgn..bufend spans lin=4 rows for an 8-wide image).
	bw.put(0);
	bw.put(0);

	uint8_t seed = colv(0); // gbuf[di-1] after the copy above
	const int kPairs = 15; // (bufend-bufbgn)/2 - 1, see file header comment
	int pixel_index = 2; // pixels 0,1 already produced above
	for(int i = 0; i < kPairs; i++) {
		int row0 = pixel_index / kXWid;
		int row1 = (pixel_index + 1) / kXWid;
		uint8_t lo = colv(row0);
		uint8_t hi = colv(row1);
		seed = pi_color_encode(mtf, bw, seed, lo, hi);
		pixel_index += 2;
		if(i != kPairs - 1) {
			bw.put(1); // continue: one more literal pair follows
		}
		// The last pair's write lands exactly on bufend, which flushes and
		// (since y_wid==lin==4) immediately completes the decode -- no
		// continuation bit is read for it.
	}

	std::vector<uint8_t> bits = bw.finish();
	out.insert(out.end(), bits.begin(), bits.end());
	return out;
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

	// -- Plain decode (no colorkey): row 0 all color #3 (B=1,R=1,G=0,E=0 ->
	// bit0|bit1 = 3), remaining rows color #0.
	{
		pc98emu_init();

		int row_colors[4] = {3, 0, 0, 0};
		write_file(grp_path, build_grp(palette, row_colors));

		int ret = grp_put(grp_path.c_str(), GPF_PALETTE_KEEP);
		expect(ret == 0, "grp_put() returns success");

		for(int c = 0; c < 16; c++) {
			expect(grp_palette[c].c.r == palette[c][0], "grp_palette.r matches header");
			expect(grp_palette[c].c.g == palette[c][1], "grp_palette.g matches header");
			expect(grp_palette[c].c.b == palette[c][2], "grp_palette.b matches header");
		}

		expect(VRAM_PLANE_B[0] == 0xFF, "VRAM plane B decoded at offset 0 (row 0, color 3)");
		expect(VRAM_PLANE_R[0] == 0xFF, "VRAM plane R decoded at offset 0 (row 0, color 3)");
		expect(VRAM_PLANE_G[0] == 0x00, "VRAM plane G stays clear at offset 0");
		// Row 1 lives one VRAM row (ROW_SIZE bytes) after row 0.
		expect(VRAM_PLANE_B[ROW_SIZE] == 0x00, "VRAM plane B clear on row 1 (color 0)");
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

		// Dot 0 (MSB, mask 0x80): color #15 (all planes 1) -> transparent,
		// must be skipped. Remaining dots (including dot 1, mask 0x40):
		// color #1 (B=1 only) -> opaque, must overwrite.
		int8_t pal2[16][3];
		std::memcpy(pal2, palette, sizeof(pal2));

		std::vector<uint8_t> out;
		{
			constexpr int kXWid = 8;
			constexpr int kYWid = 4;
			out.push_back('Z');
			out.push_back('N');
			out.push_back(0);
			out.push_back(0);
			out.push_back(0);
			out.push_back(0);
			out.push_back(4);
			out.insert(out.end(), {'P', 'C', '9', '8'});
			out.push_back(0);
			out.push_back(0);
			out.push_back(0);
			out.push_back(kXWid);
			out.push_back(0);
			out.push_back(kYWid);
			for(int c = 0; c < 16; c++) {
				for(int comp = 0; comp < 3; comp++) {
					out.push_back(static_cast<uint8_t>(pal2[c][comp]));
				}
			}

			uint8_t mtf[256];
			mtf_init(mtf);
			BitWriter bw;

			// Pixel 0 = color 15, pixel 1 = color 1 (the seed word, copied
			// verbatim into pixels 0,1 by the first main-loop token); every
			// other pixel = color 0.
			uint8_t seed_lo = 15 << 4;
			uint8_t seed_hi = 1 << 4;
			pi_color_encode(mtf, bw, 0, seed_lo, seed_hi);
			bw.put(0);
			bw.put(0);
			bw.put(0); // iter1: copy the seed pair into gbuf

			bw.put(0);
			bw.put(0); // iter2: straight into literal mode

			uint8_t seed = seed_hi;
			const int kPairs = 15;
			for(int i = 0; i < kPairs; i++) {
				seed = pi_color_encode(mtf, bw, seed, 0, 0);
				if(i != kPairs - 1) {
					bw.put(1);
				}
			}

			std::vector<uint8_t> bits = bw.finish();
			out.insert(out.end(), bits.begin(), bits.end());
		}
		write_file(grp_path, out);

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
