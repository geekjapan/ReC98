// port/tests/mdt_real_songs_test.cpp
// -----------------------------------------------------------------------
// Assets-gated smoke test: parses and renders EVERY real TH01 .MDT (BGM)
// file under TH01_ASSETS_DIR through the same Mdrv2Engine port/shim/
// snd_mdrv2.cpp drives at runtime, without needing an audio device -- see
// port/tools/mdt_render.cpp for the CLI dev-tool version of this same
// rendering path (used to dump a single song to .wav for a human to listen
// to). Confirms the engine doesn't crash/assert/hang and produces
// non-silent output across the whole real corpus, not just the synthetic
// fixtures in mdt_format_test.cpp/mdrv2_engine_test.cpp.
//
// Also attempts to parse any .MDE (sound-effect) files present and reports
// whether mdt_parse() (correctly) rejects them as a different, unsupported
// compiled format -- see mdt_format.hpp's header comment. SE playback
// itself is optional (port/shim/snd_mdrv2.cpp's header comment), so this is
// a documentation check, not a pass/fail gate.
//
// Skipped (not failed) when the real TH01 assets aren't available, matching
// title_screen_golden_test's convention.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <dirent.h>

#include "port/app/assets_dir.hpp"
#include "port/snd/mdrv2_engine.hpp"
#include "port/snd/mdt_format.hpp"

namespace {

constexpr const char *DEFAULT_ASSETS_DIR = "/Users/geekjapan/dev/projects/touhou/assets/th01";
constexpr int EXIT_SKIP = 77;

// Real songs run well under a minute; capping render length bounds this
// test's runtime even for a .MDT using MDRV2's infinite-loop marker (which
// never reports Mdrv2Engine::finished()).
constexpr uint32_t kMaxRenderSeconds = 20;

// INIT.MDT is legitimately near-silent: disassembly shows its only
// FM/SSG-role channel event is a single 96-tick rest on FM1, with every
// other supported channel just selecting an instrument and immediately
// hitting end-of-stream -- no channel ever plays a note. It isn't
// referenced anywhere in th01/'s decompiled source either, so it reads as a
// leftover instrument/patch-table utility bundled on the same disk as the
// real BGM files, not an actual song. Parsing/rendering it must still not
// crash, but silence from it isn't a bug.
bool expected_to_be_silent(const std::string &fn) {
	return fn == "INIT.MDT";
}

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

bool ends_with_ci(const std::string &s, const std::string &suffix) {
	if(s.size() < suffix.size()) {
		return false;
	}
	for(size_t i = 0; i < suffix.size(); i++) {
		if(std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i]))
		   != std::tolower(static_cast<unsigned char>(suffix[i]))) {
			return false;
		}
	}
	return true;
}

long sum_abs(const std::vector<int16_t> &buf) {
	long total = 0;
	for(int16_t s : buf) {
		total += std::abs(static_cast<int>(s));
	}
	return total;
}

// Renders `fn` (a full path) for up to kMaxRenderSeconds simulated seconds
// (or until Mdrv2Engine::finished()), returning the total sum-of-abs-sample
// "loudness" so the caller can assert non-silence. Throws MdtParseError up
// to the caller for files that don't parse.
long render_and_measure(const std::string &path, uint32_t *out_frames, uint32_t *out_rate) {
	std::vector<uint8_t> bytes;
	if(!read_file(path, bytes)) {
		throw port::snd::MdtParseError("could not read file");
	}
	port::snd::MdtSong song = port::snd::mdt_parse(std::move(bytes));

	port::snd::Mdrv2Engine engine;
	engine.load_and_play(std::move(song));

	uint32_t rate = engine.sample_rate();
	uint32_t max_frames = kMaxRenderSeconds * rate;
	const uint32_t chunk = 4096;
	std::vector<int16_t> buf(chunk);
	std::vector<int16_t> all;
	all.reserve(max_frames);

	uint32_t produced = 0;
	while(produced < max_frames && !engine.finished()) {
		uint32_t n = std::min(chunk, max_frames - produced);
		engine.render(buf.data(), n);
		all.insert(all.end(), buf.begin(), buf.begin() + n);
		produced += n;
	}

	*out_frames = produced;
	*out_rate = rate;
	return sum_abs(all);
}

} // namespace

int main() {
	std::string assets_dir = port::resolve_assets_dir(
		std::getenv("TH01_ASSETS_DIR"), DEFAULT_ASSETS_DIR, "REIMU.MDT"
	);
	if(!port::file_exists(assets_dir + "/REIMU.MDT")) {
		std::printf(
			"mdt_real_songs: skipping -- no real TH01 .MDT assets at '%s' "
			"(set TH01_ASSETS_DIR to enable this test)\n",
			assets_dir.c_str()
		);
		return EXIT_SKIP;
	}

	DIR *dir = ::opendir(assets_dir.c_str());
	if(!dir) {
		std::fprintf(stderr, "mdt_real_songs: could not open directory '%s'\n", assets_dir.c_str());
		return 1;
	}
	std::vector<std::string> mdt_files;
	std::vector<std::string> mde_files;
	struct dirent *entry;
	while((entry = ::readdir(dir)) != nullptr) {
		std::string name = entry->d_name;
		if(ends_with_ci(name, ".mdt")) {
			mdt_files.push_back(name);
		} else if(ends_with_ci(name, ".mde")) {
			mde_files.push_back(name);
		}
	}
	::closedir(dir);
	std::sort(mdt_files.begin(), mdt_files.end());
	std::sort(mde_files.begin(), mde_files.end());

	if(mdt_files.empty()) {
		std::fprintf(stderr, "mdt_real_songs: no .MDT files found in '%s'\n", assets_dir.c_str());
		return 1;
	}

	int failures = 0;
	for(const std::string &fn : mdt_files) {
		std::string path = assets_dir + "/" + fn;
		uint32_t frames = 0, rate = 1;
		long loudness = 0;
		bool crashed = false;
		try {
			loudness = render_and_measure(path, &frames, &rate);
		} catch(const port::snd::MdtParseError &e) {
			std::fprintf(stderr, "FAIL %s: parse error: %s\n", fn.c_str(), e.what());
			failures++;
			continue;
		}
		bool silent = loudness == 0;
		bool ok = !silent || expected_to_be_silent(fn);
		const char *status = !ok ? "FAIL" : (silent ? "QUIET" : "OK");
		std::printf(
			"%-5s %-16s frames=%u (%.1fs) sum_abs=%ld\n", status, fn.c_str(), frames,
			static_cast<double>(frames) / rate, loudness
		);
		if(!ok) {
			failures++;
		}
	}

	for(const std::string &fn : mde_files) {
		std::string path = assets_dir + "/" + fn;
		std::vector<uint8_t> bytes;
		if(!read_file(path, bytes)) {
			continue;
		}
		try {
			port::snd::mdt_parse(std::move(bytes));
			std::printf(
				"NOTE %-16s .MDE parsed as if it were a .MDT -- unexpected, investigate\n",
				fn.c_str()
			);
		} catch(const port::snd::MdtParseError &e) {
			std::printf(
				"NOTE %-16s .MDE correctly rejected by mdt_parse() (different compiled "
				"format, not reverse-engineered here): %s\n",
				fn.c_str(), e.what()
			);
		}
	}

	if(failures == 0) {
		std::printf(
			"mdt_real_songs: all %zu real .MDT files rendered non-silent, no crashes\n",
			mdt_files.size()
		);
	}
	return failures == 0 ? 0 : 1;
}
