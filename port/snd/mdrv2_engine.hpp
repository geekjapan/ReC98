// port/snd/mdrv2_engine.hpp
// -----------------------------------------------------------------------
// MDRV2 driver semantics reimplementation: a per-channel opcode-stream VM
// (see mdt_format.hpp) that drives a port::audio::OpnChip (ymfm YM2203) in
// real time, standing in for the real MDRV98.COM TSR that
// th01/snd/mdrv2.cpp used to INT 0F2h into.
//
// Known limitations (consistent with MDTParsingTools' own -- see
// mdt_format.hpp's header comment for provenance): MDRV2's exact hardware
// timer -> tempo formula isn't reverse-engineered, so tempo is approximated
// as `t` meaning quarter-note BPM directly; portamento, the hardware
// LFO opcodes (S/SA/SP/SH), pan, fade, and the bracket-colon loop variants
// ([::]/:]/|) are parsed (so the opcode stream stays in sync) but don't
// affect audio; SSG channels play as flat-volume square waves (no envelope
// unit support); FM channel volume (@V) is approximated as a uniform
// attenuation added to every operator's TL rather than only the algorithm's
// carrier operator(s).
#ifndef PORT_SND_MDRV2_ENGINE_HPP
#define PORT_SND_MDRV2_ENGINE_HPP

#include <cstdint>
#include <vector>

#include "port/audio/opn_chip.hpp"
#include "port/snd/mdt_format.hpp"

namespace port::snd {

class Mdrv2Engine {
public:
	explicit Mdrv2Engine(uint32_t opn_clock_hz = port::audio::kOpnClockHz);

	// Loads a parsed song, priming every channel's first event, but leaves
	// playback paused (as if mdrv2_bgm_load() were called without a
	// following mdrv2_bgm_play()). Safe to call while another song is
	// already loaded/playing (replaces it).
	void load(MdtSong song);

	// Loads a song and immediately starts playback -- equivalent to
	// load() + play(). Mainly useful for tests.
	void load_and_play(MdtSong song);

	// Resumes/starts playback of the most recently loaded song. No-op if
	// nothing has been loaded yet.
	void play();

	void stop();

	// True once every channel has hit its end with no infinite-loop marker
	// (i.e. the song played through and won't produce any more sound).
	// A song using MDRV2's "\" infinite-loop marker never reports true.
	bool finished() const { return !loaded_ || all_channels_finished(); }

	uint32_t sample_rate() const { return chip_.sample_rate(); }

	// Renders `frames` mono samples, ticking the sequencer as needed.
	// Safe to call with no song loaded (renders silence).
	void render(int16_t *out, uint32_t frames);

private:
	struct LoopFrame {
		uint32_t start_pc;
		uint8_t count;
		uint8_t done;
	};

	struct ChannelState {
		ChannelRole role = ChannelRole::Unsupported;
		bool active = false;
		uint32_t pc = 0;
		int32_t ticks_remaining = 0;
		bool finished = false;
		int32_t loop_target = -1;
		bool tie_pending = false;
		int8_t detune = 0;
		int8_t transpose = 0;
		uint8_t instrument = 0;
		uint8_t volume = 127; // FM: 0-127 "master" volume; SSG: scaled to 0-15 on write
		uint8_t gate = 8;     // articulation (Q): gate length in 8ths of the note
		bool key_on = false;
		std::vector<LoopFrame> loop_stack;
		std::vector<uint32_t> call_stack;
	};

	bool all_channels_finished() const;
	double seconds_per_tick() const;
	void recompute_samples_per_tick();

	// Advances one MDRV2 tick: decrements every active channel's note timer,
	// and for any channel that just hit 0, runs process_opcodes() to apply
	// the next timed event (note/rest/tie/portamento) and re-arm its timer.
	void step_tick();

	// Consumes zero-duration opcodes (instrument/volume/loop control/etc.)
	// until a timed opcode is applied (which sets ch.ticks_remaining and
	// returns) or the channel stream ends.
	void process_opcodes(ChannelState &ch);

	void apply_note(ChannelState &ch, uint8_t note_byte, uint8_t length_ticks);
	void apply_rest(ChannelState &ch, uint8_t length_ticks);
	void key_off(ChannelState &ch);
	void write_instrument(int fm_channel, const MdtFmInstrument &inst);
	void write_fm_note(ChannelState &ch, int fm_channel, int octave, int semitone);
	void write_ssg_note(ChannelState &ch, int ssg_channel, int octave, int semitone);

	port::audio::OpnChip chip_;
	uint32_t clock_hz_;
	MdtSong song_;
	std::vector<ChannelState> channels_;
	bool loaded_ = false;
	bool primed_ = false;
	bool playing_ = false;

	double tempo_bpm_ = 120.0;
	uint32_t samples_per_tick_ = 1;
	uint32_t samples_into_tick_ = 0;
};

} // namespace port::snd

#endif // PORT_SND_MDRV2_ENGINE_HPP
