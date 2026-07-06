// port/shim/masterlib/chain_exec.cpp -- see chain_exec.hpp for the design
// rationale.

#include "port/shim/masterlib/chain_exec.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/types.h>
#include <unistd.h>

#include "port/shim/masterlib/resdata_persist.hpp"

#ifndef PORT_CHAIN_BIN_DIR
#error "PORT_CHAIN_BIN_DIR must be defined (see port/CMakeLists.txt's masterlib target)"
#endif

namespace {

const char *sibling_binary_name(entrypoint_t entrypoint) {
	switch(entrypoint) {
	case EP_OP:      	return "th01op";
	case EP_MAIN:    	return "th01main";
	case EP_CUTSCENE:	return "th01fuuin";
	default:         	return nullptr;
	}
}

// argv[0] each sibling's own main() expects when invoked through the chain
// -- matches th01/pc98/entry.cpp's ENTRYPOINTS[] strings, though none of
// the port's main_*.cpp entry points actually branch on it (each one only
// ever calls its own single main_op()/main_main()/main_cutscene()).
const char *sibling_argv0(entrypoint_t entrypoint) {
	switch(entrypoint) {
	case EP_OP:      	return "op";
	case EP_MAIN:    	return "reiiden";
	case EP_CUTSCENE:	return "fuuin";
	default:         	return nullptr;
	}
}

// Picks (and, if not already set by an earlier hop, fixes for the rest of
// the chain) the state hand-off file's path. Process-stable because
// execv() never changes the PID -- a PID-derived default is therefore
// already consistent across every hop even before the setenv() below makes
// it explicit.
std::string resolve_state_file_path() {
	if(const char *existing = std::getenv("TH01_PORT_STATE_FILE")) {
		return existing;
	}
	char buf[256];
	std::snprintf(
		buf, sizeof(buf), "/tmp/th01port_resident_%d.bin",
		static_cast<int>(getpid())
	);
	setenv("TH01_PORT_STATE_FILE", buf, 1);
	return buf;
}

} // namespace

namespace port {

void chain_exec(entrypoint_t entrypoint, const char *self_name) {
	const char *sibling = sibling_binary_name(entrypoint);
	const char *argv0 = sibling_argv0(entrypoint);
	if(sibling == nullptr || argv0 == nullptr) {
		std::fprintf(
			stderr,
			"%s: chain_exec: unknown entrypoint %d -- exiting.\n",
			self_name, static_cast<int>(entrypoint)
		);
		std::exit(1);
	}

	std::string state_path = resolve_state_file_path();
	if(!resdata_save_all_to_file(state_path.c_str())) {
		std::fprintf(
			stderr,
			"%s: chain_exec: failed to write state hand-off file '%s' -- "
			"the next process will start with fresh/no resident state.\n",
			self_name, state_path.c_str()
		);
	}

	// Test-only escape hatch: TH01_PORT_CHAIN_STOP=1 confirms the chain
	// decision (entrypoint resolved, state saved) without an actual execv()
	// into the sibling binary. Used by tests that only care about *this*
	// binary reaching its own game_switch_binary() call cleanly -- e.g.
	// port/tests/fuuin_ending_test.cpp doesn't need a live th01op process on
	// the other end of main_cutscene()'s final game_switch_binary(EP_OP).
	if(std::getenv("TH01_PORT_CHAIN_STOP") != nullptr) {
		std::fprintf(
			stderr,
			"%s: chain_exec: TH01_PORT_CHAIN_STOP set -- stopping here "
			"instead of execv()-ing into '%s'.\n",
			self_name, sibling
		);
		std::exit(0);
	}

	const char *bin_dir = std::getenv("TH01_PORT_BIN_DIR");
	if(bin_dir == nullptr) {
		bin_dir = PORT_CHAIN_BIN_DIR;
	}
	std::string target = std::string(bin_dir) + "/" + sibling;

	char *const argv[] = {const_cast<char *>(argv0), nullptr};
	execv(target.c_str(), argv);

	// Only reached if execv() itself failed.
	std::fprintf(
		stderr,
		"%s: chain_exec: execv('%s') failed: %s -- is port/build/ up to "
		"date (or TH01_PORT_BIN_DIR set correctly)?\n",
		self_name, target.c_str(), std::strerror(errno)
	);
	std::exit(1);
}

} // namespace port
