// Unit test for port/shim/masterlib/random.cpp: irand()/random_seed against
// hand-derived expected values from libs/master.lib/random.asm's LCG
// (seed = seed * 0x015A4E35 + 1; return (seed >> 16) & 0x7FFF), so a
// regression here is a real break of RNG bit-exactness, not just "some
// value changed".

#include <cstdio>

#include "port/compat/borland.hpp"

#include "libs/master.lib/master.hpp"

namespace {

int g_failures = 0;

void expect(bool cond, const char *what) {
	if(!cond) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		g_failures++;
	}
}

} // namespace

int main() {
	// seed=1: new_seed = 1*0x015A4E35 + 1 = 0x015A4E36.
	// irand() returns (new_seed >> 16) & 0x7FFF = 0x015A = 346.
	irand_init(1);
	int first = irand();
	expect(first == 346, "irand() with seed=1 returns 346 (LCG parity)");
	expect(random_seed == 0x015A4E36L, "random_seed updated to 0x015A4E36");

	// Same seed reproduces the same sequence.
	irand_init(1);
	int repeat = irand();
	expect(repeat == first, "re-seeding reproduces the same output");

	// seed=0x015A4E36 (continuing the sequence): new_seed =
	// 0x015A4E36 * 0x015A4E35 + 1 (mod 2^32) = 0x8082A52F (cross-checked
	// with an independent 32-bit modular multiplication, not just re-run
	// through the same code under test).
	int second = irand();
	expect(
		random_seed == static_cast<long>(0x8082A52FUL),
		"second call in the sequence matches independently-computed LCG state"
	);
	expect(
		second == static_cast<int>((0x8082A52FUL >> 16) & 0x7FFFu),
		"second irand() value matches independently-computed LCG state"
	);

	// Every result must fit in 15 bits regardless of seed.
	irand_init(0x12345678);
	for(int i = 0; i < 1000; i++) {
		int v = irand();
		if(v < 0 || v > 0x7FFF) {
			expect(false, "irand() stays within [0, 0x7FFF]");
			break;
		}
	}

	if(g_failures == 0) {
		std::printf("masterlib_random_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
