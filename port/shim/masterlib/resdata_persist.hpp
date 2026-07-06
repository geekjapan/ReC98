// port/shim/masterlib/resdata_persist
// ------------------------------------
// Cross-*process* companion to resdata.cpp's in-*process* id -> block
// registry. T21's binary-switching chain (see port/shim/masterlib/
// chain_exec.hpp) replaces this port's single-process assumption (see
// resdata.cpp's own header comment) with three real, separate OS processes
// (th01op/th01main/th01fuuin) chained by execv() -- so whatever
// resdata_create() currently holds for RES_ID ("ReiidenConfig", th01/
// resident.hpp's resident_t) needs to survive that image replacement.
//
// resdata_save_all_to_file() dumps every in-use registry entry (id + byte
// size + raw bytes) to a flat file; the next process, on its own first
// resdata_exist()/resdata_create() call, transparently loads that same file
// back into its (otherwise-empty) registry before doing anything else --
// see resdata.cpp's maybe_load_state_file(). The file's path travels via
// the TH01_PORT_STATE_FILE environment variable, which chain_exec() also
// sets (if not already set) before execv(), so it's automatically inherited
// by every hop of the chain.

#ifndef PORT_SHIM_MASTERLIB_RESDATA_PERSIST_HPP
#define PORT_SHIM_MASTERLIB_RESDATA_PERSIST_HPP

namespace port {

// Writes every currently-registered resdata block to `path` (overwriting it).
// Returns false if the file couldn't be opened/written.
bool resdata_save_all_to_file(const char *path);

} // namespace port

#endif // PORT_SHIM_MASTERLIB_RESDATA_PERSIST_HPP
