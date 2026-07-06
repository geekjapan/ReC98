// Unit test for port/app/assets_dir: the asset-presence check
// port/app/main_op.cpp uses to fail with a clear message (M1 acceptance,
// see that file's header comment) instead of running original game code
// against missing data.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "port/app/assets_dir.hpp"

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
	std::string tmp_dir = PORT_TESTS_TMP_DIR "/assets_dir_test_XXXXXX";
	std::vector<char> tmpl(tmp_dir.begin(), tmp_dir.end());
	tmpl.push_back('\0');
	char *made = mkdtemp(tmpl.data());
	expect(made != nullptr, "mkdtemp() created a scratch directory");
	std::string dir = made ? made : tmp_dir;

	// No canary file yet, no env override -> falls back to ".".
	expect(
		port::resolve_assets_dir(nullptr, dir, "REIIDEN2.GRP") == ".",
		"missing canary falls back to \".\""
	);
	expect(
		!port::file_exists(dir + "/REIIDEN2.GRP"),
		"file_exists() is false for a nonexistent file"
	);

	// Env override always wins, even if the directory doesn't exist.
	expect(
		port::resolve_assets_dir("/nonexistent/env/dir", dir, "REIIDEN2.GRP")
			== "/nonexistent/env/dir",
		"non-empty env override wins regardless of canary presence"
	);
	expect(
		port::resolve_assets_dir("", dir, "REIIDEN2.GRP") == ".",
		"empty-string env override is treated as unset"
	);

	// Create the canary file -> default_dir is now used.
	std::string canary_path = dir + "/REIIDEN2.GRP";
	FILE *f = std::fopen(canary_path.c_str(), "w");
	expect(f != nullptr, "created canary file for the present-canary case");
	if(f) {
		std::fclose(f);
	}
	expect(
		port::file_exists(canary_path), "file_exists() is true after creation"
	);
	expect(
		port::resolve_assets_dir(nullptr, dir, "REIIDEN2.GRP") == dir,
		"present canary selects default_dir"
	);

	std::remove(canary_path.c_str());
	::rmdir(dir.c_str());

	if(g_failures == 0) {
		std::printf("assets_dir_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
