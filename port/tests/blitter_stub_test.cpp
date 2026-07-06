// Unit test for port/shim/blitter_stub.cpp: blit_source is a plain
// writable global (satisfying th01/main/bullet/pellet.cpp's field writes),
// and blitter_init_clip() returns nullptr unconditionally -- a deliberate,
// documented gap (see that file's header comment) rather than a real port
// of platform/x86real/pc98/blitter.cpp's pseudoregister/segment-arithmetic
// blit routines. Every call site is nullptr-safe (`if(b && ...)`), so this
// just confirms that contract holds.

#include <cstdio>

#include "port/compat/borland.hpp"

#include "platform/x86real/pc98/blitter.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond) \
	do { \
		if(!(cond)) { \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			g_failures++; \
		} \
	} while(0)

} // namespace

int main() {
	CHECK(blitter_init_clip(0, 0) == nullptr);
	CHECK(blitter_init_clip(100, 200) == nullptr);

	blit_source.stride = 42;
	blit_source.offset = 7;
	blit_source.w = 16;
	blit_source.h = 8;
	CHECK(blit_source.stride == 42);
	CHECK(blit_source.offset == 7);
	CHECK(blit_source.w == 16);
	CHECK(blit_source.h == 8);

	if(g_failures == 0) {
		std::printf("blitter_stub_test: all checks passed\n");
		return 0;
	}
	std::fprintf(stderr, "blitter_stub_test: %d check(s) FAILED\n", g_failures);
	return 1;
}
