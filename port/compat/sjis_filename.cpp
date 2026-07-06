#include "port/compat/sjis_filename.hpp"

#include <cerrno>
#include <cstring>

#include <iconv.h>

namespace port {

std::string sjis_to_utf8(const char *sjis) {
	if(sjis == nullptr) {
		return std::string();
	}
	size_t in_bytes = std::strlen(sjis);
	if(in_bytes == 0) {
		return std::string();
	}

	iconv_t cd = ::iconv_open("UTF-8", "SHIFT_JIS");
	if(cd == reinterpret_cast<iconv_t>(-1)) {
		return std::string(sjis);
	}

	// Worst case for Shift-JIS -> UTF-8 is 1 input byte -> 3 output bytes
	// (a lone non-ASCII lead byte would fail to convert, but every 2-byte
	// SJIS character maps to a 2-3 byte UTF-8 kanji), so this is generous
	// headroom, not a tight bound.
	std::string out(in_bytes * 3 + 1, '\0');

	char *in_ptr = const_cast<char *>(sjis);
	char *out_ptr = &out[0];
	size_t out_bytes_left = out.size();

	size_t result = ::iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes_left);
	::iconv_close(cd);

	if(result == static_cast<size_t>(-1)) {
		return std::string(sjis);
	}

	out.resize(out.size() - out_bytes_left);
	return out;
}

} // namespace port
