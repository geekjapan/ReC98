// port/audio/opn_chip.hpp
// -----------------------------------------------------------------------
// Thin wrapper around ymfm::ym2203 (the YM2203 / "OPN" FM+SSG sound chip
// used by PC-98's -26K/-86-class boards, and the target of TH01's MDRV2
// sound driver). Owns the chip instance, exposes a two-port register write
// (matching the real hardware's address/data port pair) and a mono PCM
// render call. See port/third_party/ymfm/README.md for what's vendored and
// why.

#ifndef PORT_AUDIO_OPN_CHIP_HPP
#define PORT_AUDIO_OPN_CHIP_HPP

#include <cstdint>
#include <memory>

namespace ymfm {
class ym2203;
class ymfm_interface;
} // namespace ymfm

namespace port::audio {

// PC-9801-26/-86 class boards clock the YM2203 at 3.9936 MHz.
constexpr uint32_t kOpnClockHz = 3993600;

class OpnChip {
public:
	explicit OpnChip(uint32_t clock_hz = kOpnClockHz);
	~OpnChip();

	OpnChip(const OpnChip &) = delete;
	OpnChip &operator=(const OpnChip &) = delete;

	// Writes `value` to OPN register `reg` (the driver-side equivalent of
	// writing the address port then the data port on real hardware).
	void write_reg(uint8_t reg, uint8_t value);

	void reset();

	// Native output sample rate for this chip/clock combination -- ymfm
	// picks this internally (input_clock / 4 at max fidelity); render()
	// always produces audio at this rate, and downstream code (AudioOut)
	// is responsible for resampling to the output device's rate.
	uint32_t sample_rate() const;

	// Renders `frames` mono samples (sum of the FM mix output and the 3 SSG
	// channel outputs, clamped to int16 range) into `out`.
	void render(int16_t *out, uint32_t frames);

private:
	struct Interface;
	std::unique_ptr<Interface> interface_;
	std::unique_ptr<ymfm::ym2203> chip_;
	uint32_t clock_hz_;
};

} // namespace port::audio

#endif // PORT_AUDIO_OPN_CHIP_HPP
