// port/snd/mdt_format.hpp
// -----------------------------------------------------------------------
// Parser for MDRV2's compiled .MDT music binary (the format mdrv2_bgm_load()
// hands to the real MDRV98 TSR via INT 0F2h function MSET). MDRV2 itself is
// undocumented at the byte level -- there's no ZUN/Regulus-authored spec for
// the compiled object format, only the MML source language (see
// https://en.touhouwiki.net/wiki/User:Mami/Music_Dev/Mdrv2/Md2cmd, the
// "Trident System" manual). The layout below is reverse-engineered from
// Lmocinemod's MDTParsingTools (https://github.com/Lmocinemod/MDTParsingTools,
// mdt_decomp_rip.py), itself based on HertzDevil's "MDRV2 MDT to MML
// unconverter" Lua script. This is research material, not vendored code: the
// structures and opcode table here are an independent reimplementation for
// direct hardware playback (register writes), not a port of that project's
// MML-decompilation logic.
//
// TH01 targets plain OPN (YM2203: FM*3 + SSG*3, no rhythm, no ADPCM) per the
// Trident System manual section 1.1 ("その他の音源では FM*3+SSG*3"). This
// parser only supports that configuration -- OPM/OPNA-rhythm/ADPCM chip IDs
// are rejected. See mdrv2_engine.hpp for known limitations in how the
// decoded opcodes are turned into audio (portamento, hardware LFO, and the
// bracket-colon loop variants are parsed for stream-sync but not audible).
//
// Binary layout:
//   [0..1]   unknown constant (always 0x02 0x03 in observed files)
//   [2..3]   u16 channel_count
//   [4..5]   u16 chip: 0=OPM, 1=OPN, 2=OPLL (only 1 is supported)
//   channel table: channel_count * (u16 location, u16 id), id==0 means the
//     slot is unused. For OPN, MDRV2's MML letter convention maps table
//     slots 0-2 to FM channels 1-3 and slots 3-5 to SSG channels 1-3 (see
//     ChannelRole below); ids 0x80-0x85 ("A".."F") are the only ones this
//     parser expects to see for an OPN-chip file.
//   [..+0]   u16 fm_instrument_table_location
//   [..+2]   u16 ssg_envelope_table_location
//   [..+4]   u16 title_location (Shift-JIS text, '$' (0x24) terminated)
//   fm_instrument_table: 32-byte blocks, one per FM instrument, running up
//     to ssg_envelope_table_location. Byte layout (see mdrv2_engine.cpp's
//     register-write code for how this is consumed):
//       [0]      SY/NOI (OPM/OPNA-only fields, unused for plain OPN)
//       [1]      SP (OPM LFO speed, unused for OPN)
//       [2]      AMD (OPM LFO amplitude depth, unused for OPN)
//       [3]      bits0-2 WF (unused), bits3-5 FL (feedback)
//       [4]      bits0-2 ALG (algorithm), bits6-7 PAN (unused for OPN)
//       [5]      bits0-3 AMS, bits4-6 PMS (unused for OPN, no AMS/PMS on 2203)
//       [6..9]   DT1/MUL per operator, physical hardware slot order
//                (slot 0/1/2/3 = operator 1/3/2/4) -- written verbatim to
//                YM2203 registers 0x30/0x34/0x38/0x3C + channel
//       [10..13] TL per operator (physical slot order) -> 0x40/0x44/0x48/0x4C
//       [14..17] AR/KS per operator (physical slot order) -> 0x50/0x54/0x58/0x5C
//       [18..21] DR/AM-on per operator (physical slot order) -> 0x60/.../0x6C
//       [22..25] SR per operator (physical slot order) -> 0x70/.../0x7C
//       [26..29] SL/RR per operator (physical slot order) -> 0x80/.../0x8C
//       [30]     bits0-6 PMD (OPM LFO depth, unused for OPN)
//       [31]     unused (per MDTParsingTools: doesn't round-trip, doesn't
//                 seem to affect playback either)
//   ssg_envelope_table: 6-byte blocks, running to EOF. Only raw[0]'s low
//     nibble (an initial volume 0-15) is currently consumed -- MDRV2's SSG
//     hardware-envelope semantics aren't reverse-engineered here, so
//     SSG channels play as flat-volume square waves. See mdrv2_engine's
//     header comment.
//   channel/macro opcode streams: start at each channel table entry's
//     `location` (and, for macros, at the location embedded in a macro-call
//     opcode). See mdt_format.cpp's kOpcodeArgBytes table for the full
//     opcode set.

#ifndef PORT_SND_MDT_FORMAT_HPP
#define PORT_SND_MDT_FORMAT_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace port::snd {

enum class MdtChip : uint16_t {
	Opm = 0,
	Opn = 1,
	Opll = 2,
};

struct MdtChannelSlot {
	uint16_t location;
	uint16_t id;
};

struct MdtFmInstrument {
	uint8_t raw[32];
};

struct MdtSsgEnvelope {
	uint8_t raw[6];
};

// A channel table slot's hardware role. TH01/OPN-only: slots 0-2 are the 3
// FM channels, slots 3-5 are the 3 SSG channels (see mdt_format.hpp's header
// comment on the MML-letter convention this assumes).
enum class ChannelRole {
	Fm1,
	Fm2,
	Fm3,
	Ssg1,
	Ssg2,
	Ssg3,
	Unsupported, // slot index >= 6, or an id this parser doesn't recognize
};

ChannelRole channel_role_for_slot(size_t slot_index, uint16_t id);

struct MdtSong {
	MdtChip chip = MdtChip::Opn;
	std::vector<MdtChannelSlot> channel_table;
	std::vector<MdtFmInstrument> fm_instruments;
	std::vector<MdtSsgEnvelope> ssg_envelopes;
	std::string title;

	// The full file image. Channel/macro opcode streams are decoded lazily
	// (during playback) by indexing directly into this buffer via the
	// `location` offsets above -- see mdrv2_engine.cpp.
	std::vector<uint8_t> data;
};

class MdtParseError : public std::runtime_error {
public:
	explicit MdtParseError(const std::string &msg) : std::runtime_error(msg) {}
};

// Parses a complete .MDT file image. Throws MdtParseError on truncated data,
// an out-of-range chip ID, or an unsupported (non-OPN) chip ID.
MdtSong mdt_parse(std::vector<uint8_t> bytes);

// Every recognized opcode byte (see mdrv2_engine.cpp for how each is
// interpreted) and the number of argument bytes immediately following it in
// the channel/macro opcode stream. Values 0x00-0x7F are notes (not literal
// opcodes); pass any of them to get the note-length argument's size (1).
// Returns -1 for unrecognized/reserved opcodes (0x80-0x8F, 0x92-0xDF, 0xFE).
int mdt_opcode_arg_bytes(uint8_t opcode);

} // namespace port::snd

#endif // PORT_SND_MDT_FORMAT_HPP
