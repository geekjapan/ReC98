// port/shim/masterlib/random.cpp
// -----------------------------------------------------------------------
// libs/master.lib/master.hpp's "Math" RNG: random_seed + irand(). Bit-exact
// port of libs/master.lib/random.asm, a 32-bit LCG matching Borland C++'s
// runtime rand() (per the asm's own comment): new_seed = seed * 0x015A4E35
// + 1 (mod 2^32); irand() returns the new seed's high 16 bits with the top
// bit masked off, i.e. bits [16:30].

#include "port/compat/borland.hpp"

#include <cstdint>

#include "libs/master.lib/master.hpp"

long random_seed = 0;

int irand(void) {
	uint32_t seed = static_cast<uint32_t>(random_seed) * 0x015A4E35u + 1u;
	random_seed = static_cast<long>(seed);
	return static_cast<int>((seed >> 16) & 0x7FFFu);
}
