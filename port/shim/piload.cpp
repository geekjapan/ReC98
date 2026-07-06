// port/shim/piload.cpp
// -----------------------------------------------------------------------
// Real port of libs/piloadc/piloadm.asm's PiBlitL(): decodes ZUN's Pi-format
// (.PI/.GRP) compressed 16-color images directly into pc98emu's VRAM planes.
// Replaces the old port/shim/piload_stub.cpp synthetic format now that M1
// (SPEC.md) needs real .GRP assets to actually decode.
//
// The format (reverse-engineered from piloadm.asm's piblit0/color()/color2()
// -- see also https://mooncore.eu/bunny/txt/pi-pic.htm for an independent
// writeup of the same compression scheme):
//
//   header: 2-byte magic ("iP" or ZUN's "NZ"), NUL-terminated comment,
//   palette-flag byte, dot-ratio word (unused here), plane-count byte
//   (always 4), 4-byte target-machine tag ("PC98"), a big-endian
//   "extension record" length word (0 for both TH01 title assets) followed
//   by that many bytes of position-override / embedded-colorkey records,
//   then (unless the palette flag's bit 7 is set) 48 raw R/G/B palette
//   bytes, then big-endian width/height words, all immediately followed by
//   the compressed pixel bitstream.
//
//   The pixel data is a line-oriented LZ scheme operating on 4-bit color
//   *pairs* (one byte per pixel, values 0-15, always processed 2 at a time):
//   each token first picks a back-reference distance (2 rows up, 1 row up
//   +-1 column, 1 row up, or "same distance as last time -> emit literals
//   instead", or "flat run" continuation), then either a fixed 1-word copy
//   or an Elias-gamma-coded run length, copying forward from that distance.
//   Literal color pairs are produced by a pair of chained move-to-front
//   caches (color()/color2() in the original) rather than raw bits, since
//   nearby pixels overwhelmingly reuse a small set of recently-seen colors.
//   Each cache entry is a full byte whose upper nibble is the actual 4-bit
//   color (maketbl() below seeds every entry as a multiple of 16); the
//   lower nibble is never meaningful, just along for the ride so cache
//   promotion can move whole bytes around.
//
// This only implements the direct-to-VRAM blit entry point (PiBlitL): it's
// the only one th01/formats/grp.cpp (and thus the OP.EXE closure) calls --
// PiLoadHeader/PiLoadPlanes (load-to-main-memory) and the near/compact model
// variants are unused here and intentionally not provided.

#include "port/compat/borland.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "libs/piloadc/piloadm.hpp"
#include "pc98emu/vram.hpp"

namespace {

// MSB-first bit reader over an in-memory byte buffer (mirrors piloadm.asm's
// dl/dh bit-cache + bx buffer pointer, minus the chunked DOS refill -- we
// just slurp the whole remaining file up front instead).
class BitReader {
public:
	explicit BitReader(const std::vector<uint8_t> &data) : data_(data) {}

	int bit() {
		if(bit_count_ == 0) {
			cur_byte_ = (pos_ < data_.size()) ? data_[pos_++] : 0;
			bit_count_ = 8;
		}
		bit_count_--;
		int b = (cur_byte_ >> 7) & 1;
		cur_byte_ = static_cast<uint8_t>(cur_byte_ << 1);
		return b;
	}

	uint8_t byte() {
		return (pos_ < data_.size()) ? data_[pos_++] : 0;
	}

	// Header fields wider than a byte are stored big-endian in the file
	// (read via the original's `lodsw; xchg ah,al` idiom).
	uint16_t word_be() {
		uint8_t b0 = byte();
		uint8_t b1 = byte();
		return static_cast<uint16_t>((b0 << 8) | b1);
	}

	void skip(int n) {
		for(int i = 0; i < n; i++) {
			byte();
		}
	}

