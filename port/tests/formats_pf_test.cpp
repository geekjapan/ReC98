// Unit test for th01/formats/pf.cpp (master.lib PAR packfile format, TH01
// variant): builds synthetic packfiles with pf_synth.hpp and loads them
// through the real arc_load()/arc_file_open()/arc_file_read()/
// arc_file_seek() implementation, in both uncompressed and RLE-compressed +
// "encrapted" (XORed) modes.

#include <cstdio>
#include <cstring>
#include <string>

#include <unistd.h>

#include "port/compat/borland.hpp"

#include "th01/formats/pf.hpp"
#include "pf_synth.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

std::string temp_dir() {
#ifdef PORT_TESTS_TMP_DIR
	return std::string(PORT_TESTS_TMP_DIR);
#else
	return std::string("/tmp");
#endif
}

void write_file(const std::string &path, const std::vector<uint8_t> &bytes) {
	std::FILE *f = std::fopen(path.c_str(), "wb");
	std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

} // namespace

int main() {
	if(chdir(temp_dir().c_str()) != 0) {
		std::fprintf(stderr, "FAIL: could not chdir to temp dir\n");
		return 1;
	}
	// arc_load()'s packfile name is stored/matched via a fixed-size
	// PF_FN_LEN (13, "8.3 + null") buffer -- a full temp-directory path
	// would silently truncate. Use a short 8.3-style name in the CWD.
	const std::string pf_path = "T.PAR";

	// -- Uncompressed entry -------------------------------------------------
	{
		std::vector<uint8_t> payload = {'H', 'E', 'L', 'L', 'O', ',', ' ', 'P',
		                                 'A', 'R', '!', 0, 1, 2, 3, 4, 5};
		pf_synth::Entry entry;
		entry.name = "TEST.BIN";
		entry.data = payload;
		entry.compressed = false;

		write_file(pf_path, pf_synth::build({entry}, /*arc_key=*/0));

		arc_key = 0;
		arc_load(pf_path.c_str());
		expect(arc_file_open("TEST.BIN") == false, "arc_file_open finds uncompressed entry");

		std::vector<uint8_t> readback(payload.size());
		size_t n = arc_file_read(readback.data(), readback.size());
		expect(n == payload.size(), "uncompressed: full read size");
		expect(readback == payload, "uncompressed: content roundtrips");

		arc_file_close();
		arc_free();
	}

	// -- Uncompressed entry with non-zero arc_key (the "encraption") -------
	{
		std::vector<uint8_t> payload = {10, 20, 30, 40, 50, 60, 70, 80};
		pf_synth::Entry entry;
		entry.name = "KEYED.BIN";
		entry.data = payload;
		entry.compressed = false;

		const uint8_t key = 0xA5;
		write_file(pf_path, pf_synth::build({entry}, key));

		arc_key = key;
		arc_load(pf_path.c_str());
		expect(arc_file_open("KEYED.BIN") == false, "arc_file_open finds keyed entry");

		std::vector<uint8_t> readback(payload.size());
		arc_file_read(readback.data(), readback.size());
		expect(readback == payload, "encrapted (XORed) content decodes back to plaintext");

		arc_file_close();
		arc_free();
		arc_key = 0;
	}

	// -- Seek within an uncompressed entry -----------------------------------
	{
		std::vector<uint8_t> payload;
		for(int i = 0; i < 64; i++) {
			payload.push_back(static_cast<uint8_t>(i));
		}
		pf_synth::Entry entry;
		entry.name = "SEEK.BIN";
		entry.data = payload;
		entry.compressed = false;

		write_file(pf_path, pf_synth::build({entry}, 0));

		arc_key = 0;
		arc_load(pf_path.c_str());
		arc_file_open("SEEK.BIN");

		arc_file_seek(10, SEEK_SET);
		uint8_t b;
		arc_file_read(&b, 1);
		expect(b == 10, "SEEK_SET repositions read");

		arc_file_seek(5, SEEK_CUR);
		arc_file_read(&b, 1);
		expect(b == 16, "SEEK_CUR repositions relative to current position");

		arc_file_close();
		arc_free();
	}

	// -- RLE-compressed entry -------------------------------------------------
	{
		std::vector<uint8_t> payload;
		for(int i = 0; i < 6; i++) payload.push_back('z'); // run of 6
		payload.push_back('y');
		payload.push_back('y'); // run of 2
		for(int i = 0; i < 300; i++) payload.push_back('w'); // long run > 257
		payload.push_back('a');
		payload.push_back('b');
		payload.push_back('c'); // literals, no repeats

		pf_synth::Entry entry;
		entry.name = "COMP.BIN";
		entry.data = payload;
		entry.compressed = true;

		write_file(pf_path, pf_synth::build({entry}, 0));

		arc_key = 0;
		arc_load(pf_path.c_str());
		expect(arc_file_open("COMP.BIN") == false, "arc_file_open finds compressed entry");

		std::vector<uint8_t> readback(payload.size());
		size_t n = arc_file_read(readback.data(), readback.size());
		expect(n == payload.size(), "compressed: full read size");
		expect(readback == payload, "RLE-compressed content decodes back correctly");

		arc_file_close();
		arc_free();
	}

	// -- RLE-compressed + encrapted entry together --------------------------
	{
		std::vector<uint8_t> payload;
		for(int i = 0; i < 10; i++) payload.push_back(0x42);
		payload.push_back(0x99);

		pf_synth::Entry entry;
		entry.name = "BOTH.BIN";
		entry.data = payload;
		entry.compressed = true;

		const uint8_t key = 0x3C;
		write_file(pf_path, pf_synth::build({entry}, key));

		arc_key = key;
		arc_load(pf_path.c_str());
		arc_file_open("BOTH.BIN");

		std::vector<uint8_t> readback(payload.size());
		arc_file_read(readback.data(), readback.size());
		expect(readback == payload, "RLE + encraption compose correctly");

		arc_file_close();
		arc_free();
		arc_key = 0;
	}

	std::remove(pf_path.c_str());

	if(g_failures == 0) {
		std::printf("formats_pf_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
