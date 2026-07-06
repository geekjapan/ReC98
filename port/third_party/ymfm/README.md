# ymfm (vendored subset)

Source: https://github.com/aaronsgiles/ymfm
Commit: 17decfae857b92ab55fbb30ade2287ace095a381 (2025-01-28)
License: BSD 3-Clause (see `LICENSE`, copied verbatim from upstream)

Only the translation-unit closure needed to build the `ymfm::ym2203` (YM2203 /
OPN) chip model is vendored -- not the full library (which also covers
OPM/OPL/OPQ/OPX/OPZ and misc chips we don't need for TH01's OPN-only MDRV2
sound hardware):

- `ymfm.h` -- core definitions shared by every chip family
- `ymfm_fm.h` / `ymfm_fm.ipp` -- FM operator/channel engine templates
- `ymfm_ssg.h` / `ymfm_ssg.cpp` -- SSG (AY-3-8910-compatible PSG) engine, used
  by YM2203's 3 SSG channels
- `ymfm_adpcm.h` / `ymfm_adpcm.cpp` -- pulled in transitively by
  `ymfm_opn.h`/`.cpp` (which also defines the YM2608/YM2610 ADPCM-capable chip
  classes in the same translation unit); unused by `ym2203` itself but
  required to compile and link `ymfm_opn.cpp` as-is
- `ymfm_opn.h` / `ymfm_opn.cpp` -- OPN chip family, including `ym2203`

`port/audio/opn_chip.{hpp,cpp}` is the thin wrapper consumed by the rest of
the port; nothing else in `port/` includes ymfm headers directly.
