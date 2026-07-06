// port/shim/masterlib/resdata.cpp
// -----------------------------------------------------------------------
// libs/master.lib/master.hpp's "Resident data" section, used through the
// ResData<T> template (master.hpp) by th01/core/resident.cpp to persist
// resident_t across the OP.EXE -> REIIDEN.EXE -> FUUIN.EXE exec() chain.
// Real master.lib backs this with a DOS TSR paragraph found by scanning
// memory for [id]. T21 made this port's binaries real, separate OS
// processes chained by execv() (port/shim/masterlib/chain_exec.hpp) rather
// than one process, so a plain in-process id -> pointer registry is no
// longer enough on its own -- see maybe_load_state_file()/
// resdata_save_all_to_file() below, which round-trip the registry through a
// flat file across exactly that kind of process replacement.
//
// Deliberately does NOT zero-initialize newly created blocks: the comment
// above th01/op_01.cpp's start_game() notes ZUN code relies on
// resdata_create() *not* clearing memory (see resident_t::debug_mode).
//
// Hand-rolled fixed-capacity registry instead of std::vector/<algorithm>:
// compat/borland.hpp's `#define __seg` (empty, needed for master.hpp's
// `void __seg *`) collides with libc++'s internal segmented-iterator code
// (<algorithm>'s for_each_n_segment.h uses `__seg` as a local variable
// name), so this TU avoids the STL container headers entirely rather than
// depending on include-order gymnastics to dodge the token collision.

#include "port/compat/borland.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "libs/master.lib/master.hpp"
#include "port/shim/masterlib/resdata_persist.hpp"

namespace {

constexpr int MAX_RESDATA_ENTRIES = 8;

struct resdata_entry_t {
	bool in_use = false;
	char id[64] = {};
	unsigned idlen = 0;
	unsigned parasize = 0;
	void *block = nullptr;
};

resdata_entry_t g_registry[MAX_RESDATA_ENTRIES];

resdata_entry_t *find(const char *id, unsigned idlen) {
	for(resdata_entry_t &entry : g_registry) {
		if(entry.in_use && entry.idlen == idlen &&
		   std::memcmp(entry.id, id, idlen) == 0) {
			return &entry;
		}
	}
	return nullptr;
}

resdata_entry_t *first_free_slot() {
	for(resdata_entry_t &slot : g_registry) {
		if(!slot.in_use) {
			return &slot;
		}
	}
	return nullptr;
}

// Loads TH01_PORT_STATE_FILE (if set and readable) into the registry, once
// per process. Called from both resdata_exist() and resdata_create() since
// either can be the first call th01/core/resident.cpp makes
// (resident_get() vs. resident_get_or_create()). Consumes the file (removes
// it after a successful load) so a later, unrelated standalone launch never
// picks up a stale hand-off meant for a since-finished exec chain.
void maybe_load_state_file() {
	static bool attempted = false;
	if(attempted) {
		return;
	}
	attempted = true;

	const char *path = std::getenv("TH01_PORT_STATE_FILE");
	if(path == nullptr) {
		return;
	}
	FILE *f = std::fopen(path, "rb");
	if(f == nullptr) {
		return;
	}

	for(;;) {
		unsigned idlen = 0;
		unsigned parasize = 0;
		if(std::fread(&idlen, sizeof(idlen), 1, f) != 1) {
			break;
		}
		if(std::fread(&parasize, sizeof(parasize), 1, f) != 1) {
			break;
		}
		resdata_entry_t *slot = first_free_slot();
		if(slot == nullptr || idlen == 0 || idlen >= sizeof(slot->id)) {
			break;
		}
		if(std::fread(slot->id, 1, idlen, f) != idlen) {
			break;
		}
		std::size_t bytes = static_cast<std::size_t>(parasize) * 16;
		void *block = std::malloc(bytes);
		if(block == nullptr) {
			break;
		}
		if(std::fread(block, 1, bytes, f) != bytes) {
			std::free(block);
			break;
		}
		slot->idlen = idlen;
		slot->parasize = parasize;
		slot->block = block;
		slot->in_use = true;
	}
	std::fclose(f);
	std::remove(path);
}

} // namespace

void *resdata_exist(const char *id, unsigned idlen, unsigned parasize) {
	(void)parasize;
	maybe_load_state_file();
	resdata_entry_t *entry = find(id, idlen);
	return (entry != nullptr) ? entry->block : nullptr;
}

void *resdata_create(const char *id, unsigned idlen, unsigned parasize) {
	maybe_load_state_file();
	resdata_entry_t *entry = find(id, idlen);
	if(entry != nullptr) {
		return entry->block;
	}
	resdata_entry_t *slot = first_free_slot();
	if(slot != nullptr) {
		void *block = std::malloc(static_cast<std::size_t>(parasize) * 16);
		if(block == nullptr) {
			return nullptr;
		}
		assert(idlen < sizeof(slot->id));
		std::memcpy(slot->id, id, idlen);
		slot->idlen = idlen;
		slot->parasize = parasize;
		slot->block = block;
		slot->in_use = true;
		return block;
	}
	assert(false && "resdata_create: registry full (MAX_RESDATA_ENTRIES)");
	return nullptr;
}

bool port::resdata_save_all_to_file(const char *path) {
	FILE *f = std::fopen(path, "wb");
	if(f == nullptr) {
		return false;
	}
	bool ok = true;
	for(resdata_entry_t &entry : g_registry) {
		if(!entry.in_use) {
			continue;
		}
		std::size_t bytes = static_cast<std::size_t>(entry.parasize) * 16;
		ok = ok
			&& std::fwrite(&entry.idlen, sizeof(entry.idlen), 1, f) == 1
			&& std::fwrite(&entry.parasize, sizeof(entry.parasize), 1, f) == 1
			&& std::fwrite(entry.id, 1, entry.idlen, f) == entry.idlen
			&& std::fwrite(entry.block, 1, bytes, f) == bytes;
	}
	std::fclose(f);
	return ok;
}
