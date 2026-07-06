// port/tests/mdt_synth.hpp
// -----------------------------------------------------------------------
// Test-only synthetic .MDT (MDRV2 compiled BGM) builder. Mirrors the binary
// layout port/snd/mdt_format.cpp parses (see mdt_format.hpp's header
// comment for the format's provenance), so tests can build minimal MDT
// images and feed them through the real parser/engine.

#ifndef PORT_TESTS_MDT_SYNTH_HPP
#define PORT_TESTS_MDT_SYNTH_HPP

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace mdt_synth {

inline void put_u16(std::vector<uint8_t> &v, size_t off, uint16_t val) {
	if(v.size() < off + 2) {
		v.resize(off + 2);
	}
	v[off] = static_cast<uint8_t>(val & 0xFF);
	v[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

inline void put_bytes(std::vector<uint8_t> &v, size_t off, std::initializer_list<uint8_t> bytes) {
	if(v.size() < off + bytes.size()) {
		v.resize(off + bytes.size());
	}
	size_t i = off;
	for(uint8_t b : bytes) {
		v[i++] = b;
	}
}

// A single channel table slot: `id` 0 means disabled. Per mdt_format.hpp's
// slot-position convention, slot index 0-2 are FM1-3 and 3-5 are SSG1-3;
// build() always emits exactly 6 slots (id 0x80-0x85 for the enabled ones,
// matching MDTParsingTools' CHANNEL_FLAGS "A".."F").
struct ChannelSpec {
	bool enabled = false;
	std::vector<uint8_t> opcodes; // this channel's opcode stream (no location prefix)
};

struct FmInstrumentSpec {
	uint8_t raw[32] = {0};
};

// Builds a complete synthetic .MDT image with exactly 6 channel slots
// (FM1-3, SSG1-3), one FM instrument table, one SSG envelope table (6
// zero-filled bytes each if `ssg_envelope_count` > 0), and the given title.
inline std::vector<uint8_t> build(
	const ChannelSpec channels[6],
	const std::vector<FmInstrumentSpec> &fm_instruments,
	int ssg_envelope_count,
	const std::string &title = "TEST"
) {
	std::vector<uint8_t> f(6);
	f[0] = 2;
	f[1] = 3; // unknown constant, always observed as 02 03
	put_u16(f, 2, 6);          // channel_count
	put_u16(f, 4, 1);          // chip = OPN

	size_t table_off = 6;
	size_t after_table = table_off + 6 * 4;
	size_t cursor = after_table + 6; // reserve fm/ssg/title location fields

	// Lay out each enabled channel's opcode stream, recording its location.
	uint16_t locations[6] = {0};
	for(int i = 0; i < 6; i++) {
		if(!channels[i].enabled) {
			continue;
		}
		locations[i] = static_cast<uint16_t>(cursor);
		if(f.size() < cursor + channels[i].opcodes.size()) {
			f.resize(cursor + channels[i].opcodes.size());
		}
		std::memcpy(f.data() + cursor, channels[i].opcodes.data(), channels[i].opcodes.size());
		cursor += channels[i].opcodes.size();
	}

	for(int i = 0; i < 6; i++) {
		put_u16(f, table_off + i * 4 + 0, locations[i]);
		put_u16(f, table_off + i * 4 + 2, channels[i].enabled ? static_cast<uint16_t>(0x80 + i) : 0);
	}

	// Title goes before the FM instrument table: mdt_parse() (matching the
	// real format) reads the SSG envelope table as running all the way to
	// true EOF, so nothing else can follow it in the file.
	uint16_t title_loc = static_cast<uint16_t>(cursor);
	std::string title_bytes = title + "$";
	if(f.size() < cursor + title_bytes.size()) {
		f.resize(cursor + title_bytes.size());
	}
	std::memcpy(f.data() + cursor, title_bytes.data(), title_bytes.size());
	cursor += title_bytes.size();

	uint16_t fm_def_loc = static_cast<uint16_t>(cursor);
	for(const auto &inst : fm_instruments) {
		if(f.size() < cursor + 32) {
			f.resize(cursor + 32);
		}
		std::memcpy(f.data() + cursor, inst.raw, 32);
		cursor += 32;
	}

	uint16_t ssg_def_loc = static_cast<uint16_t>(cursor);
	for(int i = 0; i < ssg_envelope_count; i++) {
		if(f.size() < cursor + 6) {
			f.resize(cursor + 6);
		}
		cursor += 6;
	}

	put_u16(f, after_table + 0, fm_def_loc);
	put_u16(f, after_table + 2, ssg_def_loc);
	put_u16(f, after_table + 4, title_loc);

	return f;
}

} // namespace mdt_synth

#endif // PORT_TESTS_MDT_SYNTH_HPP
