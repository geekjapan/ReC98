// port/tests/mdrv2_engine_test.cpp
#include "port/snd/mdrv2_engine.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

#include "port/snd/mdt_format.hpp"
#include "port/tests/mdt_synth.hpp"

using namespace port::snd;

namespace {

mdt_synth::FmInstrumentSpec simple_fm_instrument() {
	mdt_synth::FmInstrumentSpec inst{};
	inst.raw[3] = 0; // fl=0
	inst.raw[4] = 0; // alg=0
	inst.raw[10] = 20;
	inst.raw[11] = 20;
	inst.raw[12] = 20;
	inst.raw[13] = 0; // op4 (carrier in alg 0): loud
	inst.raw[14] = inst.raw[15] = inst.raw[16] = inst.raw[17] = 0x1F; // fast AR
	inst.raw[18] = inst.raw[19] = inst.raw[20] = inst.raw[21] = 0x05;
	inst.raw[26] = inst.raw[27] = inst.raw[28] = inst.raw[29] = 0x0F;
	return inst;
}

long sum_abs(const std::vector<int16_t> &buf) {
	long total = 0;
	for(int16_t s : buf) {
		total += std::abs(static_cast<int>(s));
	}
	return total;
}

void test_finite_song_finishes() {
	mdt_synth::ChannelSpec channels[6] = {};
	channels[0].enabled = true;
	// One quarter note C4, then end -- no infinite-loop marker (0xF3).
	channels[0].opcodes = {0xEB, 0x00, 0x40, 48, 0xFF};

	auto bytes = mdt_synth::build(channels, {simple_fm_instrument()}, 0);
	MdtSong song = mdt_parse(bytes);

	Mdrv2Engine engine;
	engine.load_and_play(std::move(song));
	assert(!engine.finished());

	std::vector<int16_t> buf(engine.sample_rate() * 2); // a couple seconds' worth
	engine.render(buf.data(), buf.size());
	assert(engine.finished());
	assert(sum_abs(buf) > 0); // the note actually made sound before ending

	std::printf("test_finite_song_finishes: OK\n");
}

void test_infinite_loop_never_finishes() {
	mdt_synth::ChannelSpec channels[6] = {};
	channels[0].enabled = true;
	// loop marker (offset 0, i.e. loop right back to here), note, rest, end.
	channels[0].opcodes = {0xEB, 0x00, 0xF3, 0x00, 0x00, 0x40, 24, 0x90, 24, 0xFF};

	auto bytes = mdt_synth::build(channels, {simple_fm_instrument()}, 0);
	MdtSong song = mdt_parse(bytes);

	Mdrv2Engine engine;
	engine.load_and_play(std::move(song));

	std::vector<int16_t> buf(engine.sample_rate());
	for(int i = 0; i < 20; i++) {
		engine.render(buf.data(), buf.size());
		assert(!engine.finished());
	}
	std::printf("test_infinite_loop_never_finishes: OK\n");
}

void test_ssg_channel_produces_sound() {
	mdt_synth::ChannelSpec channels[6] = {};
	channels[3].enabled = true; // SSG1
	channels[3].opcodes = {0xEB, 0x00, 0x49, 96, 0xFF}; // A4, half note, then end

	auto bytes = mdt_synth::build(channels, {}, /*ssg_envelope_count=*/1);
	MdtSong song = mdt_parse(bytes);

	Mdrv2Engine engine;
	engine.load_and_play(std::move(song));

	std::vector<int16_t> buf(engine.sample_rate());
	engine.render(buf.data(), buf.size());
	assert(sum_abs(buf) > 0);
	std::printf("test_ssg_channel_produces_sound: OK\n");
}

void test_load_without_play_is_silent_until_play() {
	mdt_synth::ChannelSpec channels[6] = {};
	channels[0].enabled = true;
	channels[0].opcodes = {0xEB, 0x00, 0x40, 48, 0xFF};

	auto bytes = mdt_synth::build(channels, {simple_fm_instrument()}, 0);
	MdtSong song = mdt_parse(bytes);

	Mdrv2Engine engine;
	engine.load(std::move(song));

	std::vector<int16_t> buf(4096);
	engine.render(buf.data(), buf.size());
	for(int16_t s : buf) {
		assert(s == 0); // paused: no audio until play()
	}

	engine.play();
	std::vector<int16_t> buf2(engine.sample_rate());
	engine.render(buf2.data(), buf2.size());
	assert(sum_abs(buf2) > 0);

	std::printf("test_load_without_play_is_silent_until_play: OK\n");
}

void test_malformed_stream_does_not_hang() {
	mdt_synth::ChannelSpec channels[6] = {};
	channels[0].enabled = true;
	channels[0].opcodes = {0x85}; // reserved/unrecognized opcode

	auto bytes = mdt_synth::build(channels, {simple_fm_instrument()}, 0);
	MdtSong song = mdt_parse(bytes);

	Mdrv2Engine engine;
	engine.load_and_play(std::move(song));
	std::vector<int16_t> buf(4096);
	engine.render(buf.data(), buf.size()); // must return, not spin forever
	assert(engine.finished());
	std::printf("test_malformed_stream_does_not_hang: OK\n");
}

} // namespace

int main() {
	test_finite_song_finishes();
	test_infinite_loop_never_finishes();
	test_ssg_channel_produces_sound();
	test_load_without_play_is_silent_until_play();
	test_malformed_stream_does_not_hang();
	return 0;
}
