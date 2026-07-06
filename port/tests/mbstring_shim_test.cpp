// Unit test for port/compat/mbstring_shim: the host reimplementation of
// Turbo C++'s <mbctype.h>/<mbstring.h> Shift-JIS helpers used by
// th01/hardware/{graph,grppsafx,ztext}.cpp.

#include <cstdio>
#include <cstdint>

#include "port/compat/mbstring_shim.hpp"

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
	// Lead byte ranges: 0x81-0x9F, 0xE0-0xFC.
	expect(_ismbblead(0x81), "0x81 is a lead byte (range start)");
	expect(_ismbblead(0x9F), "0x9F is a lead byte (range end)");
	expect(_ismbblead(0xE0), "0xE0 is a lead byte (second range start)");
	expect(_ismbblead(0xFC), "0xFC is a lead byte (second range end)");
	expect(!_ismbblead(0x80), "0x80 is not a lead byte");
	expect(!_ismbblead(0xA0), "0xA0 (kana range) is not a lead byte");
	expect(!_ismbblead(0x41), "ASCII 'A' is not a lead byte");

	// Halfwidth katakana range: 0xA1-0xDF.
	expect(_ismbbkana(0xA1), "0xA1 is halfwidth kana (range start)");
	expect(_ismbbkana(0xDF), "0xDF is halfwidth kana (range end)");
	expect(!_ismbbkana(0xA0), "0xA0 is not halfwidth kana");
	expect(!_ismbbkana(0xE0), "0xE0 is not halfwidth kana");

	// SJIS -> JIS: well-known reference pairs (JIS X0208 row 4, "あ").
	// SJIS 0x82A0 <-> JIS 0x2422 ("あ", hiragana A).
	expect(
		_mbcjmstojis(0x82A0) == 0x2422, "SJIS 0x82A0 ('あ') converts to JIS 0x2422"
	);
	// SJIS 0x8140 <-> JIS 0x2121 (JIS X0208 row 1 col 1, ideographic space).
	expect(
		_mbcjmstojis(0x8140) == 0x2121, "SJIS 0x8140 converts to JIS 0x2121"
	);
	// SJIS 0x9872 ("腕") <-> JIS 0x4F53 -- an arbitrary mid-table kanji
	// reference pair, high lead byte / trail byte >= 0x9F branch.
	expect(
		_mbcjmstojis(0x9872) == 0x4F53, "SJIS 0x9872 ('腕') converts to JIS 0x4F53"
	);

	if(g_failures == 0) {
		std::printf("mbstring_shim_test: all checks passed\n");
	}
	return g_failures == 0 ? 0 : 1;
}
