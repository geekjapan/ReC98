// port/shim/main_globals
// -----------------------
// Small things th01/main_01.cpp's (REIIDEN.EXE) closure needs that don't
// belong to any single component -- mirrors port/shim/op_globals.cpp's
// rationale for th01op_core, adjusted for what th01main_core actually
// compiles:
//
// 0. `end_flag`: declared `extern` in th01/resident.hpp, written by
//    main_01.cpp's game logic (`resident->end_flag = ...`) and read by
//    th01/hardware/frmdelay.cpp's frame_delay() to pick FUUIN.EXE-specific
//    delay semantics, but its actual *storage* is defined in
//    th01/fuuin_01.cpp -- FUUIN.EXE's own entry TU (see OP_DEPS.md /
//    op_globals.cpp's identical note). th01main_core deliberately doesn't
//    build FUUIN.EXE's closure (that's T12/fuuin, a separate task), so it
//    needs its own copy of this global, same as th01op_core does.
//    (`frame_rand`, the other global op_globals.cpp had to supply, is
//    already defined directly in th01/main_01.cpp itself -- no shim needed
//    here.)
//
// 1. `score`: same story as `end_flag` -- declared `extern` in
//    th01/resident.hpp, but its one definition in the combined "debloat"
//    binary lives in th01/fuuin_01.cpp (with a FUUIN.EXE-specific initial
//    value of 100000 that doesn't apply here). main_main() overwrites it
//    from `resident->score` almost immediately, so the initial value here
//    doesn't matter.
//
// 2. `entrypoint_exec()`: th01/core/initexit.cpp's game_switch_binary() --
//    reached from several places in main_main() (returning to OP.EXE,
//    advancing to the next binary segment -- including a self-re-exec into
//    a fresh th01main process for a boss-adjacent stage_id transition,
//    matching the original multi-binary DOS architecture -- and jumping to
//    the cutscene player). T21 (see /Users/geekjapan/dev/projects/touhou/
//    PLAN.md's M5) implements this for real via port/shim/masterlib/
//    chain_exec.hpp: an execv() into the sibling binary, replacing this
//    process -- see that header for the design rationale.
#include "th01/pc98/entry.hpp"
#include "th01/resident.hpp"

#include "port/shim/masterlib/chain_exec.hpp"

end_sequence_t end_flag = ES_NONE;
uscore_t score = 0;

void entrypoint_exec(entrypoint_t entrypoint)
{
	port::chain_exec(entrypoint, "th01main");
}
