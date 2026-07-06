// port/shim/fuuin_globals
// -----------------------
// Small things th01/fuuin_01.cpp's (FUUIN.EXE) closure needs that don't
// belong to any single component -- mirrors port/shim/op_globals.cpp's and
// port/shim/main_globals.cpp's rationale, adjusted for what th01fuuin_core
// actually compiles:
//
// 0. `entrypoint_exec()`: th01/core/initexit.cpp's game_switch_binary()
//    (reached from fuuin_01.cpp's main_cutscene() after the ending finishes)
//    calls this to restart the process at OP.EXE. T21 (see
//    /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) implements this for
//    real via port/shim/masterlib/chain_exec.hpp: an execv() into th01op,
//    replacing this process -- see that header for the design rationale.
//
// 1. `rank`: declared `extern` in th01/resident.hpp, defined in
//    th01/main_01.cpp (REIIDEN.EXE's own entry TU) -- but read/written
//    directly by fuuin_01.cpp's end_init() (`rank = resident->rank;`) and by
//    th01/end/end.cpp / th01/hiscore/regist.cpp (both compiled here), same
//    "each entry TU needs its own copy of the other two's globals" story as
//    th01/shim/op_globals.cpp's frame_rand/end_flag.
//
// 2. `mode_test`/`bomb_doubletap_frame`/`debug_mem()`/`debug_show_game()`:
//    th01/hardware/input.cpp (compiled here for its real input_sense()/
//    input_reset_sense()/input_up/input_down/input_lr/input_strike, which
//    th01/hiscore/regist.cpp's high-score name entry actually needs) also
//    references these four names from th01/main/debug.hpp and
//    th01/main/player/bomb.hpp -- real definitions live inline in
//    th01/main_01.cpp (REIIDEN.EXE's own entry TU, via
//    `#include "th01/main/player/bomb.cpp"` / `#include
//    "th01/main/debug.cpp"`), which th01fuuin_core doesn't compile. All four
//    are only reachable through input_sense()'s `(end_flag == ES_NONE) &&
//    mode_test` debug-menu branch; FUUIN.EXE always runs with
//    `end_flag != ES_NONE` (set in main_cutscene() before game_init()), so
//    that branch is unreachable at runtime here -- these are link-time
//    stubs only.
#include "th01/main/debug.hpp"
#include "th01/main/player/bomb.hpp"
#include "th01/pc98/entry.hpp"
#include "th01/rank.h"
#include "th01/resident.hpp"

#include "port/shim/masterlib/chain_exec.hpp"

uint8_t rank = RANK_NORMAL;
bool16 mode_test = false;
int bomb_doubletap_frame = 0;

void debug_mem(void)
{
}

void debug_show_game(void)
{
}

void entrypoint_exec(entrypoint_t entrypoint)
{
	port::chain_exec(entrypoint, "th01fuuin");
}
