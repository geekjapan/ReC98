// port/snd/mdt_format.cpp
#include "port/snd/mdt_format.hpp"

#include <cstring>

namespace port::snd {

namespace {

uint16_t read_u16(const std::vector<uint8_t> &d, size_t off) {
	if(off + 2 > d.size()) {
		throw MdtParseError("mdt_parse: truncated file (u16 read past end)");
	}
	return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}

} // namespace

ChannelRole channel_role_for_slot(size_t slot_index, uint16_t id) {
	if(id == 0) {
		return ChannelRole::Unsupported; // disabled slot
	}
	switch(slot_index) {
		case 0: return ChannelRole::Fm1;
		case 1: return ChannelRole::Fm2;
		case 2: return ChannelRole::Fm3;
		case 3: return ChannelRole::Ssg1;
		case 4: return ChannelRole::Ssg2;
		case 5: return ChannelRole::Ssg3;
		default: return ChannelRole::Unsupported;
	}
}

int mdt_opcode_arg_bytes(uint8_t opcode) {
	if(opcode < 0x80) {
		return 1; // note: 1 length byte
	}
	switch(opcode) {
		case 0x90: return 1; // rest: length
		case 0x91: return 0; // tie
		case 0xE0: return 1; // loop start (|:): count
		case 0xE1: return 0; // loop early-exit mark (:)
		case 0xE2: return 0; // loop end (:|)
		case 0xE3: return 0; // force note-off
		case 0xE4: return 1; // loop start ([): count
		case 0xE5: return 0; // loop end (])
		case 0xE6: return 1; // detune
		case 0xE7: return 1; // transpose
		case 0xE8: return 4; // amplitude LFO (triangle)
		case 0xE9: return 1; // tempo
		case 0xEA: return 1; // articulation (Q)
		case 0xEB: return 1; // instrument / SSG noise+envelope select
		case 0xEC: return 1; // volume (FM/SSG; RHYTHM's extra bytes N/A -- OPN-only)
		case 0xED: return 4; // pitch LFO (triangle)
		case 0xEE: return 2; // register move/copy
		case 0xEF: return 1; // FM LFO delay / SSG noise frequency
		case 0xF0: return 1; // fade in/out
		case 0xF1: return 1; // pan (RHYTHM's extra byte N/A -- OPN-only)
		case 0xF2: return 4; // portamento
		case 0xF3: return 2; // infinite loop marker (signed relative offset)
		case 0xF4: return 1; // volume increase
		case 0xF5: return 1; // volume decrease
		case 0xF6: return 3; // loop start ([:): count + 2-byte pointer
		case 0xF7: return 3; // loop end (:])
		case 0xF8: return 1; // sync-work value entry
		case 0xF9: return 2; // skip-to-end-of-loop marker (|)
		case 0xFA: return 2; // macro call: 2-byte location
		case 0xFB: return 4; // pitch LFO (sawtooth)
		case 0xFC: return 4; // amplitude LFO (sawtooth)
		case 0xFD: return 4; // FM hardware LFO (triangle only)
		case 0xFF: return 0; // end of channel/macro
		default: return -1;  // 0x80-0x8F, 0x92-0xDF, 0xFE: reserved/unused
	}
}

MdtSong mdt_parse(std::vector<uint8_t> bytes) {
	MdtSong song;
	song.data = std::move(bytes);
	const std::vector<uint8_t> &d = song.data;

	if(d.size() < 6) {
		throw MdtParseError("mdt_parse: file too small for header");
	}

	// [0..1]: unknown constant, not interpreted.
	uint16_t channel_count = read_u16(d, 2);
	uint16_t chip_id = read_u16(d, 4);
	if(chip_id > 2) {
		throw MdtParseError("mdt_parse: unrecognized chip id (expected 0=OPM, 1=OPN, 2=OPLL)");
	}
	song.chip = static_cast<MdtChip>(chip_id);
	if(song.chip != MdtChip::Opn) {
		throw MdtParseError("mdt_parse: only OPN (chip id 1) is supported -- TH01's MDRV2 "
		                     "hardware is FM*3+SSG*3, no rhythm/ADPCM/OPM support");
	}

	size_t off = 6;
	song.channel_table.reserve(channel_count);
	for(uint16_t i = 0; i < channel_count; i++) {
		MdtChannelSlot slot;
		slot.location = read_u16(d, off);
		slot.id = read_u16(d, off + 2);
		off += 4;
		song.channel_table.push_back(slot);
	}

	uint16_t fm_def_loc = read_u16(d, off);
	uint16_t ssg_def_loc = read_u16(d, off + 2);
	uint16_t title_loc = read_u16(d, off + 4);

	if(fm_def_loc > ssg_def_loc) {
		throw MdtParseError("mdt_parse: FM instrument table location is after SSG envelope table");
	}
	if(static_cast<size_t>(ssg_def_loc) > d.size()) {
		throw MdtParseError("mdt_parse: SSG envelope table location past end of file");
	}

	size_t fm_bytes = static_cast<size_t>(ssg_def_loc) - fm_def_loc;
	if(fm_bytes % sizeof(MdtFmInstrument) != 0) {
		throw MdtParseError("mdt_parse: FM instrument table isn't a whole number of 32-byte entries");
	}
	size_t fm_count = fm_bytes / sizeof(MdtFmInstrument);
	song.fm_instruments.resize(fm_count);
	for(size_t i = 0; i < fm_count; i++) {
		size_t src = fm_def_loc + i * sizeof(MdtFmInstrument);
		if(src + sizeof(MdtFmInstrument) > d.size()) {
			throw MdtParseError("mdt_parse: FM instrument table truncated");
		}
		std::memcpy(song.fm_instruments[i].raw, d.data() + src, sizeof(MdtFmInstrument));
	}

	size_t ssg_bytes = d.size() - ssg_def_loc;
	size_t ssg_count = ssg_bytes / sizeof(MdtSsgEnvelope);
	song.ssg_envelopes.resize(ssg_count);
	for(size_t i = 0; i < ssg_count; i++) {
		size_t src = ssg_def_loc + i * sizeof(MdtSsgEnvelope);
		std::memcpy(song.ssg_envelopes[i].raw, d.data() + src, sizeof(MdtSsgEnvelope));
	}

	if(static_cast<size_t>(title_loc) > d.size()) {
		throw MdtParseError("mdt_parse: title location past end of file");
	}
	std::string title;
	size_t p = title_loc;
	while(p < d.size() && d[p] != '$') {
		title.push_back(static_cast<char>(d[p]));
		p++;
	}
	song.title = std::move(title);

	return song;
}

} // namespace port::snd
