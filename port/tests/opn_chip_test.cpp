// port/tests/opn_chip_test.cpp
// Tone-playback smoke test for port/audio/opn_chip.hpp: writes FM registers
// directly (no MDT/sequencer involved) and asserts on the resulting PCM.
#include "port/audio/opn_chip.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using port::audio::OpnChip;

namespace {

// Sets up FM channel `ch` with a simple 1-operator-audible patch (algorithm
// 0, only the carrier operator loud) and keys on `fnum`/`block`.
void play_tone(OpnChip &chip, int ch, uint32_t fnum, int block) {
	chip.write_reg(static_cast<uint8_t>(0xB0 + ch), 0x00); // alg=0, fl=0
	chip.write_reg(static_cast<uint8_t>(0x30 + ch), 0x00);
	chip.write_reg(static_cast<uint8_t>(0x34 + ch), 0x00);
	chip.write_reg(static_cast<uint8_t>(0x38 + ch), 0x00);
	chip.write_reg(static_cast<uint8_t>(0x3C + ch), 0x00);
	chip.write_reg(static_cast<uint8_t>(0x40 + ch), 127); // op1 TL: silent
	chip.write_reg(static_cast<uint8_t>(0x44 + ch), 127); // op3 TL: silent
	chip.write_reg(static_cast<uint8_t>(0x48 + ch), 127); // op2 TL: silent
	chip.write_reg(static_cast<uint8_t>(0x4C + ch), 0);   // op4 TL: loud (carrier in alg 0)
	chip.write_reg(static_cast<uint8_t>(0x50 + ch), 0x1F);
	chip.write_reg(static_cast<uint8_t>(0x54 + ch), 0x1F);
	chip.write_reg(static_cast<uint8_t>(0x58 + ch), 0x1F);
	chip.write_reg(static_cast<uint8_t>(0x5C + ch), 0x1F);
	chip.write_reg(static_cast<uint8_t>(0xA4 + ch), static_cast<uint8_t>((block << 3) | ((fnum >> 8) & 0x07)));
	chip.write_reg(static_cast<uint8_t>(0xA0 + ch), static_cast<uint8_t>(fnum & 0xFF));
	chip.write_reg(0x28, static_cast<uint8_t>(0xF0 | ch)); // key on, all operators
}

long count_zero_crossings(const std::vector<int16_t> &buf, size_t start) {
	long zc = 0;
	for(size_t i = start + 1; i < buf.size(); i++) {
		if((buf[i - 1] < 0) != (buf[i] < 0)) {
			zc++;
		}
	}
	return zc;
}

} // namespace

int main() {
	OpnChip chip;
	assert(chip.sample_rate() > 0);

	// Silence: nothing keyed on yet.
	std::vector<int16_t> silent(4096);
	chip.render(silent.data(), silent.size());
	for(int16_t s : silent) {
		assert(s == 0);
	}

	// A keyed-on tone must produce nonzero, non-clipped-to-one-value audio.
	play_tone(chip, /*ch=*/0, /*fnum=*/1040, /*block=*/4);
	std::vector<int16_t> tone(200000);
	chip.render(tone.data(), tone.size());

	int16_t min_v = 32767, max_v = -32768;
	for(int16_t s : tone) {
		min_v = std::min(min_v, s);
		max_v = std::max(max_v, s);
	}
	assert(max_v > 500);  // audibly loud, not near-silent
	assert(min_v < -500);
	assert(max_v != min_v); // not a stuck/constant value

	// Doubling fnum (one octave up, roughly) must roughly double the zero
	// crossing rate over the same window -- a basic "pitch actually moves
	// with fnum" sanity check, without depending on the exact Hz mapping
	// (see mdrv2_engine.cpp's header comment on that formula).
	OpnChip chip_low;
	play_tone(chip_low, 0, 520, 4);
	std::vector<int16_t> low(200000);
	chip_low.render(low.data(), low.size());

	size_t settle = tone.size() / 5;
	long zc_high = count_zero_crossings(tone, settle);
	long zc_low = count_zero_crossings(low, settle);
	assert(zc_low > 0);
	double ratio = static_cast<double>(zc_high) / static_cast<double>(zc_low);
	assert(ratio > 1.5 && ratio < 2.5);

	std::printf("opn_chip_test: OK (zc_high=%ld zc_low=%ld ratio=%.2f)\n", zc_high, zc_low, ratio);
	return 0;
}
