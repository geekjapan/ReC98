#ifndef REC98_PORT
extern volatile int z_vsync_Count1;
extern volatile int z_vsync_Count2;
#else
// REC98_PORT: no real vsync interrupt exists on a host process. Instead of
// emulating one (background thread + real ISR-like ticking), give the
// counter a class type whose conversion-to-int operator presents one host
// frame (see port/app/host.hpp's host_present_frame()) and then advances by
// 1. Every read site in the OP.EXE closure is a busy-wait spin
// (th01/hardware/frmdelay.cpp's `while(z_vsync_Count1 < frames) {}`) or a
// single-shot wait (z_vsync_wait()), so this turns both into a real,
// frame-paced blocking wait with zero changes to those call sites --
// matching SPEC.md's "vsync wait API call = present one frame, blocking is
// fine, no coroutine needed" design.
struct z_vsync_counter_t {
	int count = 0;
	operator int();
	z_vsync_counter_t &operator=(int v) { count = v; return *this; }
};
extern z_vsync_counter_t z_vsync_Count1;
extern z_vsync_counter_t z_vsync_Count2;
#endif

void vsync_init(void);
void z_vsync_wait(void);
void vsync_exit(void);
