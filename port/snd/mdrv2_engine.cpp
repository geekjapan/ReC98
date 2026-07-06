// port/snd/mdrv2_engine.cpp
#include "port/snd/mdrv2_engine.hpp"

#include <algorithm>
#include <cmath>

namespace port::snd {

namespace {

// Standard equal-temperament tuning, o4 "a" = 440 Hz -- MDRV2's MML manual
// doesn't state a reference pitch, but this is the universal convention for
// PC-98-era MML drivers of this family (PMD included).
double note_frequency_hz(int octave, int semitone) {
	double semitone_from_a4 = (octave - 4) * 12.0 + (semitone - 9);
	return 440.0 * std::pow(2.0, semitone_from_a4 / 12.0);
}

// YM2203 F-Number formula (see port/audio/opn_chip.cpp's header comment for
// where this was empirically confirmed against ymfm's actual output, since
// the commonly-published OPN formula's exponent is off by one for ym2203
// specifically): fnum = freq * 144 * 2^(21-block) / clock.
uint32_t compute_fnum(double freq_hz, int block, uint32_t clock_hz) {
	double fnum = freq_hz * 144.0 * std::pow(2.0, 21 - block) / static_cast<double>(clock_hz);
	long rounded = std::lround(fnum);
	return static_cast<uint32_t>(std::clamp<long>(rounded, 0, 2047));
}

int8_t as_signed(uint8_t b) {
	return static_cast<int8_t>(b);
}

// Which physical TL register "slot" (p = 0-3, per mdt_format.hpp's "slot
// 0/1/2/3 = operator 1/3/2/4" register layout) is one of the algorithm's
// audible carrier outputs, one bit per slot. @V (MDRV2's per-channel volume)
// is a loudness control, so only carrier operators' TL should move with it
// -- attenuating a modulator changes timbre, not loudness. Sourced from
// ymfm_fm.ipp's s_algorithm_ops table (the actual chip model this engine
// drives), translated from that table's logical operator numbering (O1-O4)
// to physical slot numbering via the O1/O3/O2/O4 = slot0/1/2/3 mapping:
//   alg 0-3: only O4 (slot3) is a carrier (serial chain ending in O4)
//   alg 4:   O2+O4 (slot2, slot3)
//   alg 5-6: O2+O3+O4 (slot1, slot2, slot3)
//   alg 7:   O1+O2+O3+O4, i.e. all 4 slots (fully parallel)
constexpr uint8_t kFmCarrierSlotMask[8] = {
	0b1000, 0b1000, 0b1000, 0b1000, 0b1100, 0b1110, 0b1110, 0b1111,
};

} // namespace

Mdrv2Engine::Mdrv2Engine(uint32_t opn_clock_hz) : chip_(opn_clock_hz), clock_hz_(opn_clock_hz) {
	recompute_samples_per_tick();
}

void Mdrv2Engine::recompute_samples_per_tick() {
	double spt = seconds_per_tick() * chip_.sample_rate();
	samples_per_tick_ = static_cast<uint32_t>(std::max(1.0, std::round(spt)));
}

double Mdrv2Engine::seconds_per_tick() const {
	// Approximation: `t` is a quarter-note BPM value, and a quarter note is
	// 48 ticks (192 ticks/whole note, MDRV2's tick base -- see mdt_format.hpp).
	double bpm = std::max(1.0, tempo_bpm_);
	return 60.0 / (bpm * 48.0);
}

void Mdrv2Engine::load(MdtSong song) {
	song_ = std::move(song);
	channels_.clear();
	channels_.resize(song_.channel_table.size());
	chip_.reset();
	tempo_bpm_ = 120.0;
	recompute_samples_per_tick();
	samples_into_tick_ = 0;
	playing_ = false;
	ssg_mixer_ = 0x3F;

	for(size_t i = 0; i < song_.channel_table.size(); i++) {
		const MdtChannelSlot &slot = song_.channel_table[i];
		ChannelState &ch = channels_[i];
		ch.role = channel_role_for_slot(i, slot.id);
		if(ch.role == ChannelRole::Unsupported) {
			ch.active = false;
			ch.finished = true;
			continue;
		}
		ch.active = true;
		ch.pc = slot.location;
	}
	loaded_ = true;
	primed_ = false;
}

void Mdrv2Engine::load_and_play(MdtSong song) {
	load(std::move(song));
	play();
}

void Mdrv2Engine::play() {
	if(!loaded_) {
		return;
	}
	if(!primed_) {
		// Priming can key-on the first note (see apply_note()), so it must
		// not happen until playback actually starts -- otherwise
		// mdrv2_bgm_load() alone would already make sound.
		for(auto &ch : channels_) {
			if(ch.active && !ch.finished) {
				process_opcodes(ch);
			}
		}
		primed_ = true;
	}
	playing_ = true;
}

void Mdrv2Engine::stop() {
	playing_ = false;
	for(auto &ch : channels_) {
		key_off(ch);
	}
}

bool Mdrv2Engine::all_channels_finished() const {
	for(const auto &ch : channels_) {
		if(ch.active && !ch.finished) {
			return false;
		}
	}
	return true;
}

void Mdrv2Engine::render(int16_t *out, uint32_t frames) {
	if(!playing_) {
		chip_.render(out, frames);
		return;
	}
	uint32_t produced = 0;
	while(produced < frames) {
		uint32_t remaining_in_tick = samples_per_tick_ - samples_into_tick_;
		uint32_t chunk = std::min(remaining_in_tick, frames - produced);
		chip_.render(out + produced, chunk);
		produced += chunk;
		samples_into_tick_ += chunk;
		if(samples_into_tick_ >= samples_per_tick_) {
			samples_into_tick_ = 0;
			step_tick();
		}
	}
}

void Mdrv2Engine::step_tick() {
	for(auto &ch : channels_) {
		if(!ch.active || ch.finished) {
			continue;
		}
		if(ch.ticks_remaining > 0) {
			ch.ticks_remaining--;
		}
		if(ch.ticks_remaining <= 0) {
			process_opcodes(ch);
		}
	}
}

namespace {

uint8_t fetch_u8(MdtSong &song, uint32_t &pc) {
	if(pc >= song.data.size()) {
		return 0xFF; // treat as end-of-stream
	}
	return song.data[pc++];
}

int16_t fetch_i16(MdtSong &song, uint32_t &pc) {
	uint8_t lo = fetch_u8(song, pc);
	uint8_t hi = fetch_u8(song, pc);
	return static_cast<int16_t>(static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8));
}

} // namespace

