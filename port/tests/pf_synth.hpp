// port/tests/pf_synth.hpp
// -----------------------------------------------------------------------
// Test-only synthetic packfile (.PAR-style, th01/formats/pf.cpp) encoder.
// Mirrors pf.cpp's private on-disk pf_header_t layout and its "encraption"
// (XOR with arc_key) + optional RLE compression, so tests can build minimal
// packfiles and load them through the real arc_load()/arc_file_*() code.

#ifndef PORT_TESTS_PF_SYNTH_HPP
#define PORT_TESTS_PF_SYNTH_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pf_synth {

constexpr int kFileCount = 64; // pf.cpp's FILE_COUNT
constexpr int kFnLen = 13;     // game/pf.h's PF_FN_LEN
constexpr uint16_t kTypeCompressed = 0x9595;
constexpr uint16_t kTypeUncompressed = 0x0001; // anything with a nonzero low byte

// Mirror of pf.cpp's private pf_header_t. Same compiler/flags as pf.cpp, so
// this has an identical layout (no padding: 2+1+13=16, then four aligned
// int32_t fields -- sizeof == 32, verified by the static_assert below).
#pragma pack(push, 1)
struct Header {
	uint16_t type;
	int8_t aux;
	char fn[kFnLen];
	int32_t packsize;
	int32_t orgsize;
	int32_t offset;
	int32_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 32, "pf_synth::Header must match pf.cpp's on-disk pf_header_t");

// RLE-encodes `data` using the scheme th01/formats/pf.cpp's unrle() decodes:
// a run of N>=2 identical bytes starts with the byte twice followed by a
// repeat count (N-2, capped so the initial group covers at most 257 bytes);
// a run of 1 is stored as a single literal byte. See pf.cpp's unrle() for
// the decoder and its inline comment for the scheme.
//
// A run longer than 257 needs a second (or further) group to cover the
// overflow -- but that continuation group must NOT repeat the leading pair:
// unrle() leaves its "last output byte" state (rle_next_byte) equal to the
// run's byte after the first group finishes, so the very next byte in the
// stream is compared against that carried-over value. A plain second
// [b, b, count] triple would make unrle() consume its own leading `b` as
// that comparison partner and then misread its second `b` as the repeat
// count. Continuation groups therefore use the 2-byte form [b, count]: the
// single `b` pairs against the carry-over (producing one free repeat), and
// `count` supplies the rest (covering up to 256 further repeats).
inline std::vector<uint8_t> rle_encode(const std::vector<uint8_t> &data) {
	std::vector<uint8_t> out;
	size_t i = 0;
	while(i < data.size()) {
		uint8_t b = data[i];
		size_t run = 1;
		while((i + run) < data.size() && data[i + run] == b) {
			run++;
		}
		if(run == 1) {
			out.push_back(b);
			i += 1;
			continue;
		}
		size_t first_chunk = (run > 257) ? 257 : run;
		out.push_back(b);
		out.push_back(b);
		out.push_back(static_cast<uint8_t>(first_chunk - 2));
		i += first_chunk;

		size_t remaining = run - first_chunk;
		while(remaining > 0) {
			size_t chunk = (remaining > 256) ? 256 : remaining;
			out.push_back(b);
			out.push_back(static_cast<uint8_t>(chunk - 1));
			i += chunk;
			remaining -= chunk;
		}
	}
	return out;
}

// XORs every byte with [key], matching pf.cpp's crapt()/cache_next().
inline void crapt(std::vector<uint8_t> &buf, uint8_t key) {
	for(auto &b : buf) {
		b = static_cast<uint8_t>(b ^ key);
	}
}

struct Entry {
	std::string name; // e.g. "TEST.GRC"; must fit PF_FN_LEN - 1 characters
	std::vector<uint8_t> data;
	bool compressed = false;
};

// Builds a full synthetic packfile image (fixed-size header block, followed
// by each entry's, possibly RLE-compressed, "encrapted" data) that
// arc_load()/arc_file_open() can parse. [arc_key] must match the value the
// test points th01/formats/pf.cpp's `arc_key` global at before loading.
inline std::vector<uint8_t> build(const std::vector<Entry> &entries, uint8_t arc_key = 0) {
	std::vector<Header> headers(kFileCount);
	std::memset(headers.data(), 0, sizeof(Header) * kFileCount);

	const size_t header_bytes = sizeof(Header) * kFileCount;
	std::vector<uint8_t> data_blob;

	for(size_t i = 0; i < entries.size(); i++) {
		const Entry &e = entries[i];
		Header &h = headers[i];

		std::vector<uint8_t> stored = e.compressed ? rle_encode(e.data) : e.data;
		crapt(stored, arc_key);

		h.type = e.compressed ? kTypeCompressed : kTypeUncompressed;
		h.aux = 3;
		std::memset(h.fn, 0, kFnLen);
		// arc_load() un-inverts (~) each on-disk filename byte up to the
		// null terminator, so the stored bytes must be pre-inverted.
		for(size_t c = 0; c < e.name.size() && c < static_cast<size_t>(kFnLen - 1); c++) {
			h.fn[c] = static_cast<char>(~static_cast<uint8_t>(e.name[c]));
		}
		h.packsize = static_cast<int32_t>(stored.size());
		h.orgsize = static_cast<int32_t>(e.data.size());
		h.offset = static_cast<int32_t>(header_bytes + data_blob.size());
		h.reserved = 0;

		data_blob.insert(data_blob.end(), stored.begin(), stored.end());
	}

	std::vector<uint8_t> out(header_bytes + data_blob.size());
	std::memcpy(out.data(), headers.data(), header_bytes);
	if(!data_blob.empty()) {
		std::memcpy(out.data() + header_bytes, data_blob.data(), data_blob.size());
	}
	return out;
}

} // namespace pf_synth

#endif // PORT_TESTS_PF_SYNTH_HPP
