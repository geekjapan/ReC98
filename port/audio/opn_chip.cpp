// port/audio/opn_chip.cpp
#include "port/audio/opn_chip.hpp"

#include <algorithm>

#include "ymfm.h"
#include "ymfm_opn.h"

namespace port::audio {

struct OpnChip::Interface : ymfm::ymfm_interface {};

OpnChip::OpnChip(uint32_t clock_hz)
	: interface_(std::make_unique<Interface>())
	, chip_(std::make_unique<ymfm::ym2203>(*interface_))
	, clock_hz_(clock_hz) {
	chip_->reset();
}

OpnChip::~OpnChip() = default;

void OpnChip::write_reg(uint8_t reg, uint8_t value) {
	chip_->write(0, reg);   // address port
	chip_->write(1, value); // data port
}

void OpnChip::reset() {
	chip_->reset();
}

uint32_t OpnChip::sample_rate() const {
	return chip_->sample_rate(clock_hz_);
}

void OpnChip::render(int16_t *out, uint32_t frames) {
	for(uint32_t i = 0; i < frames; i++) {
		ymfm::ym2203::output_data sample;
		chip_->generate(&sample, 1);
		// sample.data[0] = mixed FM output; [1..3] = SSG channels A/B/C.
		// Real hardware sums these onto a shared analog bus; ymfm gives us
		// each source at full digital scale, so we halve the sum to leave
		// headroom rather than hard-clip when several channels peak at once.
		int32_t mix = sample.data[0] + sample.data[1] + sample.data[2] + sample.data[3];
		mix /= 2;
		out[i] = static_cast<int16_t>(std::clamp(mix, -32768, 32767));
	}
}

} // namespace port::audio