// Forward-scans from `scan_pc` (just past a "|:" loop's ":" early-exit
// marker) to find the position right after that loop's matching ":|",
// tracking nested "|:"/":|" pairs. Used only when a loop is about to play
// its final repetition, so the "played on every iteration except the last"
// tail gets skipped. See mdt_format.hpp's opcode table comment.
static uint32_t find_pipe_loop_end(MdtSong &song, uint32_t scan_pc) {
	int depth = 0;
	while(scan_pc < song.data.size()) {
		uint8_t op = song.data[scan_pc++];
		if(op == 0xE0) {
			depth++;
			scan_pc += 1; // count byte
			continue;
		}
		if(op == 0xE2) {
			if(depth == 0) {
				return scan_pc;
			}
			depth--;
			continue;
		}
		int argb = mdt_opcode_arg_bytes(op);
		if(argb < 0) {
			return scan_pc; // malformed stream; bail defensively
		}
		scan_pc += static_cast<uint32_t>(argb);
	}
	return scan_pc;
}

// Same idea as find_pipe_loop_end(), for the bracket-colon ("[:"/":]"/"|")
// loop variant instead of the pipe-colon ("|:"/":"/":|") one. 0xF6/0xF7 carry
// extra bytes beyond a repeat count (a 2-byte pointer for 0xF6, 3 bytes for
// 0xF7) that MDTParsingTools' own author couldn't resolve the meaning of
// either (see mdt_decomp_rip.py's 0xF6/0xF7/0xF9 handlers, and
// mdrv2_engine.hpp's header comment) -- this engine ignores them and
// forward-scans instead, exactly like the pipe-colon variant already did.
static uint32_t find_bracket_colon_loop_end(MdtSong &song, uint32_t scan_pc) {
	int depth = 0;
	while(scan_pc < song.data.size()) {
		uint8_t op = song.data[scan_pc++];
		if(op == 0xF6) {
			depth++;
			scan_pc += 3; // count + 2-byte pointer, unresolved (see above)
			continue;
		}
		if(op == 0xF7) {
			if(depth == 0) {
				return scan_pc + 3; // this loop's own end marker's args
			}
			depth--;
			scan_pc += 3;
			continue;
		}
		int argb = mdt_opcode_arg_bytes(op);
		if(argb < 0) {
			return scan_pc; // malformed stream; bail defensively
		}
		scan_pc += static_cast<uint32_t>(argb);
	}
	return scan_pc;
}

