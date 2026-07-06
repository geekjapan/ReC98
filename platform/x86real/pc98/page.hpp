// VRAM page flipping
// ------------------

#include "x86real.h"

#ifdef REC98_PORT
#include "pc98emu/vram.hpp"
// REC98_PORT: _outportb_() emits a raw x86 OUT instruction via __emit__()
// (see x86real.h), which is meaningless (and unimplementable) in a host
// process -- there's no real port 0xA4/0xA6 to write to, and no inline
// machine-code emission on a modern, non-x86-real-mode target. pc98emu
// already models page flipping directly (see pc98emu_page_set_accessible/
// shown() in pc98emu/vram.hpp) rather than by decoding I/O port writes, so
// route through that instead of pc98emu_outportb() (which only dispatches
// GRCG ports 0x7C/0x7E; see pc98emu/grcg.cpp).
#define page_show(page)  	pc98emu_page_set_shown(page)
#define page_access(page)	pc98emu_page_set_accessible(page)
#else
#define page_show(page)  	_outportb_(0xA4, page)
#define page_access(page)	_outportb_(0xA6, page)
#endif
