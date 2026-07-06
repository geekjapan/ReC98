// Unit test for th01/formats/cfg.hpp (reiiden.cfg format). This header
// declares only data structures (cfg_t/cfg_options_t) -- op_01.cpp does the
// actual load/save inline, and that's out of T8's scope (see WORKFLOW.md's
// T9 op-bringup). This test just verifies the struct compiles and
// round-trips through a raw byte buffer the same way op_01.cpp's
// file_read/file_write(&cfg, sizeof(cfg)) calls would.

#include <cstdio>
#include <cstring>

#include "port/compat/borland.hpp"

#include "th01/formats/cfg.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

} // namespace

int main() {
	cfg_t original = {};
	std::memcpy(original.id, CFG_ID, sizeof(CFG_ID) - 1);
	original.opts.rank = RANK_HARD;
	original.opts.bgm_mode = BGM_MODE_MDRV2;
	original.opts.credit_bombs = 2;
	original.opts.credit_lives_extra = 3;

	uint8_t buf[sizeof(cfg_t)];
	std::memcpy(buf, &original, sizeof(cfg_t));

	cfg_t roundtripped;
	std::memcpy(&roundtripped, buf, sizeof(cfg_t));

	expect(std::memcmp(roundtripped.id, CFG_ID, sizeof(CFG_ID) - 1) == 0,
	       "cfg_t.id round-trips through a raw byte buffer");
	expect(roundtripped.opts.rank == RANK_HARD, "cfg_options_t.rank round-trips");
	expect(roundtripped.opts.bgm_mode == BGM_MODE_MDRV2, "cfg_options_t.bgm_mode round-trips");
	expect(roundtripped.opts.credit_bombs == 2, "cfg_options_t.credit_bombs round-trips");
	expect(roundtripped.opts.credit_lives_extra == 3, "cfg_options_t.credit_lives_extra round-trips");

	if(g_failures == 0) {
		std::printf("formats_cfg_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
