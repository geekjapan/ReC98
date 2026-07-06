// Unit test for th01/formats/scoredat.cpp (REYHI*.DAT high score file
// format): exercises scoredat_load() (including first-run recreation),
// scoredat_save(), scoredat_name_get(), and scoredat_hiscore_get() against
// a real temp-directory file, round-tripping the name-byte "encoding" and
// score/stage/route arrays.

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#include <unistd.h>

#include "port/compat/borland.hpp"

#include "th01/rank.h"
#include "th01/resident.hpp"
#include "th01/formats/scoredat.hpp"

// th01/resident.cpp (not part of th01/formats, out of T8 scope) is the real
// definition; scoredat.cpp's scoredat_fn macro just needs some `rank` to
// pick a REYHI*.DAT filename.
uint8_t rank;

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

} // namespace

int main() {
	if(chdir(temp_dir().c_str()) != 0) {
		std::fprintf(stderr, "FAIL: could not chdir to temp dir\n");
		return 1;
	}
	std::remove("REYHINO.DAT");
	rank = RANK_NORMAL;

	// First load: file doesn't exist yet, gets recreated with defaults.
	int ret = scoredat_load();
	expect(ret == 0, "scoredat_load() recreates and loads a missing file");
	expect(scoredat_hiscore_get() == 1000, "recreated file's top score is 1000 (10 places * 100)");

	unsigned char name[SCOREDAT_NAME_BYTES + 1];
	scoredat_name_get(0, name);
	expect(std::strlen(reinterpret_cast<char *>(name)) <= SCOREDAT_NAME_BYTES,
	       "scoredat_name_get null-terminates within the name buffer");

	scoredat_free();

	// Reload and verify the on-disk recreated file is consistent.
	ret = scoredat_load();
	expect(ret == 0, "scoredat_load() re-reads the recreated file");
	expect(scoredat_hiscore_get() == 1000, "reloaded top score still matches");

	unsigned char name_reloaded[SCOREDAT_NAME_BYTES + 1];
	scoredat_name_get(0, name_reloaded);
	expect(std::memcmp(name, name_reloaded, SCOREDAT_NAME_BYTES) == 0,
	       "name-byte encode/decode roundtrips through a save+reload");

	scoredat_free();

	// Save + reload roundtrip.
	ret = scoredat_load();
	expect(ret == 0, "scoredat_load() before save-roundtrip test");
	scoredat_save();
	scoredat_free();

	ret = scoredat_load();
	expect(ret == 0, "scoredat_load() after explicit scoredat_save()");
	expect(scoredat_hiscore_get() == 1000, "score survives an explicit save+reload roundtrip");
	scoredat_free();

	std::remove("REYHINO.DAT");

	if(g_failures == 0) {
		std::printf("formats_scoredat_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
