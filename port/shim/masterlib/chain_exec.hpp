// port/shim/masterlib/chain_exec
// --------------------------------
// T21 (see /Users/geekjapan/dev/projects/touhou/PLAN.md's M5) real
// implementation of th01/core/initexit.cpp's game_switch_binary() ->
// entrypoint_exec() seam, for a port built as three separate executables
// (th01op/th01main/th01fuuin) instead of ReC98's single combined
// "debloated" binary (which switches entry points via th01/pc98/entry.cpp's
// execl() of *itself* with a different argv[1]).
//
// Design choice (documented per the task): real execv() of the sibling
// port binary, not in-process re-dispatch. th01op/th01main/th01fuuin are
// already separate CMake executables with separate main()s (one per
// original .EXE, matching real hardware's process boundaries) -- an
// in-process chain would mean either merging all three into one binary
// (a much larger, riskier restructuring of the existing M1-M4 CMake
// layout) or awkwardly calling main_op()/main_main()/main_cutscene() back
// to back inside a single process despite their file-scope statics and
// SDL host lifetimes never being designed to coexist. execv() replacing
// the process image is also the closer match to the original DOS
// architecture (a real process boundary, not a function call), and it's
// what lets `resident`/scoredat survival actually get *tested*: a
// leftover in-memory pointer surviving a function call proves nothing;
// surviving a real execv() proves the hand-off mechanism works.
//
// State hand-off: chain_exec() calls port::resdata_save_all_to_file()
// (port/shim/masterlib/resdata_persist.hpp) immediately before execv(),
// dumping every resdata_create()'d block (in practice, just resident_t --
// see th01/core/resident.cpp) to a flat file whose path travels via the
// TH01_PORT_STATE_FILE environment variable (inherited automatically by
// execv(), and by every subsequent hop of the chain). The next process's
// first resdata_exist()/resdata_create() call transparently loads it back
// in (resdata.cpp's maybe_load_state_file()) and removes the file.
//
// Sibling binary resolution: all three executables land in the same
// build output directory (port/CMakeLists.txt has no per-target
// RUNTIME_OUTPUT_DIRECTORY overrides), so chain_exec() just needs that one
// directory -- baked in at build time as PORT_CHAIN_BIN_DIR (same
// "absolute source/build-tree path baked into the binary" convention as
// pc98emu's PC98EMU_FONT_DIR; this project isn't packaged for distribution
// yet, see that target's CMakeLists.txt comment), overridable via the
// TH01_PORT_BIN_DIR environment variable for out-of-tree test setups.

#ifndef PORT_SHIM_MASTERLIB_CHAIN_EXEC_HPP
#define PORT_SHIM_MASTERLIB_CHAIN_EXEC_HPP

#include "th01/pc98/entry.hpp" // entrypoint_t

namespace port {

// Serializes all resident state to the TH01_PORT_STATE_FILE hand-off file
// and execv()s into the sibling port binary implementing `entrypoint`
// (EP_OP -> th01op, EP_MAIN -> th01main, EP_CUTSCENE -> th01fuuin),
// replacing the current process image. `self_name` is used only for
// diagnostic messages (e.g. "th01op", "th01main", "th01fuuin").
//
// Does not return on success. On failure (sibling binary missing, exec()
// error), logs a clear diagnostic to stderr and calls std::exit(1) -- this
// is a port-internal wiring problem (a build/deployment issue), not a
// original-game error path worth propagating differently.
[[noreturn]] void chain_exec(entrypoint_t entrypoint, const char *self_name);

} // namespace port

#endif // PORT_SHIM_MASTERLIB_CHAIN_EXEC_HPP
