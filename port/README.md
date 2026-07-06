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
- `audio/` -- YM2203 (OPN) emulation (`opn_chip.{hpp,cpp}`, wrapping vendored
  ymfm) and SDL3 audio device output (`audio_out.{hpp,cpp}`)
- `snd/` -- MDRV2 `.MDT` binary format parser and sequencer engine
  (`mdt_format.{hpp,cpp}`, `mdrv2_engine.{hpp,cpp}`)
- `third_party/ymfm/` -- vendored subset of aaronsgiles/ymfm (BSD-3)
- `assets/` -- vendored third-party data (fonts, ...), with license files
- `demo/` -- SDL3 presenter exercising the emulated hardware paths
- `app/` -- SDL3 host (`host.{hpp,cpp}`) shared by port executables, plus
  each executable's own entry point (`main_op.cpp` for `th01op`)
- `tests/` -- `ppm` golden-test helper, plus ctest unit and golden tests

## th01op (TH01 OP.EXE: title screen + options menu, M1)

`th01op` runs `th01/op_01.cpp`'s `main_op()` directly (see
`app/main_op.cpp`'s header comment for why it doesn't reproduce the original
DOS multi-entrypoint `main()` dispatcher). Its dependency closure -- every
original ReC98 TU `main_op()` actually calls into, and nothing from
REIIDEN.EXE/FUUIN.EXE's gameplay/stage/boss code -- is the `th01op_core`
CMake target; see that target's comment in `CMakeLists.txt` and
`/Users/geekjapan/dev/projects/touhou/OP_DEPS.md` for the survey it's based
on.

Without the original game's data files, `th01op` resolves an assets
directory (`$TH01_ASSETS_DIR`, else the path documented in SPEC.md, else the
current directory) and exits with a clear message if it doesn't contain
TH01's data (checked via `REIIDEN2.GRP`, the first file the title screen
loads) -- see `app/main_op.cpp` and `app/assets_dir.{hpp,cpp}`. With the
directory present, it starts the SDL host and renders the title screen for
real.

## Sound (MDRV98/MDRV2 reimplementation on ymfm, M4)

