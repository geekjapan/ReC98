// Unit test for port/shim/masterlib/file_io.cpp: exercises file_ropen/
// read/close/create/append/write/seek/exist/delete/size against real temp
// files, driving them through the actual libs/master.lib/master.hpp
// declarations (compiled signature-exact).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

std::string temp_path(const char *name) {
#ifdef PORT_TESTS_TMP_DIR
	return std::string(PORT_TESTS_TMP_DIR) + "/" + name;
#else
	return std::string("/tmp/") + name;
#endif
}

} // namespace

int main() {
	const std::string fn = temp_path("masterlib_file_io_test.bin");
	std::remove(fn.c_str());

	// Nonexistent file: file_ropen fails, file_exist is false.
	expect(file_ropen(fn.c_str()) == 0, "ropen of missing file fails");
	expect(file_exist(fn.c_str()) == 0, "missing file does not exist");

	// Create + write + close.
	expect(file_create(fn.c_str()) != 0, "file_create succeeds");
	const char payload[] = "hello, master.lib";
	int written = file_write(payload, sizeof(payload));
	expect(written == sizeof(payload), "file_write writes full payload");
	file_close();

	expect(file_exist(fn.c_str()) != 0, "file exists after create+write");

	// Reopen for read, verify contents and size.
	expect(file_ropen(fn.c_str()) != 0, "ropen of existing file succeeds");
	expect(file_size() == sizeof(payload), "file_size matches written size");
	char buf[sizeof(payload)] = {};
	int read = file_read(buf, sizeof(buf));
	expect(read == sizeof(payload), "file_read reads full payload");
	expect(std::memcmp(buf, payload, sizeof(payload)) == 0,
	       "read-back content matches");
	file_close();

	// file_seek + partial read.
	expect(file_ropen(fn.c_str()) != 0, "ropen for seek test");
	file_seek(7, SEEK_SET);
	char partial[6] = {};
	file_read(partial, 5);
	expect(std::memcmp(partial, "maste", 5) == 0, "seek(SEEK_SET) repositions read");
	file_seek(-5, SEEK_CUR);
	file_read(partial, 5);
	expect(std::memcmp(partial, "maste", 5) == 0, "seek(SEEK_CUR) repositions read");
	file_close();

	// file_append: opens existing file positioned at the end.
	expect(file_append(fn.c_str()) != 0, "file_append opens existing file");
	const char more[] = "!!!";
	file_write(more, sizeof(more));
	file_close();

	expect(file_ropen(fn.c_str()) != 0, "ropen after append");
	expect(
		file_size() == static_cast<long>(sizeof(payload) + sizeof(more)),
		"file_size reflects appended bytes"
	);
	file_close();

	// file_delete.
	expect(file_delete(fn.c_str()) != 0, "file_delete succeeds");
	expect(file_exist(fn.c_str()) == 0, "file gone after delete");

	// dos_puts2: just verify it doesn't crash (writes to stdout).
	dos_puts2("dos_puts2 smoke test\n");

	// dos_free: verify it's callable on a malloc'd block without crashing
	// (paired allocator with resdata_create(), see masterlib_resdata_test).
	void *block = std::malloc(16);
	dos_free(block);

	if(g_failures == 0) {
		std::printf("masterlib_file_io_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
