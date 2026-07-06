// port/compat/sjis_filename
// --------------------------
// TH01's original code opens files by raw Shift-JIS byte-string literals
// (e.g. th01/shiftjis/fns.hpp's ARC_FN, embedded verbatim in the source as
// whatever bytes cp932 encodes "東方靈異.伝" to -- see that file's own
// comment). MS-DOS/PC-98 filesystems just store those bytes as-is, so the
// original REIIDEN.EXE's file_ropen(ARC_FN) needs no translation at all.
//
// macOS's APFS (and Linux filesystems in general) instead expect UTF-8 file
// names; the real asset file on disk here is literally named with UTF-8
// bytes for "東方靈異.伝" (see SPEC.md's asset-directory setup). So every
// raw-SJIS filename the ported game code passes to a host filesystem call
// (fopen/stat/etc.) needs converting first -- this is that conversion,
// shared by port/shim/masterlib/file_io.cpp's file_* open paths and
// port/app's assets-directory canary checks.
//
// ASCII-only filenames (the overwhelming majority -- REYHIES.DAT, ST1.GRP,
// etc.) round-trip through this unchanged: ASCII is a subset of both Shift-JIS
// and UTF-8, so this is safe to call unconditionally on every filename, not
// just the couple that actually contain non-ASCII bytes.

#ifndef PORT_COMPAT_SJIS_FILENAME_HPP
#define PORT_COMPAT_SJIS_FILENAME_HPP

#include <string>

namespace port {

// Converts a NUL-terminated Shift-JIS (cp932) byte string to UTF-8. Falls
// back to returning `sjis` unchanged (byte-for-byte) if the conversion fails
// (e.g. malformed input) -- callers pass the result straight to a filesystem
// call, so a best-effort passthrough is safer than aborting.
std::string sjis_to_utf8(const char *sjis);

} // namespace port

#endif // PORT_COMPAT_SJIS_FILENAME_HPP
