// port/app/reiiden_bootstrap
// ---------------------------
// Shared setup both port/app/main_reiiden.cpp (the th01main executable) and
// port/tests/stage1_bringup_test.cpp (the headless M3 gameplay-verification
// test) need before calling th01/main_01.cpp's main_main():
//
// - ARC_FN (th01/shiftjis/fns.hpp) is a raw Shift-JIS byte string; the real
//   asset file on disk is named with the equivalent UTF-8 bytes (see
//   port/compat/sjis_filename.hpp) -- reiiden_arc_fn_utf8() is the shared
//   conversion, used as the assets-directory canary by both callers.
//
// - On real hardware, REIIDEN.EXE is only ever exec()'d by OP.EXE's
//   start_game() (th01/op_01.cpp), which allocates and seeds the resident
//   structure (rank/lives/route/stage_id/etc.) before the exec chain reaches
//   here. This port's th01main is a standalone process that bypasses OP.EXE
//   entirely (see main_reiiden.cpp's header comment), so resident_get()
//   would always see a never-allocated structure and main_main() would
//   immediately bail out with ERROR_RESIDENT_INVALID -- there's no OP.EXE
//   process in this port that could have seeded it first.
//   reiiden_seed_fresh_resident() replicates start_game(true)'s field-by-
//   field seeding (see that function's own comment for the exact list this
//   mirrors), giving standalone launches the same "fresh new game, from
//   stage 1" starting state OP.EXE's START menu item would produce. Defaults
//   match th01/formats/cfg.hpp's CFG_*_DEFAULT, the same values a
//   never-before-run REIIDEN.CFG would resolve to.

#ifndef PORT_APP_REIIDEN_BOOTSTRAP_HPP
#define PORT_APP_REIIDEN_BOOTSTRAP_HPP

#include "port/compat/borland.hpp" // __seg (needed by th01/resident.hpp)

#include "port/compat/sjis_filename.hpp"

#include <string>

#include "th01/formats/cfg.hpp" // CFG_*_DEFAULT
#include "th01/resident.hpp" // resident_get_or_create()
#include "th01/shiftjis/fns.hpp" // ARC_FN

namespace port {

inline std::string reiiden_arc_fn_utf8() {
	return sjis_to_utf8(ARC_FN);
}

inline void reiiden_seed_fresh_resident() {
	resident_t __seg *r = resident_get_or_create();

	r->rank = CFG_RANK_DEFAULT;
	r->bgm_mode = CFG_BGM_MODE_DEFAULT;
	r->rem_bombs = CFG_CREDIT_BOMBS_DEFAULT;
	r->credit_lives_extra = CFG_CREDIT_LIVES_EXTRA_DEFAULT;
	r->rand = 0;
	r->score = 0;
	r->debug_mode = DM_OFF;
	r->rem_lives = static_cast<int8_t>(CFG_CREDIT_LIVES_EXTRA_DEFAULT + 2);
	r->point_value = 0;
	r->route = ROUTE_MAKAI;
	r->stage_id = 0;
	for(int i = 0; i < SCENE_COUNT; i++) {
		r->continues_per_scene[i] = 0;
	}
	for(int i = 0; i < STAGES_PER_SCENE - 1; i++) {
		r->bonus_per_stage[i] = 0;
	}
	r->score_highest = 0;
	r->end_flag = ES_NONE;
	r->snd_need_init = true;
	r->pellet_speed = to_pellet_speed(-0.1);
}

} // namespace port

#endif // PORT_APP_REIIDEN_BOOTSTRAP_HPP
