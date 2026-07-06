// port/shim/masterlib/file_io.cpp
// -----------------------------------------------------------------------
// libs/master.lib/master.hpp's "DOS" and "Optionally buffered single-file
// I/O" sections. The file_* API operates on a single implicit "current
// file", exactly like the real master.lib (one open handle at a time,
// buffered through master.lib's own internal buffer); here that's just a
// stdio FILE*.
//
// dos_getch/putch/puts/create/close/write/seek/axdx (the raw DOS-handle
// half of the "DOS" section) aren't exercised by any OP.EXE closure code
// path -- only the buffered file_* API and dos_puts2/dos_free are. They're
// stubbed with an assert so a future caller fails loudly instead of getting
// silently wrong behavior.

#include "port/compat/borland.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "libs/master.lib/master.hpp"

#include "port/compat/sjis_filename.hpp"

namespace {

FILE *g_current_file = nullptr;

} // namespace

int file_ropen(const char *filename) {
	g_current_file = std::fopen(port::sjis_to_utf8(filename).c_str(), "rb");
	return g_current_file != nullptr;
}

int file_read(void *buf, unsigned wsize) {
	if(g_current_file == nullptr) {
		return 0;
	}
	return static_cast<int>(std::fread(buf, 1, wsize, g_current_file));
}

long file_size(void) {
	if(g_current_file == nullptr) {
		return 0;
	}
	long cur = std::ftell(g_current_file);
	std::fseek(g_current_file, 0, SEEK_END);
	long size = std::ftell(g_current_file);
	std::fseek(g_current_file, cur, SEEK_SET);
	return size;
}

int file_create(const char *filename) {
	g_current_file = std::fopen(port::sjis_to_utf8(filename).c_str(), "wb");
	return g_current_file != nullptr;
}

int file_append(const char *filename) {
	std::string path = port::sjis_to_utf8(filename);
	g_current_file = std::fopen(path.c_str(), "r+b");
	if(g_current_file == nullptr) {
		g_current_file = std::fopen(path.c_str(), "w+b");
	}
	if(g_current_file == nullptr) {
		return 0;
	}
	std::fseek(g_current_file, 0, SEEK_END);
	return 1;
}

int file_write(const void *buf, unsigned wsize) {
	if(g_current_file == nullptr) {
		return 0;
	}
	return static_cast<int>(std::fwrite(buf, 1, wsize, g_current_file));
}

void file_seek(long pos, int dir) {
	if(g_current_file == nullptr) {
		return;
	}
	// SEEK_SET=0, SEEK_CUR=1, SEEK_END=2 in both master.hpp and <cstdio>.
	std::fseek(g_current_file, pos, dir);
}

void file_close(void) {
	if(g_current_file != nullptr) {
		std::fclose(g_current_file);
		g_current_file = nullptr;
	}
}

int file_exist(const char *filename) {
	FILE *f = std::fopen(port::sjis_to_utf8(filename).c_str(), "rb");
	if(f == nullptr) {
		return 0;
	}
	std::fclose(f);
	return 1;
}

int file_delete(const char *filename) {
	return std::remove(port::sjis_to_utf8(filename).c_str()) == 0;
}

void dos_puts2(const char *string) {
	std::fputs(string, stdout);
}

void dos_free(void *seg) {
	std::free(seg);
}

int dos_getch(void) {
	assert(false && "dos_getch: unused by OP.EXE closure, not implemented");
	return 0;
}

void dos_putch(int chr) {
	(void)chr;
	assert(false && "dos_putch: unused by OP.EXE closure, not implemented");
}

void dos_puts(const char *str) {
	(void)str;
	assert(false && "dos_puts: unused by OP.EXE closure, not implemented");
}

int dos_create(const char *filename, int attrib) {
	(void)filename;
	(void)attrib;
	assert(false && "dos_create: unused by OP.EXE closure, not implemented");
	return -1;
}

int dos_close(int fh) {
	(void)fh;
	assert(false && "dos_close: unused by OP.EXE closure, not implemented");
	return -1;
}

int dos_write(int fh, const void *buffer, unsigned len) {
	(void)fh;
	(void)buffer;
	(void)len;
	assert(false && "dos_write: unused by OP.EXE closure, not implemented");
	return -1;
}

long dos_seek(int fh, long offs, int mode) {
	(void)fh;
	(void)offs;
	(void)mode;
	assert(false && "dos_seek: unused by OP.EXE closure, not implemented");
	return -1;
}

long dos_axdx(int axval, const char *strval) {
	(void)axval;
	(void)strval;
	assert(false && "dos_axdx: unused by OP.EXE closure, not implemented");
	return -1;
}
