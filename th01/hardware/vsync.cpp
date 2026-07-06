/* ReC98
 * -----
 * VSync interrupt handler
 */

#pragma warn -aus

#include "x86real.h"
#include "th01/hardware/vsync.hpp"

#ifndef REC98_PORT

extern bool vsync_initialized = false;

volatile int z_vsync_Count1;
volatile int z_vsync_Count2;
static void interrupt (*vsync_callback_old)(...);

static void interrupt vsync_intfunc(...)
{
	z_vsync_Count1++;
	z_vsync_Count2++;
	outportb(0x00, 0x20); // End of Interrupt
	outportb(0x64, 0); // VSync interrupt trigger
}

void vsync_init(void)
{
	if(vsync_initialized == false) {
		vsync_initialized = true;
		disable();
		vsync_callback_old = getvect(0x0A);
		setvect(0x0A, vsync_intfunc);

		// Disable all interrupts from 0x08 to 0x0F except for 0x0A
		outportb(0x02, (inportb(0x02) & 0xFB));

		outportb(0x64, 0); // VSync interrupt trigger
		enable();
	}
}

void vsync_exit(void)
{
	if(vsync_initialized == true) {
		vsync_initialized = false;
		disable();

		// Reenable all interrupts from 0x08 to 0x0F except for 0x0A
		outportb(0x02, (inportb(0x02) | 0x04));

		setvect(0x0a, vsync_callback_old);
		enable();
	}
}

void z_vsync_wait(void)
{
	do {
		_AL = inportb(0x60);
	} while((_AL & 0x20) != 0);
	do {
		_AL = inportb(0x60);
	} while((_AL & 0x20) == 0);
}

#else // REC98_PORT

// REC98_PORT: see th01/hardware/vsync.hpp's z_vsync_counter_t comment. There
// is no real vsync interrupt to hook, so vsync_init()/vsync_exit() just
// track the "are we initialized" flag that game_init()/game_exit_inner()
// already gate on; the actual frame pacing lives in the counter type's
// conversion operator below.

#include "port/app/host.hpp"

extern bool vsync_initialized = false;

z_vsync_counter_t z_vsync_Count1;
z_vsync_counter_t z_vsync_Count2;

z_vsync_counter_t::operator int()
{
	port::host_present_frame();
	count++;
	return count;
}

void vsync_init(void)
{
	vsync_initialized = true;
}

void vsync_exit(void)
{
	vsync_initialized = false;
}

void z_vsync_wait(void)
{
	port::host_present_frame();
}

#endif // REC98_PORT
