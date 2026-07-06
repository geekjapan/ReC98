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
//    advancing to the next binary segment, jumping to the cutscene player)
//    -- calls this to restart the process at a different entry point. Real
//    implementation needs th01/pc98/entry.cpp's DOS multi-entrypoint
//    dispatcher, which neither th01op nor th01main build (see
//    port/app/main_op.cpp's header comment). Every one of th01main's
//    reachable game_switch_binary() call sites is well past the "no
//    assets" bring-up scope (they all require a live playthrough to reach),
//    so this is a clearly-logged stub, identical in spirit to
//    op_globals.cpp's.
#include <cstdio>
#include <cstdlib>

#include "th01/pc98/entry.hpp"
#include "th01/resident.hpp"

end_sequence_t end_flag = ES_NONE;
uscore_t score = 0;

void entrypoint_exec(entrypoint_t entrypoint)
{
	std::fprintf(
		stderr,
		"th01main: entrypoint switch (%d) requested, but th01main only "
		"implements the REIIDEN.EXE gameplay closure -- exiting.\n",
		static_cast<int>(entrypoint)
	);
	std::exit(0);
}