void Mdrv2Engine::process_opcodes(ChannelState &ch) {
	while(true) {
		uint8_t op = fetch_u8(song_, ch.pc);

		if(op < 0x80) {
			uint8_t length = fetch_u8(song_, ch.pc);
			apply_note(ch, op, length);
			return;
		}

		switch(op) {
			case 0x90: { // rest
				uint8_t length = fetch_u8(song_, ch.pc);
				apply_rest(ch, length);
				return;
			}
			case 0x91: // tie
				ch.tie_pending = true;
				break;
			case 0xE0: { // loop start (|:)
				uint8_t count = fetch_u8(song_, ch.pc);
				ch.loop_stack.push_back({ch.pc, count, 0});
				break;
			}
			case 0xE1: { // loop early-exit mark (:)
				if(!ch.loop_stack.empty()) {
					LoopFrame &top = ch.loop_stack.back();
					if(top.done + 1 >= top.count) {
						ch.pc = find_pipe_loop_end(song_, ch.pc);
					}
				}
				break;
			}
			case 0xE2: { // loop end (:|)
				if(!ch.loop_stack.empty()) {
					LoopFrame top = ch.loop_stack.back();
					ch.loop_stack.pop_back();
					top.done++;
					if(top.done < top.count) {
						ch.pc = top.start_pc;
						ch.loop_stack.push_back(top);
					}
				}
				break;
			}
			case 0xE3: // force note-off
				key_off(ch);
				break;
			case 0xE4: { // loop start ([)
				uint8_t count = fetch_u8(song_, ch.pc);
				ch.loop_stack.push_back({ch.pc, count, 0});
				break;
			}
			case 0xE5: { // loop end (])
				if(!ch.loop_stack.empty()) {
					LoopFrame top = ch.loop_stack.back();
					ch.loop_stack.pop_back();
					top.done++;
					if(top.done < top.count) {
						ch.pc = top.start_pc;
						ch.loop_stack.push_back(top);
					}
				}
				break;
			}
			case 0xE6: // detune
				ch.detune = as_signed(fetch_u8(song_, ch.pc));
				break;
			case 0xE7: // transpose
				ch.transpose = as_signed(fetch_u8(song_, ch.pc));
				break;
			case 0xE8: // amplitude LFO (triangle) -- parsed, not applied
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				break;
			case 0xE9: { // tempo
				uint8_t tempo = fetch_u8(song_, ch.pc);
				tempo_bpm_ = std::max<uint8_t>(tempo, 1);
				recompute_samples_per_tick();
				break;
			}
			case 0xEA: // articulation (Q)
				ch.gate = std::clamp<uint8_t>(fetch_u8(song_, ch.pc), 1, 8);
				break;
			case 0xEB: { // instrument change / SSG noise+envelope select
				uint8_t inst = fetch_u8(song_, ch.pc);
				if(ch.role == ChannelRole::Ssg1 || ch.role == ChannelRole::Ssg2 || ch.role == ChannelRole::Ssg3) {
					// Bits 7/6 pack the tone/noise mixer mix (active-low, so
					// a clear bit means "enabled"); the low 6 bits are the
					// envelope index (unused -- see mdt_format.hpp's header
					// comment). Confirmed against MDTParsingTools'
					// mdt_decomp_rip.py 0xEB handling for SSG-role channels.
					ch.ssg_tone_enabled = !(inst & 0x40);
					ch.ssg_noise_enabled = !(inst & 0x80);
					ch.instrument = inst & 0x3F; // envelope index; envelope shape not modeled
				} else {
					ch.instrument = inst;
					int fm_channel = static_cast<int>(ch.role); // Fm1=0, Fm2=1, Fm3=2
					if(ch.instrument < song_.fm_instruments.size()) {
						write_instrument(fm_channel, song_.fm_instruments[ch.instrument]);
					}
				}
				break;
			}
			case 0xEC: // volume
				ch.volume = fetch_u8(song_, ch.pc);
				break;
			case 0xED: // pitch LFO (triangle) -- parsed, not applied
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				break;
			case 0xEE: // register move/copy -- parsed, not applied
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				break;
			case 0xEF: // FM LFO delay / SSG noise frequency -- parsed, not applied
				fetch_u8(song_, ch.pc);
				break;
			case 0xF0: // fade -- parsed, not applied
				fetch_u8(song_, ch.pc);
				break;
			case 0xF1: // pan -- parsed, not applied
				fetch_u8(song_, ch.pc);
				break;
			case 0xF2: { // portamento -- glide not modeled; play start pitch for the duration
				uint8_t start_note = fetch_u8(song_, ch.pc);
				uint8_t duration = fetch_u8(song_, ch.pc);
				fetch_i16(song_, ch.pc); // change amount -- unused
				apply_note(ch, start_note, duration);
				return;
			}
			case 0xF3: { // infinite loop marker
				int16_t rel = fetch_i16(song_, ch.pc);
				ch.loop_target = static_cast<int32_t>(ch.pc) + rel;
				break;
			}
			case 0xF4: // volume increase
				ch.volume = static_cast<uint8_t>(std::min<int>(127, ch.volume + fetch_u8(song_, ch.pc)));
				break;
			case 0xF5: // volume decrease
				ch.volume = static_cast<uint8_t>(std::max<int>(0, ch.volume - fetch_u8(song_, ch.pc)));
				break;
			case 0xF6: { // loop start ([:); see find_bracket_colon_loop_end()'s
			             // comment for what's unresolved about this opcode
				uint8_t count = fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc); // 2-byte pointer, unused (see above)
				fetch_u8(song_, ch.pc);
				ch.loop_stack.push_back({ch.pc, count, 0});
				break;
			}
			case 0xF7: { // loop end (:])
				fetch_u8(song_, ch.pc); // 3 bytes, unused (see above)
				fetch_u8(song_, ch.pc);
				fetch_u8(song_, ch.pc);
				if(!ch.loop_stack.empty()) {
					LoopFrame top = ch.loop_stack.back();
					ch.loop_stack.pop_back();
					top.done++;
					if(top.done < top.count) {
						ch.pc = top.start_pc;
						ch.loop_stack.push_back(top);
					}
				}
				break;
			}
			case 0xF8: // sync-work value entry -- parsed, not exposed yet
				fetch_u8(song_, ch.pc);
				break;
			case 0xF9: { // skip-to-end-of-loop mark on last iteration (bracket-colon)
				fetch_u8(song_, ch.pc); // 2-byte pointer, unused (see above)
				fetch_u8(song_, ch.pc);
				if(!ch.loop_stack.empty()) {
					LoopFrame &top = ch.loop_stack.back();
					if(top.done + 1 >= top.count) {
						ch.pc = find_bracket_colon_loop_end(song_, ch.pc);
					}
				}
				break;
			}
			case 0xFA: { // macro call
				uint16_t location = static_cast<uint16_t>(fetch_u8(song_, ch.pc)) |
				                     (static_cast<uint16_t>(fetch_u8(song_, ch.pc)) << 8);
				ch.call_stack.push_back(ch.pc);
				ch.pc = location;
				break;
			}
			case 0xFF: // end of channel/macro
				if(!ch.call_stack.empty()) {
					ch.pc = ch.call_stack.back();
					ch.call_stack.pop_back();
				} else if(ch.loop_target >= 0) {
					ch.pc = static_cast<uint32_t>(ch.loop_target);
				} else {
					ch.finished = true;
					key_off(ch);
					return;
				}
				break;
			default:
				// Reserved/unrecognized opcode: bail out rather than risk an
				// infinite loop on a corrupt or unsupported stream.
				ch.finished = true;
				key_off(ch);
				return;
		}
	}
}

