// port/app/assets_dir
// --------------------
// Resolves which directory a port/app/ entry point should chdir() into
// before running original ReC98 game code, so its file_ropen()-based asset
// loads (relative to the process's working directory, matching the
// original DOS binary reading assets from its own directory) find real
// data. Split out from port/app/main_op.cpp so it's unit-testable without
// linking that file's main().

#ifndef PORT_APP_ASSETS_DIR_HPP
#define PORT_APP_ASSETS_DIR_HPP

#include <string>

namespace port {

// True if a regular file (or anything stat()-able) exists at `path`.
bool file_exists(const std::string &path);

// Priority: `env_override` if non-null/non-empty (meant to be
// getenv("TH01_ASSETS_DIR")), else `default_dir` if it contains a file named
// `canary`, else ".". `default_dir`/`canary` are parameters (rather than
// hardcoded) purely so tests can point this at a temp directory instead of
// the real SPEC.md assets path.
std::string resolve_assets_dir(
	const char *env_override, const std::string &default_dir,
	const std::string &canary
);

} // namespace port

#endif // PORT_APP_ASSETS_DIR_HPP
