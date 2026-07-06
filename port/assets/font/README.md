# Font assets: Shinonome 16-dot JIS bitmap font

`shinonome16_hankaku.bdf` (JISX0201.1976, 8x16, halfwidth ASCII + halfwidth
katakana, 221 glyphs) and `shinonome16_kanji.bdf` (JISX0208.1990, 16x16,
fullwidth Gothic kanji, 6879 glyphs) are standard BDF (Glyph Bitmap
Distribution Format) text files, parsed at load time by
`port/pc98emu/text.cpp`.

`BDF ENCODING` values are the JIS codepoint itself: for the kanji font this
is the same two-byte, `(row_byte << 8) | col_byte` (each byte 0x21-0x7E)
representation as ReC98's own `jis_t`; for the hankaku font it's the plain
JIS X0201 byte (`ank_t`), 0x20-0x7E for ASCII and 0xA1-0xDF for halfwidth
katakana.

## Provenance

Source: [The Electronic Font Open Laboratory](http://openlab.ring.gr.jp/efont/)'s
Shinonome font family (東雲フォント), originally designed by Yasuyuki
Furukawa and donated to /efont/, 16-dot Gothic variant. Mirror used:
https://github.com/code4fukui/shinonome-font (`16/hankaku`, `16/latin1`,
`16/kanjic`).

License: **Public Domain**, see `LICENSE` (and `AUTHORS`) in this directory,
copied verbatim from the upstream repository.

## How the .bdf files were produced

The upstream repository ships its font data as `.bit` files (BDF with the
`BITMAP` section written as ASCII art instead of hex) plus small Perl
conversion tools, not as ready-to-use `.bdf`. To regenerate:

```sh
# Kanji (JISX0208, standalone -- no merge needed)
curl -sO https://raw.githubusercontent.com/code4fukui/shinonome-font/main/16/kanjic/font_src.bit
curl -sO https://raw.githubusercontent.com/code4fukui/shinonome-font/main/tools/bit2bdf.in
perl <(sed 's/^#!@PERL@/#!\/usr\/bin\/perl/' bit2bdf.in) font_src.bit > shinonome16_kanji.bdf

# Hankaku (JISX0201: latin1's ASCII half merged with the katakana diff)
curl -sO https://raw.githubusercontent.com/code4fukui/shinonome-font/main/16/hankaku/font_src_diff.bit
curl -sO https://raw.githubusercontent.com/code4fukui/shinonome-font/main/16/latin1/font_src.bit
curl -sO https://raw.githubusercontent.com/code4fukui/shinonome-font/main/tools/bdfmerge.in
perl <(sed 's/^#!@PERL@/#!\/usr\/bin\/perl/' bdfmerge.in) font_src_diff.bit font_src.bit \
    | perl <(sed 's/^#!@PERL@/#!\/usr\/bin\/perl/' bit2bdf.in) - > shinonome16_hankaku.bdf
```

Then substitute the unresolved `@FOUNDRY@`/`@FAMILY@` autoconf placeholders
(`efont`/`Shinonome`) and copy `LICENSE`/`AUTHORS` from the repository root.