	size_t pos() const { return pos_; }

private:
	const std::vector<uint8_t> &data_;
	size_t pos_ = 0;
	uint8_t cur_byte_ = 0;
	int bit_count_ = 0;
};

// One move-to-front cache lookup+promotion step (piloadm.asm's color() and
// color2() are two calls to this same logic against the same 256-entry
// table, chained: color2's seed is color()'s result). `mtf` is seeded by
// mtf_init() below and evolves as the image decodes.
uint8_t mtf_decode_stage(uint8_t *mtf, BitReader &br, int seed) {
	int si = seed;
	if(br.bit() == 1) {
		if(br.bit() == 1) {
			// Rank 1: swap mtf[si] and mtf[si+1], return the promoted value.
			uint8_t a = mtf[si];
			uint8_t b = mtf[si + 1];
			mtf[si] = b;
			mtf[si + 1] = a;
			return b;
		}
		// Rank 0: already at the front.
		return mtf[si];
	}

	// Rank 2..15, encoded as a short unary(1-2 bit) prefix picking how many
	// suffix bits follow (1, 2, or 3), then that many suffix bits.
	int cx = 1;
	if(br.bit() == 1) {
		if(br.bit() == 1) {
			cx = (cx << 1) | br.bit();
		}
		cx = (cx << 1) | br.bit();
	}
	cx = (cx << 1) | br.bit();

	int s = si + cx;
	uint8_t value = mtf[s];
	// Move-to-front: shift mtf[s-cx+1 .. s] up by one (i.e. mtf[s-i] =
	// mtf[s-i-1] for i=0..cx-1), then drop `value` at mtf[si] (== s-cx).
	for(int i = 0; i < cx; i++) {
		mtf[s - i] = mtf[s - i - 1];
	}
	mtf[si] = value;
	return value;
}

// Decodes one literal pixel *pair* (color()/color2()): the second stage's
// seed chains from the first stage's result.
uint16_t pi_color(uint8_t *mtf, BitReader &br, uint8_t seed) {
	uint8_t lo = mtf_decode_stage(mtf, br, seed);
	uint8_t hi = mtf_decode_stage(mtf, br, lo);
	return static_cast<uint16_t>(lo | (hi << 8));
}

// Seeds the 256-entry move-to-front cache with the same pattern
// piloadm.asm's maketbl builds: 16 outer passes of 16 descending-by-16
// values, giving a fresh ranking (all entries multiples of 16) before
// anything's been promoted.
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

struct PiHeader {
	int x_wid = 0;
	int y_wid = 0;
	int vadr = 0;
	bool colorkey = false;
	uint8_t tcol = 0;
};

// Parses everything up to (and including) the palette, leaving `br`
// positioned at the start of the compressed pixel bitstream. Writes the
// 48-byte raw R/G/B palette to buf_aligned+0x100 (th01/formats/grp.cpp reads
// it from there, per PILOADC.DOC's documented layout). Returns false on a
// malformed/unsupported header.
bool pi_parse_header(
	BitReader &br, uint8_t *buf_aligned, int x_pos_in, int y_pos_in,
	int option, PiHeader &out
) {
	uint8_t m0 = br.byte();
	uint8_t m1 = br.byte();
	bool magic_pi = (m0 == 'i' && m1 == 'P');
	bool magic_nz = (m0 == 'Z' && m1 == 'N'); // ZUN's own "NZ" signature
	if(!magic_pi && !magic_nz) {
		return false;
	}

	// NUL-terminated comment.
	while(br.byte() != 0) {
	}

	uint8_t palette_flag = br.byte();
	br.word_be(); // dot ratio -- unused by this closure
	if(br.byte() != 4) {
		return false; // plane count: only 4-bpp images are supported
	}
	br.skip(4); // target-machine tag, e.g. "PC98"

	// Extension records: position overrides (code 1) and an embedded
	// colorkey suggestion (code 2), only honored when the caller didn't
	// already request one. Neither TH01 title asset uses these (both have
	// a zero-length extension region), but they cost little to support.
	uint16_t ext_len = br.word_be();
	int x_override = 0;
	int y_override = 0;
	out.colorkey = (option & 0x40) != 0;
	out.tcol = static_cast<uint8_t>((option >> 8) & 0xFF);
	if(ext_len != 0) {
		size_t ext_end = br.pos() + ext_len;
		while(br.pos() < ext_end) {
			uint8_t code = br.byte();
			if(code == 1) {
				x_override = static_cast<int16_t>(br.word_be());
				y_override = static_cast<int16_t>(br.word_be());
			} else if(code == 2) {
				br.skip(3);
				uint8_t col = br.byte();
				if(!out.colorkey && out.tcol == 0xFF) {
					out.tcol = col;
					out.colorkey = true;
				}
			} else {
				break; // unknown record type: original also just bails out
			}
		}
	}

	int x_pos = (x_pos_in >= 0) ? x_pos_in : x_override;
	int y_pos = (y_pos_in >= 0) ? y_pos_in : y_override;
	out.vadr = (x_pos >> 3) + y_pos * pc98emu::ROW_SIZE;

	out.x_wid = br.word_be();
	out.y_wid = br.word_be();
	assert(((option & 0x80) == 0) &&
		"PiBlitL: line-skip (option bit 7) unused by th01op, not implemented");
	assert((out.x_wid % 8) == 0 && "PiBlitL: x_wid must be byte-aligned");

	if(!(palette_flag & 0x80)) {
		for(int i = 0; i < 48; i++) {
			buf_aligned[0x100 + i] = br.byte();
		}
	}

	return true;
}

} // namespace

