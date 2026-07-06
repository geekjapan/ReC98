#pragma option -zCGRAPH_EX_TEXT

#include "x86real.h"
#include "th01/hardware/scrollup.hpp"
#include "th01/hardware/vsync.hpp" // z_vsync_wait()

#ifndef REC98_PORT

static const int GDC_ROW_SIZE = (ROW_SIZE / 2);

// Actually taken straight from the setgsta() example function from the
// PC-9801 Programmers' Bible, p. 150.
void z_vsync_wait_and_scrollup(vram_y_t line_on_top)
{
	line_on_top %= RES_Y;

	int16_t dp1sta = (line_on_top * GDC_ROW_SIZE);
	// Line counts start at the 5th bit of the parameter
	int16_t dp1lin = ((RES_Y - line_on_top) << 4);
	int16_t dp2sta = (0 * GDC_ROW_SIZE);
	int16_t dp2lin = (line_on_top << 4);

	// Display address increment time; depends on the GDC clock frequency
	int im;
	if((peekb(0, 0x54D) & 4) == 0) {	// GDC clock == 2.5 MHz?
		im = 0;
	} else {
		im = (1 << 6); // 6th bit inside the parameter
	}
	// Wait until VSYNC
	while( (inportb(0xA0) & 0x20) != 0 );
	while( (inportb(0xA0) & 0x20) == 0 );
	// Wait until the graphics GDC's command FIFO is empty
	while( (inportb(0xA0) & 0x04) == 0 );

	// Graphics GDC SCROLL command
	// (see PC-9801 Programmers' Bible, p. 143)
	// ---------------------------
	// 7 = SCROLL, 0 = starting regularly with the first parameter
	outport(0xA2, 0x70);
	// Display partition #1
	outportb(0xA0, (dp1sta % 0x100));
	outportb(0xA0, (dp1sta / 0x100));
	outportb(0xA0, (dp1lin % 0x100));
	outportb(0xA0, ((dp1lin / 0x100) + im));
	// Display partition #2
	outportb(0xA0, (dp2sta % 0x100));
	outportb(0xA0, (dp2sta / 0x100));
	outportb(0xA0, (dp2lin % 0x100));
	outportb(0xA0, ((dp2lin / 0x100) + im));
	// ---------------------------
}

#else // REC98_PORT

// REC98_PORT: this function's original body (see the #ifndef branch above)
// polls the GDC status register (port 0xA0) directly -- a raw hardware
// wait that port/compat/x86real_shim.cpp's __inportb__() doesn't (and, on a
// host process, can't meaningfully) implement, so it falls through to that
// dispatcher's "unhandled port" default of always returning 0. In an
// optimized (NDEBUG) build, the `assert(false, ...)` guarding that default
// compiles out, so `inportb(0xA0)` silently always returns 0 -- and this
// function's second wait loop, `while((inportb(0xA0) & 0x20) == 0);`, spins
// forever as a result: a true, permanent, CPU-pegging hang with no further
// frames presented, not merely an original-hardware-style busy spin. This
// is the root cause of the reported "th01main hangs" gameplay-stability
// bug: it's reached unconditionally from th01/main_01.cpp every time a life
// is lost or a stage segment clears, so any run that reaches either point
// (more reliably with Shot held continuously, since that clears cards --
// and loses lives to unavoided pellets -- faster than tapping) hangs here.
//
// The actual wait this function performs is "wait for the next VSync",
// exactly what th01/hardware/vsync.hpp's REC98_PORT branch already turns
// into a real, frame-paced wait via z_vsync_counter_t/
// port::host_present_frame() -- z_vsync_wait() (declared in the same
// header) is that same primitive, so reuse it here instead of re-deriving
// a GDC-status-polling loop that has no host equivalent.
//
// pc98emu doesn't model the GDC's hardware split-screen scroll registers
// (there's no "two display partitions" concept in its VRAM/renderer, only
// a single flat framebuffer), so the dp1/dp2 register writes below have no
// host-side effect to reproduce; every other GDC/CG-ROM register write
// without a modeled equivalent is already treated the same way (an inert
// no-op) by x86real_shim.cpp's __outportb__(), per this file's own header
// comment. [line_on_top] is always 0 at this port's only reachable call
// site (th01/main_01.cpp's life-loss/stage-clear transition), which is
// itself a "reset to no scroll" no-op even under real GDC semantics.
void z_vsync_wait_and_scrollup(vram_y_t /* line_on_top */)
{
	z_vsync_wait();
}

#endif // REC98_PORT
