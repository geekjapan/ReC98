// port/tests/th01_grcg_test_stub.hpp
// -----------------------------------------------------------------------
// Test-only linkage for th01/hardware/graph.h's grcg_setcolor_rmw()/
// grcg_setcolor_tcr()/grcg_off_func(). Their real implementation lives in
// th01/hardware/graph.cpp (TH01-local GRCG helpers, out of scope for T8 --
// see port/shim/masterlib/README.md; that whole file gets ported by T9
// op-bringup). Some th01/formats draw functions (grc_put_8(), ptn.hpp's
// vram_erase() macro) call them directly, so format tests that exercise
// those draw paths need *something* to link against.
//
// This reimplements the exact real algorithm (see th01/hardware/graph.cpp's
// grcg_setcolor()/grcg_setcolor_rmw/tcr/grcg_off_func, and
// th01/hardware/grcg.hpp's grcg_off()), just targeting pc98emu_outportb()
// directly instead of going through x86real.h's outportb()/__outportb__,
// which port/pc98emu/io.{hpp,cpp} (SPEC.md's T1 component) hasn't been
// built yet to back. Not a fake stub: it's the same GRCG port I/O sequence,
// only re-pointed at the one I/O dispatch entry point that already exists.

#ifndef PORT_TESTS_TH01_GRCG_TEST_STUB_HPP
#define PORT_TESTS_TH01_GRCG_TEST_STUB_HPP

#include "port/compat/borland.hpp"

#include "th01/hardware/graph.h"
#include "pc98emu/grcg.hpp"

// Not `inline`: this header is only ever included by a single translation
// unit per test binary, and grc.cpp/ptn.cpp's calls to these functions
// happen in *their* object file, not this TU -- so `inline` (which clang is
// free to drop when never odr-used *within this TU*) silently vanishes
// even though another object file needs the symbol. Plain external-linkage
// definitions are always emitted.
namespace th01_grcg_test_stub {

void setcolor(uint8_t mode, vc2 col) {
	pc98emu_outportb(0x7C, mode);
	pc98emu_outportb(0x7E, (col & 1) ? 0xFF : 0x00);
	pc98emu_outportb(0x7E, (col & 2) ? 0xFF : 0x00);
	pc98emu_outportb(0x7E, (col & 4) ? 0xFF : 0x00);
	pc98emu_outportb(0x7E, (col & 8) ? 0xFF : 0x00);
}

} // namespace th01_grcg_test_stub

void grcg_setcolor_rmw(vc2 col) {
	th01_grcg_test_stub::setcolor(0xC0, col);
}

void grcg_setcolor_tcr(vc2 col) {
	th01_grcg_test_stub::setcolor(0x80, col);
}

void grcg_off_func(void) {
	pc98emu_outportb(0x7C, 0x00);
}

#endif // PORT_TESTS_TH01_GRCG_TEST_STUB_HPP
