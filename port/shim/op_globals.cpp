// port/shim/op_globals
// --------------------
// Small things th01/op_01.cpp's closure needs that don't belong to any
// single component:
//
// 0. `frame_rand`/`end_flag`: declared `extern` in th01/resident.hpp but
//    defined in th01/main_01.cpp and th01/fuuin_01.cpp respectively --
//    REIIDEN.EXE's and FUUIN.EXE's own entry TUs. On real hardware all three
//    entry points (op_01.cpp/main_01.cpp/fuuin_01.cpp) are object files in
//    the *same* combined "debloat" binary (see Tupfile.lua), so those
//    definitions are always present; th01op only builds the OP.EXE closure,
//    so it needs its own copies of the couple of shared globals op_01.cpp
//    (frame_rand) and th01/hardware/frmdelay.cpp (end_flag) still reach for.
//
// 1. `input_ok`/`input_shot`: declared `extern` in th01/hardware/input.hpp
//    and written by th01/hardware/input.cpp's input_sense(). op_01.cpp reads
//    them directly (see ok_shot_cancel_sense()'s `input_update_bool(input_ok,
//    ...)` etc.) but never calls input_sense() itself -- it senses BIOS
//    keygroups directly via int86(0x18) instead. Compiling the whole of
//    input.cpp would drag in th01/main/debug.hpp and th01/main/player/
//    bomb.hpp (full REIIDEN gameplay closure, out of scope for OP.EXE), so
//    this defines just the two globals it actually needs.
//
// 2. `entrypoint_exec()`: th01/core/initexit.cpp's game_switch_binary() (in
//    turn reachable from op_01.cpp's start_game(), the "begin a new game /
//    continue" menu action) calls this to restart the process at a
//    different entry point. T21 (see /Users/geekjapan/dev/projects/touhou/
//    PLAN.md's M5) implements this for real via port/shim/masterlib/
//    chain_exec.hpp: an execv() into the sibling th01main/th01fuuin
//    binary, replacing this process -- see that header for the design
//    rationale.
#include "th01/hardware/input.hpp"
#include "th01/pc98/entry.hpp"
#include "th01/resident.hpp"

#include "port/shim/masterlib/chain_exec.hpp"

unsigned long frame_rand = 0;
end_sequence_t end_flag = ES_NONE;

bool input_ok = false;
bool input_shot = false;

void entrypoint_exec(entrypoint_t entrypoint)
{
	port::chain_exec(entrypoint, "th01op");
}