void Mdrv2Engine::key_off(ChannelState &ch) {
	if(ch.role == ChannelRole::Fm1 || ch.role == ChannelRole::Fm2 || ch.role == ChannelRole::Fm3) {
		int fm_channel = static_cast<int>(ch.role);
		chip_.write_reg(0x28, static_cast<uint8_t>(fm_channel));
	} else if(ch.role == ChannelRole::Ssg1 || ch.role == ChannelRole::Ssg2 || ch.role == ChannelRole::Ssg3) {
		int ssg_channel = static_cast<int>(ch.role) - static_cast<int>(ChannelRole::Ssg1);
		chip_.write_reg(0x08 + ssg_channel, 0); // volume 0
	}
	ch.key_on = false;
}

void Mdrv2Engine::apply_rest(ChannelState &ch, uint8_t length_ticks) {
	key_off(ch);
	ch.tie_pending = false;
	ch.ticks_remaining = length_ticks > 0 ? length_ticks : 1;
}

void Mdrv2Engine::apply_note(ChannelState &ch, uint8_t note_byte, uint8_t length_ticks) {
	int octave = note_byte >> 4;
	int semitone = note_byte & 0x0F;
	if(semitone > 11) {
		// Reserved encoding (e.g. SSG octave-8 special case, unsupported --
		// see mdt_decomp_rip.py's own "0x80...0x8F are unused" TODO).
		apply_rest(ch, length_ticks);
		return;
	}

	int total_semitone = semitone + ch.transpose;
	int octave_adj = static_cast<int>(std::floor(total_semitone / 12.0));
	semitone = total_semitone - octave_adj * 12;
	octave = std::clamp(octave + octave_adj, 0, 7);

	bool tied = ch.tie_pending;
	ch.tie_pending = false;

	if(ch.role == ChannelRole::Fm1 || ch.role == ChannelRole::Fm2 || ch.role == ChannelRole::Fm3) {
		int fm_channel = static_cast<int>(ch.role);
		if(!tied) {
			chip_.write_reg(0x28, static_cast<uint8_t>(fm_channel)); // key off
			int16_t scratch[4];
			chip_.render(scratch, 4); // let the off state actually latch (see header comment)
		}
		write_fm_note(ch, fm_channel, octave, semitone);
		if(!tied) {
			chip_.write_reg(0x28, static_cast<uint8_t>(0xF0 | fm_channel)); // key on, all operators
		}
	} else if(ch.role == ChannelRole::Ssg1 || ch.role == ChannelRole::Ssg2 || ch.role == ChannelRole::Ssg3) {
		int ssg_channel = static_cast<int>(ch.role) - static_cast<int>(ChannelRole::Ssg1);
		write_ssg_note(ch, ssg_channel, octave, semitone);
	}
	ch.key_on = true;
	ch.ticks_remaining = length_ticks > 0 ? length_ticks : 1;
}

