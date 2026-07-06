# TH01 SDL Port -- port/

See `/Users/geekjapan/dev/projects/touhou/SPEC.md` for the component spec and
`/Users/geekjapan/dev/projects/touhou/WORKFLOW.md` for the task DAG and merge
process.

## Build

```
cmake -B port/build -S port
cmake --build port/build
```

Requires C++17, CMake >= 3.24, SDL3 (Homebrew: `brew install sdl3`).
`port/build/` is gitignored -- never commit it.

## Test

```
ctest --test-dir port/build --output-on-failure
```

## CI gate (local, no GitHub Actions)

```
port/ci.sh            # configure + build + ctest; exits nonzero on any failure
port/ci.sh --clean    # wipe port/build/ first, forcing a full rebuild
```

Run this before merging any worker branch into `sdl-port` -- it's the
coordinator's merge gate per WORKFLOW.md. A nonzero exit means don't merge.

## Golden tests

`port/tests/ppm.{hpp,cpp}` is the shared golden-test helper: it reads/writes
binary PPM (P6) images and compares two images channel-by-channel with a
configurable tolerance. Golden tests use tolerance 0 (exact byte match).

Fixtures live under `port/tests/golden/`. A golden test renders a
deterministic scene, converts the RGBA8888 output to a `ppm::Image` via
`ppm::from_rgba8888()`, and compares it against the checked-in fixture with
`ppm::compare(..., /*tolerance=*/0, ...)`.

To (re)generate a fixture after an intentional rendering change, run the
test binary directly with `--generate`, e.g.:

```
port/build/test_render_golden --generate
git add port/tests/golden/render_basic.ppm
```

Then re-run `port/ci.sh` to confirm the test now passes against the
regenerated fixture, and review the diff (e.g. open it in an image viewer
that reads PPM, or `xxd` a byte range) before committing.

## Layout

- `pc98emu/` -- PC-98 hardware emulation (VRAM/GRCG/EGC/text), no ReC98 header deps
- `assets/` -- vendored third-party data (fonts, ...), with license files
- `demo/` -- SDL3 presenter exercising the emulated hardware paths
- `tests/` -- `ppm` golden-test helper, plus ctest unit and golden tests

## Third-party assets

`port/assets/font/` vendors the Shinonome 16-dot JIS bitmap font (東雲フォント)
from [The Electronic Font Open Laboratory](http://openlab.ring.gr.jp/efont/),
used by `pc98emu/text.cpp` to render TRAM's halfwidth (8x16, JIS X0201) and
fullwidth (16x16, JIS X0208) glyphs. **License: Public Domain** -- see
`port/assets/font/LICENSE` and `AUTHORS` (copied verbatim from upstream) and
`port/assets/font/README.md` for provenance and how the vendored `.bdf`
files were produced from the upstream source distribution.
