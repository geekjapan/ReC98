// port/shim/masterlib/keyboard.cpp
// -----------------------------------------------------------------------
// libs/master.lib/master.hpp's keyboard entry points actually called from
// the OP.EXE closure (th01/op_01.cpp): key_start/key_end/key_sense_bios.
// key_sense(int) itself is T4's (port/shim/input.cpp), not redeclared here.
//
// key_start/key_end install/remove master.lib's own INT 18h-hooking
// keyboard driver on real hardware; under SDL there's no driver to install,
// so they're no-ops. key_sense_bios() approximates "any key currently
// pressed" (what th01/op_01.cpp's `while(!key_sense_bios())` hit-key-to-
// continue loop needs) by ORing key_sense() across all BIOS keygroups.

#include "port/compat/borland.hpp"

#include "libs/master.lib/master.hpp"
#include "port/shim/input.hpp"

namespace {
constexpr int NUM_KEYGROUPS = 15;
}

void key_start(void) {
}

void key_end(void) {
}

unsigned key_sense_bios(void) {
	unsigned any = 0;
	for(int group = 0; group < NUM_KEYGROUPS; group++) {
		any |= static_cast<unsigned>(key_sense(group));
	}
	return any;
}
