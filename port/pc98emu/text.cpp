#include "pc98emu/text.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

#ifndef PC98EMU_FONT_DIR
#error "PC98EMU_FONT_DIR must be defined by the build (see port/CMakeLists.txt)"
#endif

namespace pc98emu {
tram_cell_t g_tram[TEXT_ROWS][TEXT_COLS];
} // namespace pc98emu

namespace {

// Row bits are stored MSB-first (bit7/bit15 = leftmost dot), matching both
// the vendored BDF files' native bit order and pc98emu_render()'s VRAM dot
// convention (vram.cpp: "bit7 = leftmost dot in the byte").
std::array<std::array<uint8_t, pc98emu::CELL_H>, 256> g_ank_glyph;

// Heap-allocated and deliberately never freed: several tests (see e.g.
// stage1_bringup_test.cpp's "quitter" thread) call std::exit() from a
// background thread the instant their director script finishes, while the
// main thread's game loop (still running -- main_main()'s stage loop never
// returns on its own) may be mid-frame inside glyph_row() below, reading
// this map. std::exit() runs static-storage-duration destructors on the
// calling thread without waiting for other threads to stop, so a
// stack/value member here would let the main thread's concurrent find()
// race a live ~unordered_map() teardown -- intermittent SIGSEGV inside the
// hash-map's own node-freeing code. A reference to a leaked heap object has
// no destructor to race against.
std::unordered_map<uint16_t, std::array<uint16_t, pc98emu::CELL_H>> &g_kanji_glyph =
	*new std::unordered_map<uint16_t, std::array<uint16_t, pc98emu::CELL_H>>();
bool g_fonts_loaded = false;
bool g_blink_visible = true;

// Minimal BDF (Glyph Bitmap Distribution Format) reader, just enough to
// pull ENCODING + the hex BITMAP rows out of the vendored Shinonome fonts.
// `sink` is called once per STARTCHAR/ENDCHAR block.
template <typename Sink>
void parse_bdf(const std::string &path, Sink &&sink) {
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if(!f) {
		std::fprintf(stderr, "pc98emu/text: failed to open font file %s\n", path.c_str());
		std::exit(1);
	}

	char line[256];
	int encoding = -1;
	bool in_bitmap = false;
	std::array<uint32_t, pc98emu::CELL_H> rows{};
	int row_count = 0;

	while(std::fgets(line, sizeof(line), f)) {
		if(!in_bitmap) {
			if(std::sscanf(line, "ENCODING %d", &encoding) == 1) {
				continue;
			}
			if(std::strncmp(line, "BITMAP", 6) == 0) {
				in_bitmap = true;
				row_count = 0;
			}
		} else if(std::strncmp(line, "ENDCHAR", 7) == 0) {
			in_bitmap = false;
			sink(encoding, rows, row_count);
		} else if(row_count < pc98emu::CELL_H) {
			rows[static_cast<size_t>(row_count++)] = static_cast<uint32_t>(std::strtoul(line, nullptr, 16));
		}
	}

	std::fclose(f);
}

void load_fonts() {
	if(g_fonts_loaded) {
		return;
	}

	parse_bdf(
		std::string(PC98EMU_FONT_DIR) + "/shinonome16_hankaku.bdf",
		[](int encoding, const std::array<uint32_t, pc98emu::CELL_H> &rows, int row_count) {
			if(encoding < 0 || encoding > 0xFF) {
				return;
			}
			for(int y = 0; y < pc98emu::CELL_H; y++) {
				uint32_t row = (y < row_count) ? rows[static_cast<size_t>(y)] : 0;
				g_ank_glyph[static_cast<size_t>(encoding)][static_cast<size_t>(y)] = static_cast<uint8_t>(row & 0xFF);
			}
		}
	);

	parse_bdf(
		std::string(PC98EMU_FONT_DIR) + "/shinonome16_kanji.bdf",
		[](int encoding, const std::array<uint32_t, pc98emu::CELL_H> &rows, int row_count) {
			if(encoding < 0 || encoding > 0xFFFF) {
				return;
			}
			std::array<uint16_t, pc98emu::CELL_H> glyph{};
			for(int y = 0; y < pc98emu::CELL_H; y++) {
				uint32_t row = (y < row_count) ? rows[static_cast<size_t>(y)] : 0;
				glyph[static_cast<size_t>(y)] = static_cast<uint16_t>(row & 0xFFFF);
			}
			g_kanji_glyph[static_cast<uint16_t>(encoding)] = glyph;
		}
	);

	g_fonts_loaded = true;
}

// Row `y`'s 8-dot pattern for the glyph occupying the given cell, MSB =
// leftmost dot. Unknown codepoints (not covered by the vendored font) fall
// back to an all-blank glyph rather than crashing -- TH01's actual text is
// covered by the JIS X0208 range this font implements in full, so this only
// matters for malformed/garbage codes.
uint8_t glyph_row(const pc98emu::tram_cell_t &cell, int y) {
	if(cell.half == pc98emu::glyph_half_t::NONE) {
		return (cell.code < g_ank_glyph.size()) ? g_ank_glyph[cell.code][static_cast<size_t>(y)] : 0;
	}
	auto it = g_kanji_glyph.find(cell.code);
	if(it == g_kanji_glyph.end()) {
		return 0;
	}
	uint16_t full = it->second[static_cast<size_t>(y)];
	return (cell.half == pc98emu::glyph_half_t::LEFT) ? static_cast<uint8_t>(full >> 8) : static_cast<uint8_t>(full & 0xFF);
}

} // namespace