int PiBlitL(
	void *buff, uint16_t bufsiz, int x, int y, int option,
	int (*read_func)(void *buf, unsigned size)
) {
	uint8_t *aligned = reinterpret_cast<uint8_t *>(
		(reinterpret_cast<uintptr_t>(buff) + 15) & ~static_cast<uintptr_t>(15));
	if(bufsiz < 0x130) {
		return PILOAD_ERR_BUFFER_TOO_SMALL;
	}

	// Slurp the whole remaining file: much simpler than replicating the
	// original's chunked DOS-buffer refill, and title-screen .GRP files are
	// only a few KiB.
	std::vector<uint8_t> data;
	{
		uint8_t chunk[65536];
		for(;;) {
			int n = read_func(chunk, sizeof(chunk));
			if(n <= 0) {
				break;
			}
			data.insert(data.end(), chunk, chunk + n);
			if(static_cast<size_t>(n) < sizeof(chunk)) {
				break;
			}
		}
	}

	BitReader br(data);
	PiHeader hdr;
	if(!pi_parse_header(br, aligned, x, y, option, hdr)) {
		return PILOAD_ERR_NO_PI;
	}

	const int x_wid = hdr.x_wid;
	const int y_wid = hdr.y_wid;
	const bool colorkey = hdr.colorkey;
	const uint8_t tcol = hdr.tcol;

	uint8_t mtf[256];
	mtf_init(mtf);

	const int bufbgn = 2 * x_wid;
	const int bufend = 6 * x_wid;
	const int line1 = -x_wid;
	const int line11 = -x_wid + 1;
	const int line2 = -2 * x_wid;
	const int line4 = 4 * x_wid;

	std::vector<uint8_t> gbuf(static_cast<size_t>(6 * x_wid), 0);

	int vadr = hdr.vadr;
	int rows_emitted = 0;

	// gtrans()'s VRAM sink, minus its bit-packing tricks: pack 8 pixel-index
	// bytes (0-15 each, in the upper nibble -- see file header comment) into
	// one byte per plane (B=bit0,R=bit1,G=bit2,E=bit3, MSB=leftmost dot),
	// matching pc98emu_render()'s decode convention. Honors GPF_COLORKEY-
	// style masking exactly like the old port/shim/piload_stub.cpp did.
	auto emit_rows = [&](int gbuf_off, int row_count) {
		for(int r = 0; r < row_count; r++) {
			const uint8_t *row = &gbuf[gbuf_off + r * x_wid];
			for(int col = 0; col < x_wid / 8; col++) {
				uint8_t b_bits = 0, r_bits = 0, g_bits = 0, e_bits = 0;
				uint8_t keep_mask = 0xFF;
				for(int bit = 0; bit < 8; bit++) {
					uint8_t v = static_cast<uint8_t>(row[col * 8 + bit] >> 4);
					uint8_t mask = static_cast<uint8_t>(0x80 >> bit);
					if(v & 1) b_bits |= mask;
					if(v & 2) r_bits |= mask;
					if(v & 4) g_bits |= mask;
					if(v & 8) e_bits |= mask;
					if(colorkey && v == tcol) {
						keep_mask = static_cast<uint8_t>(keep_mask & ~mask);
					}
				}
				int dst = vadr + col;
				VRAM_PLANE_B[dst] = static_cast<uint8_t>(
					(VRAM_PLANE_B[dst] & ~keep_mask) | (b_bits & keep_mask));
				VRAM_PLANE_R[dst] = static_cast<uint8_t>(
					(VRAM_PLANE_R[dst] & ~keep_mask) | (r_bits & keep_mask));
				VRAM_PLANE_G[dst] = static_cast<uint8_t>(
					(VRAM_PLANE_G[dst] & ~keep_mask) | (g_bits & keep_mask));
				VRAM_PLANE_E[dst] = static_cast<uint8_t>(
					(VRAM_PLANE_E[dst] & ~keep_mask) | (e_bits & keep_mask));
			}
			vadr += pc98emu::ROW_SIZE;
		}
	};

	// Flushes the lin=4 completed rows at [bufbgn,bufend), then slides the
	// last 2 rows down to the front as history for future back-references.
	// Returns true once all y_wid rows have been emitted.
	auto flush = [&]() -> bool {
		int rows_this_batch = std::min(4, y_wid - rows_emitted);
		if(rows_this_batch > 0) {
			emit_rows(bufbgn, rows_this_batch);
			rows_emitted += rows_this_batch;
		}
		std::memmove(&gbuf[0], &gbuf[bufend - 2 * x_wid],
			static_cast<size_t>(2 * x_wid));
		return rows_emitted >= y_wid;
	};

	// color(0) seeds the 2-row "phantom" pre-history with a single repeated
	// color pair, giving the first real rows something to reference.
	uint16_t seed_word = pi_color(mtf, br, 0);
	uint8_t seed_lo = static_cast<uint8_t>(seed_word & 0xFF);
	uint8_t seed_hi = static_cast<uint8_t>((seed_word >> 8) & 0xFF);
	for(int i = 0; i < 2 * x_wid; i += 2) {
		gbuf[i] = seed_lo;
		gbuf[i + 1] = seed_hi;
	}

	int di = bufbgn;
	int bp = 0;

	while(true) {
		int token_si;

		if(br.bit() == 1) {
			// pos2: a short (1-2 bit) fixed-distance code.
			if(br.bit() == 0) {
				token_si = line2;
			} else if(br.bit() == 0) {
				token_si = line11;
			} else {
				token_si = line11 - 2; // == -x_wid - 1
			}
		} else if(br.bit() == 1) {
			token_si = line1;
		} else if(gbuf[di - 1] == gbuf[di - 2]) {
			// Flat-run continuation (pos00): distinct "distance marker" from
			// the general -4 case below, even though both start at -4.
			if(-4 == bp) {
				goto nopres;
			}
			bp = -4;
			token_si = di - 2;
			goto bjmp;
		} else {
			token_si = -4;
		}

		if(token_si == bp) {
			goto nopres;
		}
		bp = token_si;
		token_si = di + token_si;

	bjmp:
		if(br.bit() == 0) {
			// Single-word (2-pixel) copy -- the cheapest, most common token.
			gbuf[di] = gbuf[token_si];
			gbuf[di + 1] = gbuf[token_si + 1];
			di += 2;
		} else {
			// Elias-gamma-ish run length (in words), unbounded.
			int len_words = 1;
			int prefix = 0;
			while(br.bit() == 1) {
				prefix++;
			}
			for(int i = 0; i <= prefix; i++) {
				len_words = (len_words << 1) | br.bit();
			}

			int src = token_si;
			for(;;) {
				int avail_words = (bufend - di) / 2;
				int take = std::min(len_words, avail_words);
				for(int i = 0; i < take; i++) {
					gbuf[di] = gbuf[src];
					gbuf[di + 1] = gbuf[src + 1];
					di += 2;
					src += 2;
				}
				len_words -= take;
				if(len_words == 0) {
					break;
				}
				if(flush()) {
					return PILOAD_ERR_OK;
				}
				src -= line4;
				di = bufbgn;
			}
		}

		if(di == bufend) {
			if(flush()) {
				return PILOAD_ERR_OK;
			}
			di = bufbgn;
		}
		continue;

	nopres:
		{
			uint8_t seed = gbuf[di - 1];
			for(;;) {
				uint16_t pair = pi_color(mtf, br, seed);
				gbuf[di] = static_cast<uint8_t>(pair & 0xFF);
				gbuf[di + 1] = static_cast<uint8_t>((pair >> 8) & 0xFF);
				di += 2;
				seed = static_cast<uint8_t>((pair >> 8) & 0xFF);
				if(di == bufend) {
					if(flush()) {
						return PILOAD_ERR_OK;
					}
					di = bufbgn;
				}
				if(br.bit() == 0) {
					bp = 0;
					break;
				}
			}
		}
	}
}
