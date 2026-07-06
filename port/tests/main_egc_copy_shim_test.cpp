// Unit test for port/shim/egc_copy_shim.cpp: the EGCCopy class
// (platform/x86real/pc98/egc.hpp) forwards to pc98emu's own EGC model
// instead of the original _ES/_SI/_DI-pseudoregister implementation
// (platform/x86real/pc98/egc.cpp). pc98emu/egc.{hpp,cpp}'s own
// egc_copy_rect()/egc_copy_rect_interpage()/egc_copy_rect_interpage_same_pos()
// are already thoroughly tested by egc_test.cpp -- this just checks that
// EGCCopy's constructor/destructor/methods actually reach them (a thin
// forwarding smoke test, not a re-test of the EGC engine itself).

#include <cstdio>
#include <cstring>

#include "port/compat/borland.hpp"

#include "pc98emu/vram.hpp"
#include "platform/x86real/pc98/egc.hpp"

// platform/x86real/pc98/egc.hpp pulls in pc98.h, whose `ROW_SIZE` macro
// would otherwise shadow (textually, mid-expression) pc98emu::ROW_SIZE
// below -- undo it since this file only wants the pc98emu one.
#undef ROW_SIZE

namespace {

int g_failures = 0;

#define CHECK(cond) \
	do { \
		if(!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			g_failures++; \
		} \
	} while(0)

bool read_dot(int page, int plane, int x, int y) {
	int byte_offset = (x / 8) + y * pc98emu::ROW_SIZE;
	int bit = 7 - (x % 8);
	return (pc98emu_planes[page][plane][byte_offset] >> bit) & 1;
}

void write_dot(int page, int plane, int x, int y, bool value) {
	int byte_offset = (x / 8) + y * pc98emu::ROW_SIZE;
	int bit = 7 - (x % 8);
	uint8_t &byte = pc98emu_planes[page][plane][byte_offset];
	byte = value
		? static_cast<uint8_t>(byte | (1 << bit))
		: static_cast<uint8_t>(byte & ~(1 << bit));
}

void reset_all() {
	pc98emu::g_egc_state = {};
	std::memset(pc98emu_planes, 0, sizeof(pc98emu_planes));
	pc98emu_page_set_accessible(0);
	pc98emu_page_set_shown(0);
}

} // namespace

int main() {
	reset_all();
	for(int i = 0; i < 16; i++) {
		write_dot(0, pc98emu::PLANE_B, i, 0, (i % 2) == 0);
	}
	{
		EGCCopy copier; // must activate/deactivate cleanly (ctor/dtor)
		copier.rect(0, 0, 32, 0, 16, 1);
	}
	for(int i = 0; i < 16; i++) {
		CHECK(read_dot(0, pc98emu::PLANE_B, 32 + i, 0) == ((i % 2) == 0));
	}

	reset_all();
	for(int i = 0; i < 16; i++) {
		write_dot(0, pc98emu::PLANE_R, i, 1, (i % 3) == 0);
	}
	{
		EGCCopy copier;
		copier.rect_interpage(0, 1, 16, 5, 16, 1, /*src_page=*/0);
	}
	for(int i = 0; i < 16; i++) {
		CHECK(read_dot(1, pc98emu::PLANE_R, 16 + i, 5) == ((i % 3) == 0));
	}

	reset_all();
	for(int i = 0; i < 16; i++) {
		write_dot(1, pc98emu::PLANE_G, i, 2, (i % 5) == 0);
	}
	{
		EGCCopy copier;
		copier.rect_interpage(0, 2, 16, 1, /*src_page=*/1); // same-position overload
	}
	for(int i = 0; i < 16; i++) {
		CHECK(read_dot(0, pc98emu::PLANE_G, i, 2) == ((i % 5) == 0));
	}

	if(g_failures == 0) {
		std::printf("main_egc_copy_shim_test: all checks passed\n");
		return 0;
	}
	std::fprintf(stderr, "main_egc_copy_shim_test: %d check(s) FAILED\n", g_failures);
	return 1;
}