void Mdrv2Engine::write_fm_note(ChannelState &ch, int fm_channel, int octave, int semitone) {
	double freq = note_frequency_hz(octave, semitone);
	uint32_t fnum = compute_fnum(freq, octave, clock_hz_);
	fnum = static_cast<uint32_t>(std::clamp<int32_t>(static_cast<int32_t>(fnum) + ch.detune, 0, 2047));
	chip_.write_reg(0xA4 + fm_channel, static_cast<uint8_t>((octave << 3) | ((fnum >> 8) & 0x07)));
	chip_.write_reg(0xA0 + fm_channel, static_cast<uint8_t>(fnum & 0xFF));

	if(ch.instrument < song_.fm_instruments.size()) {
		// Re-apply volume attenuation on top of the instrument's base TLs
		// every time a note starts, since @V (0xEC) only updates ch.volume,
		// not the registers directly (see this function's caller). Only
		// carrier-operator TLs are attenuated (see kFmCarrierSlotMask) --
		// modulator TLs stay at the instrument's authored value.
		const MdtFmInstrument &inst = song_.fm_instruments[ch.instrument];
		uint8_t alg = inst.raw[4] & 0x07;
		uint8_t carrier_mask = kFmCarrierSlotMask[alg];
		uint8_t atten = static_cast<uint8_t>((127 - ch.volume) / 2);
		for(int p = 0; p < 4; p++) {
			int tl = inst.raw[10 + p];
			if(carrier_mask & (1 << p)) {
				tl += atten;
			}
			chip_.write_reg(static_cast<uint8_t>(0x40 + p * 4 + fm_channel),
			                 static_cast<uint8_t>(std::clamp(tl, 0, 127)));
		}
	}
}

