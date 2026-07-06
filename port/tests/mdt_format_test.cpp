// port/tests/mdt_format_test.cpp
#include "port/snd/mdt_format.hpp"

#include <cassert>
#include <cstdio>

#include "port/tests/mdt_synth.hpp"

using namespace port::snd;

namespace {

void test_header_and_tables() {
	mdt_synth::ChannelSpec channels[6] = {};
	channels[0].enabled = true;
	channels[0].opcodes = {0xFF}; // empty FM1 track: just "end"
	channels[3].enabled = true;
	channels[3].opcodes = {0xFF}; // empty SSG1 track

	mdt_synth::FmInstrumentSpec inst{};
	inst.raw[4] = 3; // alg=3

	auto bytes = mdt_synth::build(channels, {inst}, /*ssg_envelope_count=*/2, "HELLO");
	MdtSong song = mdt_parse(bytes);

	assert(song.chip == MdtChip::Opn);
	assert(song.channel_table.size() == 6);
	assert(song.channel_table[0].id == 0x80);
	assert(song.channel_table[1].id == 0); // disabled slot
	assert(song.channel_table[3].id == 0x40);
	assert(song.fm_instruments.size() == 1);
	assert(song.fm_instruments[0].raw[4] == 3);
	assert(song.ssg_envelopes.size() == 2);
	assert(song.title == "HELLO");

	assert(channel_role_for_slot(0, song.channel_table[0].id) == ChannelRole::Fm1);
	assert(channel_role_for_slot(3, song.channel_table[3].id) == ChannelRole::Ssg1);
	assert(channel_role_for_slot(1, song.channel_table[1].id) == ChannelRole::Unsupported);
	assert(channel_role_for_slot(3, 0x83) == ChannelRole::Unsupported); // "D": FM-semantics id, no OPN hardware

	std::printf("test_header_and_tables: OK\n");
}

void test_rejects_non_opn_chip() {
	std::vector<uint8_t> bytes(6);
	bytes[0] = 2;
	bytes[1] = 3;
	mdt_synth::put_u16(bytes, 2, 0); // channel_count = 0
	mdt_synth::put_u16(bytes, 4, 0); // chip = OPM
	mdt_synth::put_u16(bytes, 6, 6); // fm_def_loc
	mdt_synth::put_u16(bytes, 8, 6); // ssg_def_loc
	mdt_synth::put_u16(bytes, 10, 6); // title_loc
	bytes.push_back('$');

	bool threw = false;
	try {
		mdt_parse(bytes);
	} catch(const MdtParseError &) {
		threw = true;
	}
	assert(threw);
	std::printf("test_rejects_non_opn_chip: OK\n");
}

void test_rejects_truncated_file() {
	std::vector<uint8_t> bytes = {1, 2, 3};
	bool threw = false;
	try {
		mdt_parse(bytes);
	} catch(const MdtParseError &) {
		threw = true;
	}
	assert(threw);
	std::printf("test_rejects_truncated_file: OK\n");
}

void test_opcode_arg_bytes_table() {
	assert(mdt_opcode_arg_bytes(0x00) == 1); // note
	assert(mdt_opcode_arg_bytes(0x7F) == 1); // note
	assert(mdt_opcode_arg_bytes(0x90) == 1); // rest
	assert(mdt_opcode_arg_bytes(0x91) == 0); // tie
	assert(mdt_opcode_arg_bytes(0xE0) == 1); // loop start |:
	assert(mdt_opcode_arg_bytes(0xF3) == 2); // infinite loop marker
	assert(mdt_opcode_arg_bytes(0xFA) == 2); // macro call
	assert(mdt_opcode_arg_bytes(0xFF) == 0); // end
	assert(mdt_opcode_arg_bytes(0x85) == -1); // reserved
	assert(mdt_opcode_arg_bytes(0xFE) == -1); // reserved
	std::printf("test_opcode_arg_bytes_table: OK\n");
}

} // namespace

int main() {
	test_header_and_tables();
	test_rejects_non_opn_chip();
	test_rejects_truncated_file();
	test_opcode_arg_bytes_table();
	return 0;
}
