#include "port/app/assets_dir.hpp"

#include <sys/stat.h>

namespace port {

bool file_exists(const std::string &path) {
	struct stat st;
	return ::stat(path.c_str(), &st) == 0;
}

std::string resolve_assets_dir(
	const char *env_override, const std::string &default_dir,
	const std::string &canary
) {
	if(env_override && env_override[0] != '\0') {
		return env_override;
	}
	if(file_exists(default_dir + "/" + canary)) {
		return default_dir;
	}
	return ".";
}

} // namespace port