void Mdrv2Engine::write_ssg_note(ChannelState &ch, int ssg_channel, int octave, int semitone) {
	double freq = note_frequency_hz(octave, semitone);
	// YM2203's SSG sub-unit runs off half the FM clock (see
	// ym2203::ssg_effective_clock()); standard AY-3-8910-family tone divider
	// is /16.
	double ssg_clock = clock_hz_ / 2.0;
	uint32_t period = static_cast<uint32_t>(std::clamp(std::lround(ssg_clock / (16.0 * freq)), 1L, 4095L));
	chip_.write_reg(static_cast<uint8_t>(0x00 + ssg_channel * 2), static_cast<uint8_t>(period & 0xFF));
	chip_.write_reg(static_cast<uint8_t>(0x01 + ssg_channel * 2), static_cast<uint8_t>((period >> 8) & 0x0F));
	// Mixer (register 0x07, active-low, shared by all 3 SSG channels): only
	// this channel's tone/noise bits are touched, per ch.ssg_tone_enabled/
	// ssg_noise_enabled (decoded from 0xEB -- see process_opcodes). SSG
	// hardware envelopes aren't modeled (see this file's header comment) --
	// flat volume 0-15 scaled down from MDRV2's 0-127 @V range.
	uint8_t tone_bit = static_cast<uint8_t>(1 << ssg_channel);
	uint8_t noise_bit = static_cast<uint8_t>(1 << (3 + ssg_channel));
	ssg_mixer_ = ch.ssg_tone_enabled ? (ssg_mixer_ & ~tone_bit) : (ssg_mixer_ | tone_bit);
	ssg_mixer_ = ch.ssg_noise_enabled ? (ssg_mixer_ & ~noise_bit) : (ssg_mixer_ | noise_bit);
	chip_.write_reg(0x07, ssg_mixer_);
	uint8_t vol = static_cast<uint8_t>(std::min<int>(15, ch.volume >> 3));
	chip_.write_reg(static_cast<uint8_t>(0x08 + ssg_channel), vol);
}

void Mdrv2Engine::write_instrument(int fm_channel, const MdtFmInstrument &inst) {
	for(int p = 0; p < 4; p++) {
		chip_.write_reg(static_cast<uint8_t>(0x30 + p * 4 + fm_channel), inst.raw[6 + p]);
		chip_.write_reg(static_cast<uint8_t>(0x40 + p * 4 + fm_channel), inst.raw[10 + p]);
		chip_.write_reg(static_cast<uint8_t>(0x50 + p * 4 + fm_channel), inst.raw[14 + p]);
		chip_.write_reg(static_cast<uint8_t>(0x60 + p * 4 + fm_channel), inst.raw[18 + p]);
		chip_.write_reg(static_cast<uint8_t>(0x70 + p * 4 + fm_channel), inst.raw[22 + p]);
		chip_.write_reg(static_cast<uint8_t>(0x80 + p * 4 + fm_channel), inst.raw[26 + p]);
	}
	uint8_t alg = inst.raw[4] & 0x07;
	uint8_t fl = (inst.raw[3] >> 3) & 0x07;
	chip_.write_reg(static_cast<uint8_t>(0xB0 + fm_channel), static_cast<uint8_t>((fl << 3) | alg));
}

} // namespace port::snd
