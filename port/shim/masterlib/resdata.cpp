// port/shim/masterlib/resdata.cpp
// -----------------------------------------------------------------------
// libs/master.lib/master.hpp's "Resident data" section, used through the
// ResData<T> template (master.hpp) by th01/core/resident.cpp to persist
// resident_t across the OP.EXE -> REIIDEN.EXE -> FUUIN.EXE exec() chain.
// Real master.lib backs this with a DOS TSR paragraph found by scanning
// memory for [id]; since the port's binaries all run in one process, a
// simple id -> pointer registry gives the same "survives
// game_switch_binary()" behavior without needing DOS memory scanning.
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
#include <cstdlib>
#include <cstring>

#include "libs/master.lib/master.hpp"

namespace {

constexpr int MAX_RESDATA_ENTRIES = 8;

struct resdata_entry_t {
	bool in_use = false;
	char id[64] = {};
	unsigned idlen = 0;
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

} // namespace

void *resdata_exist(const char *id, unsigned idlen, unsigned parasize) {
	(void)parasize;
	resdata_entry_t *entry = find(id, idlen);
	return (entry != nullptr) ? entry->block : nullptr;
}

void *resdata_create(const char *id, unsigned idlen, unsigned parasize) {
	resdata_entry_t *entry = find(id, idlen);
	if(entry != nullptr) {
		return entry->block;
	}
	for(resdata_entry_t &slot : g_registry) {
		if(!slot.in_use) {
			void *block = std::malloc(static_cast<std::size_t>(parasize) * 16);
			if(block == nullptr) {
				return nullptr;
			}
			assert(idlen < sizeof(slot.id));
			std::memcpy(slot.id, id, idlen);
			slot.idlen = idlen;
			slot.block = block;
			slot.in_use = true;
			return block;
		}
	}
	assert(false && "resdata_create: registry full (MAX_RESDATA_ENTRIES)");
	return nullptr;
}
