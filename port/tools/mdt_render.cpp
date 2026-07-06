// port/tools/mdt_render.cpp
// -----------------------------------------------------------------------
// Offline MDRV2 song renderer: parses a .MDT file and renders it to PCM
// through the same Mdrv2Engine port/shim/snd_mdrv2.cpp drives at runtime,
// without needing an audio device -- lets a real .MDT be checked for
// crashes/silence and, optionally, dumped to a .wav for a human to listen
// to. See port/tests/mdt_real_songs_test.cpp for the ctest-wired smoke test
// that exercises the same rendering path across the whole real asset corpus
// (this tool is for a single file with a human-chosen output path, which
// ctest binaries can't take as CLI args for).
//
// Usage: mdt_render <file.mdt> [--wav out.wav] [--seconds N]
//   --wav out.wav   write the rendered PCM as a 16-bit mono WAV file
//   --seconds N     render at most N simulated seconds (default 60);
//                    rendering stops earlier if the song finishes first
//                    (see Mdrv2Engine::finished()) unless it uses MDRV2's
//                    infinite-loop marker, which never finishes

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "port/snd/mdrv2_engine.hpp"
#include "port/snd/mdt_format.hpp"

namespace {

bool read_file(const std::string &fn, std::vector<uint8_t> &out) {
	std::ifstream f(fn, std::ios::binary | std::ios::ate);
	if(!f) {
		return false;
	}
	std::streamsize size = f.tellg();
	if(size < 0) {
		return false;
	}
	f.seekg(0, std::ios::beg);
	out.resize(static_cast<size_t>(size));
	if(size > 0 && !f.read(reinterpret_cast<char *>(out.data()), size)) {
		return false;
	}
	return true;
}

// Minimal 16-bit mono PCM WAV writer -- just enough of the RIFF/WAVE format
// for any media player to open the file; no exotic chunks.
void write_wav_mono16(const std::string &fn, const std::vector<int16_t> &pcm, uint32_t sample_rate) {
	std::ofstream f(fn, std::ios::binary);
	uint32_t data_bytes = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
	uint32_t byte_rate = sample_rate * 2;
	uint16_t block_align = 2;
	uint16_t bits_per_sample = 16;
	uint16_t num_channels = 1;
	uint32_t fmt_chunk_size = 16;
	uint32_t riff_size = 4 + (8 + fmt_chunk_size) + (8 + data_bytes);

	auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };

	f.write("RIFF", 4);
	w32(riff_size);
	f.write("WAVE", 4);
	f.write("fmt ", 4);
	w32(fmt_chunk_size);
	w16(1); // PCM
	w16(num_channels);
	w32(sample_rate);
	w32(byte_rate);
	w16(block_align);
	w16(bits_per_sample);
	f.write("data", 4);
	w32(data_bytes);
	if(!pcm.empty()) {
		f.write(reinterpret_cast<const char *>(pcm.data()), data_bytes);
	}
}

long sum_abs(const std::vector<int16_t> &buf) {
	long total = 0;
	for(int16_t s : buf) {
		total += std::abs(static_cast<int>(s));
	}
	return total;
}

// Mdrv2Engine::sample_rate() is ymfm's native OPN render rate (chip clock /
// 4, ~998 kHz -- see port/audio/opn_chip.hpp) -- valid in a WAV file's
// header, but real-time playback (port/audio/audio_out.cpp) always lets
// SDL3 resample it down to the output device's rate. Do the same here with
// simple linear interpolation so the dumped .wav is a normal, small,
// universally-playable file rather than a 998 kHz oddity.
std::vector<int16_t> resample_linear(const std::vector<int16_t> &src, uint32_t src_rate, uint32_t dst_rate) {
	if(src_rate == dst_rate || src.empty()) {
		return src;
	}
	size_t dst_len = static_cast<size_t>(
		static_cast<double>(src.size()) * dst_rate / src_rate
	);
	std::vector<int16_t> out(dst_len);
	double step = static_cast<double>(src_rate) / dst_rate;
	for(size_t i = 0; i < dst_len; i++) {
		double src_pos = i * step;
		size_t i0 = static_cast<size_t>(src_pos);
		size_t i1 = std::min(i0 + 1, src.size() - 1);
		double frac = src_pos - i0;
		out[i] = static_cast<int16_t>(src[i0] + (src[i1] - src[i0]) * frac);
	}
	return out;
}

} // namespace

int main(int argc, char **argv) {
	if(argc < 2) {
		std::fprintf(stderr, "usage: %s <file.mdt> [--wav out.wav] [--seconds N]\n", argv[0]);
		return 2;
	}
	std::string in_path = argv[1];
	std::string wav_path;
	double max_seconds = 60.0;
	for(int i = 2; i < argc; i++) {
		if(std::strcmp(argv[i], "--wav") == 0 && i + 1 < argc) {
			wav_path = argv[++i];
		} else if(std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
			max_seconds = std::atof(argv[++i]);
		}
	}

	std::vector<uint8_t> bytes;
	if(!read_file(in_path, bytes)) {
		std::fprintf(stderr, "mdt_render: could not read '%s'\n", in_path.c_str());
		return 1;
	}

	port::snd::MdtSong song;
	try {
		song = port::snd::mdt_parse(std::move(bytes));
	} catch(const port::snd::MdtParseError &e) {
		std::fprintf(stderr, "mdt_render: parse error: %s\n", e.what());
		return 1;
	}

	std::string title = song.title;
	port::snd::Mdrv2Engine engine;
	engine.load_and_play(std::move(song));

	uint32_t rate = engine.sample_rate();
	uint32_t max_frames = static_cast<uint32_t>(max_seconds * rate);
	const uint32_t chunk = 4096;
	std::vector<int16_t> pcm;
	pcm.reserve(max_frames);
	std::vector<int16_t> buf(chunk);

	uint32_t produced = 0;
	while(produced < max_frames && !engine.finished()) {
		uint32_t n = std::min(chunk, max_frames - produced);
		engine.render(buf.data(), n);
		pcm.insert(pcm.end(), buf.begin(), buf.begin() + n);
		produced += n;
	}

	long loudness = sum_abs(pcm);
	std::printf(
		"mdt_render: '%s' (\"%s\") -> %u frames (%.2fs) at %u Hz, finished=%s, sum_abs=%ld\n",
		in_path.c_str(), title.c_str(), produced, static_cast<double>(produced) / rate, rate,
		engine.finished() ? "true" : "false", loudness
	);

	if(!wav_path.empty()) {
		constexpr uint32_t kWavSampleRate = 44100;
		std::vector<int16_t> resampled = resample_linear(pcm, rate, kWavSampleRate);
		write_wav_mono16(wav_path, resampled, kWavSampleRate);
		std::printf("mdt_render: wrote '%s' (resampled %u Hz -> %u Hz)\n", wav_path.c_str(), rate,
		            kWavSampleRate);
	}

	return 0;
}