`th01/snd/mdrv2.h` (TH01's BGM/SE API) is implemented for real by
`shim/snd_mdrv2.cpp`, on top of:

- `audio/opn_chip.{hpp,cpp}` -- a thin wrapper around `ymfm::ym2203`
  (YM2203/OPN: FM*3 + SSG*3, matching TH01's actual PC-98 sound hardware)
- `snd/mdt_format.{hpp,cpp}` -- a parser for MDRV2's compiled `.MDT` BGM
  binary format, reverse-engineered from
  [Lmocinemod/MDTParsingTools](https://github.com/Lmocinemod/MDTParsingTools)
  (research material, not vendored code -- see `mdt_format.hpp`'s header
  comment for exactly what this is/isn't based on, since MDRV2's compiled
  object format has no primary-source spec)
- `snd/mdrv2_engine.{hpp,cpp}` -- the actual driver semantics
  reimplementation: a per-channel opcode-stream VM that drives `opn_chip` in
  real time, standing in for the real MDRV98.COM TSR
- `audio/audio_out.{hpp,cpp}` -- SDL3 playback device output

No real TH01 `.MDT`/`.MDE` assets are available in this environment, so
`snd/mdt_format` and `snd/mdrv2_engine` are tested against synthetic
fixtures built with `tests/mdt_synth.hpp` (same convention as
`tests/pf_synth.hpp` for `th01formats`) rather than real BGM files -- see
`tests/opn_chip_test.cpp`, `tests/mdt_format_test.cpp`, and
`tests/mdrv2_engine_test.cpp`.

**Known limitations** (see `mdrv2_engine.hpp`'s header comment for the full
list): MDRV2's exact tempo-timer formula isn't reverse-engineered, so tempo
is approximated; portamento, the hardware LFO opcodes, pan, fade, and the
bracket-colon loop variants are parsed (so the opcode stream stays in sync)
but don't affect audio; SSG channels play as flat-volume square waves; FM
volume is a uniform TL attenuation rather than per-algorithm carrier-only
attenuation. Effect sounds (`.MDE`, `mdrv2_se_load()`/`mdrv2_se_play()`) use
a different, not-yet-reverse-engineered format and remain silent no-ops.

## Binary-switching chain (OP.EXE -> REIIDEN.EXE -> FUUIN.EXE, M5)

On real hardware, TH01 is one combined "debloat" binary with multiple entry
points (`th01/pc98/entry.cpp`), and `game_switch_binary()`
(`th01/core/initexit.cpp`) switches between them via `execl()` of *itself*
with a different `argv[1]`. This port instead builds three separate
executables (`th01op`/`th01main`/`th01fuuin`, one per original .EXE, each
with its own `main()` under `app/`) -- so `entrypoint_exec()`
(`th01/pc98/entry.hpp`'s seam for this) is implemented per-binary in
`shim/op_globals.cpp`/`shim/main_globals.cpp`/`shim/fuuin_globals.cpp`, all
three now delegating to `shim/masterlib/chain_exec.{hpp,cpp}`.

**Design choice**: a real `execv()` into the sibling binary, replacing the
current process image, rather than in-process re-dispatch -- see
`chain_exec.hpp`'s header comment for the full rationale (closer match to
the original process-boundary architecture; lets `resident`/scoredat
survival actually get tested, since a leftover in-process pointer surviving
a function call proves nothing that a real process replacement doesn't).
Resident state (`th01/resident.hpp`'s `resident_t`, looked up via
`th01/core/resident.cpp`'s `ResData<T>`) is handed off across each `execv()`
by serializing every `shim/masterlib/resdata.cpp` block to a flat file
(path via the `TH01_PORT_STATE_FILE` environment variable, auto-generated
from the process's PID if unset -- stable across the whole chain, since
`execv()` never changes the PID -- and inherited automatically by every
hop); the next process's first `resdata_exist()`/`resdata_create()` call
transparently loads it back in and deletes the file. Sibling binaries are
resolved via `PORT_CHAIN_BIN_DIR`, baked in at build time (same convention
as `pc98emu`'s `PC98EMU_FONT_DIR`; overridable via `TH01_PORT_BIN_DIR`).

`app/main_reiiden.cpp` only seeds a fresh resident
(`app/reiiden_bootstrap.hpp`) when none already exists -- otherwise it would
silently discard whatever the previous hop (OP.EXE's `start_game()`, or
REIIDEN.EXE's own self-re-exec for a boss-adjacent `stage_id` transition)
put there. `app/main_fuuin.cpp` similarly picks the real
resident-driven ending path over the hidden `"t1"`/`"t2"` preview shortcut
whenever a resident structure exists.

**Acceptance test**: `tests/fullchain_binary_switch_test.cpp` can't drive
this the way `menu_nav_test.cpp`/`stage1_holdshot_test.cpp` do (an in-process
director thread injecting synthetic keys) -- `execv()` replaces the whole
process, director thread included, the instant `entrypoint_exec()` runs. So
the actual scripted driving lives in the production binaries themselves,
gated off by default behind `TH01_PORT_AUTOPILOT_OP`/
`TH01_PORT_AUTOPILOT_MAIN` (see `app/chain_autopilot.hpp` and the autopilot
threads in `app/main_op.cpp`/`app/main_reiiden.cpp`), reusing
`menu_nav_test.cpp`'s menu-navigation technique and
`stage1_holdshot_test.cpp`'s `test_damage` debug-cheat technique
respectively. The test itself `fork()`s + `execl()`s the real `th01op`
binary with those variables set and waits for the whole chain (OP menu ->
START -> real `execv()` -> stage 1's 4 card-wall segments, accelerated ->
a boss-adjacent `stage_id` transition -> a second real `execv()` into a
fresh `th01main` process) to exit(0) on its own -- verifying with a
sentinel written to `resident_t::unused_3` (a field no original ZUN code
ever touches) that state actually survives both `execv()` hops, not just
that the process didn't crash.

`tests/fuuin_ending_test.cpp` covers the other end of the chain: it seeds a
cleared-game resident directly (`app/fuuin_bootstrap.hpp`) and calls
`main_cutscene()` with real (non-`"t"`-prefixed) args, driving the actual
`end_init()` path -- letting the real `ED*.GRP` ending slideshow render
(inspectable via `TH01_DUMP_FRAME`) and proving both the verdict screen's
input-gated wait and `th01/hiscore/regist.cpp`'s high-score name-entry loop
are escapable headlessly (synthetic Return/`input_ok` presses). It sets
`TH01_PORT_CHAIN_STOP=1` (`chain_exec.cpp`'s test-only escape hatch) so the
ending's own final `game_switch_binary(EP_OP)` confirms the switch decision
without actually `execv()`-ing into `th01op` -- irrelevant to what this test
verifies.

## Third-party assets

`port/assets/font/` vendors the Shinonome 16-dot JIS bitmap font (東雲フォント)
from [The Electronic Font Open Laboratory](http://openlab.ring.gr.jp/efont/),
used by `pc98emu/text.cpp` to render TRAM's halfwidth (8x16, JIS X0201) and
fullwidth (16x16, JIS X0208) glyphs. **License: Public Domain** -- see
`port/assets/font/LICENSE` and `AUTHORS` (copied verbatim from upstream) and
`port/assets/font/README.md` for provenance and how the vendored `.bdf`
files were produced from the upstream source distribution.

`port/third_party/ymfm/` vendors a subset of
[aaronsgiles/ymfm](https://github.com/aaronsgiles/ymfm) (YM2203/OPN chip
emulation). **License: BSD 3-Clause** -- see `port/third_party/ymfm/LICENSE`
and `port/third_party/ymfm/README.md` for exactly which files are vendored
and why.
