// Unit test for port/shim/masterlib/resdata.cpp: resdata_exist/create id ->
// pointer registry semantics (create is idempotent, distinct ids don't
// collide, new blocks are NOT zero-initialized).

#include <cstdio>
#include <cstring>

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
	const char id_a[] = "TESTRES_A";
	const char id_b[] = "TESTRES_B";

	expect(
		resdata_exist(id_a, sizeof(id_a) - 1, 1) == nullptr,
		"resdata_exist on unknown id returns nullptr"
	);

	void *a1 = resdata_create(id_a, sizeof(id_a) - 1, 4);
	expect(a1 != nullptr, "resdata_create allocates a block");

	void *a2 = resdata_exist(id_a, sizeof(id_a) - 1, 4);
	expect(a2 == a1, "resdata_exist finds the same block after create");

	void *a3 = resdata_create(id_a, sizeof(id_a) - 1, 4);
	expect(a3 == a1, "resdata_create is idempotent for an existing id");

	expect(
		resdata_exist(id_b, sizeof(id_b) - 1, 4) == nullptr,
		"distinct id is not found"
	);
	void *b1 = resdata_create(id_b, sizeof(id_b) - 1, 4);
	expect(b1 != nullptr && b1 != a1, "distinct id gets a distinct block");

	// Write a sentinel to a1, then re-fetch by id and confirm it persists
	// (i.e. the same live memory, not a fresh allocation).
	static_cast<unsigned char *>(a1)[0] = 0x42;
	void *a4 = resdata_exist(id_a, sizeof(id_a) - 1, 4);
	expect(
		static_cast<unsigned char *>(a4)[0] == 0x42,
		"data written to a resident block persists across re-lookup"
	);

	if(g_failures == 0) {
		std::printf("masterlib_resdata_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
