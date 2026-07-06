// Unit test for port/shim/masterlib/math.cpp: the SinTable8/CosTable8
// fixed-point trig table and iatan2()/isqrt()/ihypot(), needed by
// th01/math/polar.hpp and REIIDEN gameplay code (T11a) but never reached by
// the OP.EXE title/menu closure (T9), so never previously shimmed.

#include <cmath>
#include <cstdio>

#include "port/compat/borland.hpp"

#include "libs/master.lib/master.hpp"

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
	// Table shape: angle 0 = pure +X (cos=max, sin=0); angle 64 (quarter
	// turn, 256/4) = pure +Y (sin=max, cos=0); both quadrant peaks hit the
	// full Q8 amplitude of 256 (matching th01/math/polar.hpp's `>> 8`
	// divide convention).
	CHECK(Sin8(0) == 0);
	CHECK(Cos8(0) == 256);
	CHECK(Sin8(64) == 256);
	CHECK(Cos8(64) == 0);
	CHECK(Sin8(128) == 0);
	CHECK(Cos8(128) == -256);

	// Sin8()/Cos8() mask their argument to 8 bits (`SinTable8[(t) & 0xff]`),
	// so it must wrap cleanly instead of indexing out of bounds.
	CHECK(Sin8(256) == Sin8(0));
	CHECK(Sin8(-1) == Sin8(255));

	// Table is monotonically non-decreasing over the first quarter turn.
	for(int i = 1; i < 64; i++) {
		CHECK(Sin8(i) >= Sin8(i - 1));
	}

	CHECK(isqrt(0) == 0);
	CHECK(isqrt(16) == 4);
	CHECK(isqrt(10000) == 100);

	// iatan2() domain matches Sin8()/Cos8(): 0..255 mapping to 0..2*pi.
	CHECK(iatan2(0, 1) == 0); // pointing along +X
	int quarter = iatan2(1, 0); // pointing along +Y
	CHECK(quarter == 64);

	CHECK(ihypot(3, 4) == 5);

	if(g_failures == 0) {
		std::printf("masterlib_math_test: all checks passed\n");
		return 0;
	}
	std::fprintf(stderr, "masterlib_math_test: %d check(s) FAILED\n", g_failures);
	return 1;
}
