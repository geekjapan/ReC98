# shim/masterlib

Subset of `libs/master.lib/master.hpp` / `libs/master.lib/pc98_gfx.hpp` needed
to compile and link TH01 `OP.EXE`'s dependency closure (see
`/Users/geekjapan/dev/projects/touhou/OP_DEPS.md`, and the DAG in
`WORKFLOW.md`, task T3).

Scope was derived by grepping actual (non-macro-shadowed) master.lib call
sites across the OP.EXE closure file set (`th01/op_01.cpp`,
`th01/core/{initexit,resident}.cpp`,
`th01/hardware/{egc,egcrect,egcstart,frmdelay,graph,grp2xscs,grp_text,
grppsafx,planar,vsync}.cpp`, `th01/formats/grp.cpp`) rather than implementing
the whole header. Several master.lib-looking names in that closure
(`grcg_setcolor`, `grcg_off`, `z_graph_*`, `z_palette_*`) are actually
th01-local macros/functions (`th01/hardware/graph.cpp`,
`th01/hardware/grcg.hpp`) that shadow or bypass the real master.lib calls
with direct BIOS/port-I/O reimplementations -- those are NOT part of this
shim; they're existing ReC98 source, ported as-is by whichever task compiles
them (T9 op-bringup).

## Implemented (real master.lib semantics)

- **File I/O** (`file_io.cpp`): `file_ropen/read/close/create/append/write/
  seek/exist/delete/size` -- single implicit "current file" over stdio
  `FILE*`, matching master.lib's one-file-at-a-time buffered API.
  `dos_puts2`, `dos_free` (paired with `resdata_create`'s `malloc`).
- **Resident data** (`resdata.cpp`): `resdata_exist`, `resdata_create` --
  process-local id->pointer registry (real master.lib persists this across
  `exec()` chains via a DOS TSR paragraph; the port's binaries all run in one
  process, so a static registry gives the same "survives
  `game_switch_binary()`" behavior). Deliberately does NOT zero-initialize
  new blocks: `th01/op_01.cpp`'s `start_game()` comment notes ZUN code relies
  on this.
- **Keyboard** (`keyboard.cpp`): `key_start`/`key_end` (no-ops -- no TSR to
  install under SDL), `key_sense_bios` (ORs `key_sense()` from
  `port/shim/input.cpp`, T4, across all BIOS keygroups as an "any key
  pressed" proxy). `key_sense(int)` itself is T4's, not redeclared here.
- **RNG** (`random.cpp`): `random_seed`/`irand()`, bit-exact port of
  `libs/master.lib/random.asm`'s 32-bit LCG (multiplier `0x015A4E35`,
  increment 1, result = high 16 bits masked to 15 bits).
- **EGC/graphics init** (`gfx.cpp`): `egc_on/off/start` (thin wrappers over
  `pc98emu_egc_activate/deactivate`), `graph_start` (palette-to-black +
  page 0 reset + VRAM clear, per `graph_start.asm`), `grcg_boxfill` (GRCG
  rectangle fill via `grcg_vram_write()`, replicating the same edge-byte
  bit-masking as `th01/hardware/graph.cpp`'s `graph_r_hline()`). Also defines
  the `Palettes`/`PaletteTone` globals and a real `palette_show()`, both
  needed internally by `graph_start()`.

## Deliberately NOT implemented

Not called anywhere in the OP.EXE closure (verified by grep, not just
absent from `master.hpp`): joystick (`js_*`), EMS (`ems_*`), heap
(`mem_*`/`hmem_*`/`smem_*`), machine ID (`get_machine`), BGM beep
(`bgm_*`), resident palettes (`respal_*`), superimpose sprites (`super_*`),
TRAM text (`text_*`), gaiji (`gaiji_*`), clipping (`grc_*`), the rest of
GRCG/EGC drawing primitives, and `palette_{black,white}_{in,out}`/
`palette_entry_rgb` (the *real* master.lib fades -- z_palette_black_in()
etc. in `th01/hardware/graph.cpp` are separate th01-local functions that
bypass master.lib entirely). Add them here if a later milestone's dependency
survey finds a real (non-shadowed) call site.

Stub-with-assert, declared but exercised by no OP.EXE code path (`file_io.cpp`):
`dos_getch/putch/puts/create/close/write/seek/axdx` -- the raw DOS-handle
half of the "DOS" section in `master.hpp`. Only the buffered `file_*` API is
actually used by this closure; these fail loudly (assert) rather than
silently misbehaving if some later TU ends up calling them.
