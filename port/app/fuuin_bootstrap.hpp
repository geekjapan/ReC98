// port/app/fuuin_bootstrap
// -------------------------
// T21 (see /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) support for
// driving th01/fuuin_01.cpp's main_cutscene() through its *real*,
// resident-driven ending path instead of the hidden "t1"/"t2" ending-preview
// shortcut port/app/main_fuuin.cpp otherwise falls back to (see that file's
// header comment) when standalone and no resident structure exists.
//
// On real hardware, FUUIN.EXE is only ever exec()'d by REIIDEN.EXE's
// game_cleared handling (th01/main_01.cpp: `resident->end_flag =
// static_cast<end_sequence_t>(ES_MAKAI + route); ...
// game_switch_binary(EP_CUTSCENE);`) right after a full clear -- so
// th01/fuuin_01.cpp's end_init() (the `argv[1][0] != 't'` branch) expects a
// resident structure with a real cleared-game snapshot already in it. This
// header mirrors that shape directly (matching main_01.cpp's own field
// list at its game_cleared branch, plus the score/continues/rank fields
// end_init() itself copies out of resident_t), the same "replicate the
// upstream caller's seeding since there's no live upstream process in this
// port" approach port/app/reiiden_bootstrap.hpp already takes for
// REIIDEN.EXE.

#ifndef PORT_APP_FUUIN_BOOTSTRAP_HPP
#define PORT_APP_FUUIN_BOOTSTRAP_HPP

#include "port/compat/borland.hpp" // __seg (needed by th01/resident.hpp)

#include "th01/rank.h" // RANK_NORMAL
#include "th01/resident.hpp" // resident_get_or_create()

namespace port {

// Seeds a resident structure representing a just-cleared playthrough on
// `route`, with `continues_used` continues spent in total (0 selects the
// "flawless clear" end_good()/boss-slides ending; > 0 selects the shorter
// end_bad() ending -- see th01/end/end.cpp's `if(continues_total == 0)`
// branch) all attributed to the shrine stage, so
// fuuin_ending_test.cpp can pick either path deterministically.
inline void fuuin_seed_cleared_resident(route_t route, uint16_t continues_used) {
	resident_t __seg *r = resident_get_or_create();

	r->rank = RANK_NORMAL;
	r->end_flag = static_cast<end_sequence_t>(ES_MAKAI + route);
	r->score = 1234500;
	r->score_highest = 1500000;
	r->rand = 424242;
	r->credit_lives_extra = 1;

	r->continues_per_scene[0] = continues_used;
	for(int i = 1; i < SCENE_COUNT; i++) {
		r->continues_per_scene[i] = 0;
	}
}

} // namespace port

#endif // PORT_APP_FUUIN_BOOTSTRAP_HPP