void pc98emu_text_reset() {
	for(auto &row : pc98emu::g_tram) {
		for(auto &cell : row) {
			cell = pc98emu::tram_cell_t{};
		}
	}
	g_blink_visible = true;
}

void pc98emu_text_put_halfwidth(int col, int row, uint8_t ank_code, uint8_t atrb) {
	if(col < 0 || col >= pc98emu::TEXT_COLS || row < 0 || row >= pc98emu::TEXT_ROWS) {
		return;
	}
	pc98emu::g_tram[row][col] = {ank_code, atrb, pc98emu::glyph_half_t::NONE};
}

void pc98emu_text_put_fullwidth(int col, int row, uint16_t jis_code, uint8_t atrb) {
	if(col < 0 || (col + 1) >= pc98emu::TEXT_COLS || row < 0 || row >= pc98emu::TEXT_ROWS) {
		return;
	}
	pc98emu::g_tram[row][col] = {jis_code, atrb, pc98emu::glyph_half_t::LEFT};
	pc98emu::g_tram[row][col + 1] = {jis_code, atrb, pc98emu::glyph_half_t::RIGHT};
}

void pc98emu_text_set_blink_visible(bool visible) {
	g_blink_visible = visible;
}

void pc98emu_text_composite(uint32_t *out_rgba) {
	using namespace pc98emu;

	load_fonts();

	constexpr int out_stride = TEXT_COLS * CELL_W; // == pc98emu::RES_X (640)

	for(int row = 0; row < TEXT_ROWS; row++) {
		for(int col = 0; col < TEXT_COLS; col++) {
			const tram_cell_t &cell = g_tram[row][col];
			if(!(cell.atrb & ATRB_DISP)) {
				continue;
			}
			if((cell.atrb & ATRB_BLINK) && !g_blink_visible) {
				continue;
			}

			bool reverse = (cell.atrb & ATRB_REVERSE) != 0;
			uint8_t r = (cell.atrb & ATRB_RED) ? 0xFF : 0x00;
			uint8_t g = (cell.atrb & ATRB_GREEN) ? 0xFF : 0x00;
			uint8_t b = (cell.atrb & ATRB_BLUE) ? 0xFF : 0x00;
			uint32_t pixel = (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) | (static_cast<uint32_t>(b) << 8) | 0xFF;

			int px0 = col * CELL_W;
			int py0 = row * CELL_H;

			for(int y = 0; y < CELL_H; y++) {
				uint8_t bits = glyph_row(cell, y);
				if(reverse) {
					bits = static_cast<uint8_t>(~bits);
				}
				if((cell.atrb & ATRB_UNDERLINE) && y == (CELL_H - 2)) {
					bits = 0xFF; // one dot row above the descent, at the baseline
				}
				if(cell.atrb & ATRB_VLINE) {
					// Simplification: real hardware draws this at a fixed
					// screen column regardless of glyph half; here it's just
					// this cell's own leftmost dot, so a fullwidth glyph
					// gets one line per half. Rare enough in TH01 usage to
					// not be worth more precision without a hardware ref.
					bits |= 0x80;
				}

				int py = py0 + y;
				for(int bit = 0; bit < CELL_W; bit++) {
					if(!((bits >> (CELL_W - 1 - bit)) & 1)) {
						continue;
					}
					out_rgba[py * out_stride + px0 + bit] = pixel;
				}
			}
		}
	}
}

void pc98emu_font_ank_rows(uint8_t ank_code, uint8_t rows_out[pc98emu::CELL_H]) {
	load_fonts();
	for(int y = 0; y < pc98emu::CELL_H; y++) {
		rows_out[y] = g_ank_glyph[ank_code][static_cast<size_t>(y)];
	}
}

void pc98emu_font_kanji_rows(uint16_t jis_code, uint16_t rows_out[pc98emu::CELL_H]) {
	load_fonts();
	auto it = g_kanji_glyph.find(jis_code);
	for(int y = 0; y < pc98emu::CELL_H; y++) {
		uint16_t row = (it == g_kanji_glyph.end())
			? 0
			: it->second[static_cast<size_t>(y)];
		// th01/hardware/grppsafx.cpp's graph_putsa_fx() unconditionally
		// byte-swaps kanji rows (__rotr__(..., 8)) to undo the raw BIOS INT
		// 18h AH=14h memory layout (left-8-dots byte first in memory, which
		// becomes the low byte of a little-endian 16-bit read -- so bit15
		// isn't the leftmost dot until that swap happens). Our BDF-derived
		// rows are already MSB-first (bit15 = leftmost dot, see this file's
		// g_kanji_glyph comment), so pre-swap here to cancel that expected
		// rotation back out.
		rows_out[y] = static_cast<uint16_t>((row << 8) | (row >> 8));
	}
}
