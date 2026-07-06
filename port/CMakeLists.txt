cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

project(th01port CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH /opt/homebrew)
find_package(SDL3 CONFIG REQUIRED)

# ---------------------------------------------------------------------------
# pc98emu: software emulation of the PC-98 planar VRAM / GRCG / EGC hardware.
# ---------------------------------------------------------------------------
add_library(pc98emu STATIC
	pc98emu/pc98emu.cpp
	pc98emu/vram.cpp
	pc98emu/grcg.cpp
	pc98emu/egc.cpp
	pc98emu/text.cpp
)
target_include_directories(pc98emu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Vendored JIS bitmap font (see port/assets/font/README.md), loaded at
# runtime by pc98emu/text.cpp -- baked in as an absolute source-tree path
# rather than copied next to the binary, since this project isn't packaged
# for distribution yet.
target_compile_definitions(pc98emu PRIVATE PC98EMU_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/font")

# ---------------------------------------------------------------------------
# th01port_demo: SDL3 presenter exercising the emulated hardware paths.
# ---------------------------------------------------------------------------
add_executable(th01port_demo
	demo/main.cpp
)
target_link_libraries(th01port_demo PRIVATE pc98emu SDL3::SDL3)

# ---------------------------------------------------------------------------
# compat: REC98_PORT Borland/planar.h shim (header-only). Consumed directly
# by ReC98's own headers (pc98.h/platform.h/planar.h) under #ifdef
# REC98_PORT, so it needs both the port/ tree (for pc98emu/*.hpp) and the
# ReC98 repo root (for pc98.h/planar.h/platform.h and the
# "port/compat/vram_ref.hpp" include planar.h reaches for) on its include
# path.
# ---------------------------------------------------------------------------
add_library(compat INTERFACE)
target_include_directories(compat INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(compat INTERFACE REC98_PORT=1)
target_compile_options(compat INTERFACE -Wno-unknown-pragmas)
target_link_libraries(compat INTERFACE pc98emu)

# ---------------------------------------------------------------------------
# shim/snd_stub: silent no-op implementation of th01/snd/mdrv2.h. MDRV98
# reimplementation is a later milestone (T13); this just lets every TU that
# calls into the sound API compile and link.
# ---------------------------------------------------------------------------
add_library(sndstub STATIC
	shim/snd_stub.cpp
)
target_link_libraries(sndstub PRIVATE compat)

# ---------------------------------------------------------------------------
# ymfm (vendored subset): BSD-3 YM2203/OPN chip emulation core. Only the
# translation-unit closure needed for ymfm::ym2203 is vendored -- see
# port/third_party/ymfm/README.md for exactly what and why.
# ---------------------------------------------------------------------------
add_library(ymfm STATIC
	third_party/ymfm/src/ymfm_opn.cpp
	third_party/ymfm/src/ymfm_ssg.cpp
	third_party/ymfm/src/ymfm_adpcm.cpp
)
target_include_directories(ymfm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src)
# Third-party code: don't hold it to this project's own warning bar.
target_compile_options(ymfm PRIVATE -w)

# ---------------------------------------------------------------------------
# audio/opn_chip: thin wrapper around ymfm::ym2203 (register write + mono PCM
# render). This is TH01's actual sound hardware target -- see SPEC.md/T13.
# ---------------------------------------------------------------------------
add_library(portaudio_opn STATIC
	audio/opn_chip.cpp
)
target_include_directories(portaudio_opn PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(portaudio_opn PRIVATE ymfm)

# ---------------------------------------------------------------------------
# audio/audio_out: SDL3 audio device output, fed by a background thread
# pulling PCM from a Renderer callback. Not exercised by ctest (opening a
# real playback device isn't guaranteed in headless/CI); see
# audio_out.hpp's header comment.
# ---------------------------------------------------------------------------
add_library(portaudio_out STATIC
	audio/audio_out.cpp
)
target_include_directories(portaudio_out PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(portaudio_out PUBLIC SDL3::SDL3)

# ---------------------------------------------------------------------------
# snd/mdt_format + snd/mdrv2_engine: MDRV2 .MDT binary parser and the
# opcode-stream sequencer that drives port::audio::OpnChip in real time --
# TH01's MDRV98 driver semantics, reimplemented on ymfm. See
# mdt_format.hpp/mdrv2_engine.hpp's header comments for scope, provenance,
# and known limitations.
# ---------------------------------------------------------------------------
add_library(sndmdt STATIC
	snd/mdt_format.cpp
	snd/mdrv2_engine.cpp
)
target_include_directories(sndmdt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(sndmdt PUBLIC portaudio_opn)

# ---------------------------------------------------------------------------
# shim/snd_mdrv2: real (non-stub) implementation of th01/snd/mdrv2.h, built
# on sndmdt + portaudio_out. Effect sounds (.MDE) aren't reverse-engineered
# yet, so se_load()/se_play() remain no-ops -- see snd_mdrv2.cpp's header
# comment. This is what th01op_core links for the M4 milestone (OP BGM
# actually playing); `sndstub` above is kept around for its own test and as
# a documented fallback.
# ---------------------------------------------------------------------------
add_library(sndmdrv2 STATIC
	shim/snd_mdrv2.cpp
)
target_link_libraries(sndmdrv2 PRIVATE compat sndmdt portaudio_out)

# ---------------------------------------------------------------------------
# shim/input: SDL3 keyboard state -> PC-98 BIOS keygroup bitmap (key_sense()),
# consumed by th01/hardware/input.cpp exactly like the original
# KEY_SENSE.ASM/BIOS INT 18h path.
# ---------------------------------------------------------------------------
add_library(shim STATIC
	shim/input.cpp
)
target_include_directories(shim PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(shim PUBLIC SDL3::SDL3)

# ---------------------------------------------------------------------------
# shim/masterlib: subset of libs/master.lib/master.hpp + pc98_gfx.hpp needed
# by TH01 OP.EXE's dependency closure (file I/O, resident data, keyboard
# init, RNG, EGC/GRCG/graph init). See shim/masterlib/README.md for the
# exact function inventory and what's deliberately not implemented.
# key_sense_bios() calls into shim/input's key_sense(), hence the link dep.
# ---------------------------------------------------------------------------
add_library(masterlib STATIC
	shim/masterlib/file_io.cpp
	shim/masterlib/resdata.cpp
	shim/masterlib/keyboard.cpp
	shim/masterlib/random.cpp
	shim/masterlib/gfx.cpp
)
target_link_libraries(masterlib PRIVATE compat shim)

# ---------------------------------------------------------------------------
# th01formats: th01/formats/*.cpp (grp/grc/grz/pf/ptn/bos/scoredat -- cfg has
# no .cpp, just struct declarations) compiled as-is under REC98_PORT, plus
# port/shim/piload_stub.cpp (a synthetic-format stand-in for libs/piloadc's
# real 16-bit-assembly .PI/.GRP decoder, see that file's header comment).
# -include forces compat/borland.hpp before every original ReC98 source file
# here, exactly like it would be available via <dos.h> et al. under Turbo
# C++ -- see port/compat/borland.hpp's own header comment.
# -Wno-register: th01/formats/grz.cpp uses the C++98 `register` keyword,
# removed in C++17; harmless under a strict host compiler.
# -DGAME=1: TH01's compile-time game ID, read by e.g. defconv.h (DEFCONV
# calling convention) and th01/formats/ptn.hpp's th01/hardware/egc.h include.
# ---------------------------------------------------------------------------
add_library(th01formats STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/formats/pf.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/formats/grp.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/formats/grc.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/formats/grz.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/formats/ptn.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/formats/bos.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/formats/scoredat.cpp
	shim/piload_stub.cpp
	shim/egc_th01_shim.cpp
)
target_compile_definitions(th01formats PRIVATE GAME=1)
target_compile_options(th01formats PRIVATE
	-Wno-register
	-include ${CMAKE_CURRENT_SOURCE_DIR}/compat/borland.hpp
)
target_link_libraries(th01formats PRIVATE compat masterlib)

# ---------------------------------------------------------------------------
# apphost: SDL3 presentation host shared by port/app/ entry points -- owns
# the window/renderer/streaming-texture triple and the "present one frame"
# step (port::host_present_frame()) that th01/hardware/vsync.hpp's
# REC98_PORT branch calls into, turning ReC98's vsync-interrupt busy-waits
# into a real frame-paced blocking wait (see SPEC.md's app/host design).
# ---------------------------------------------------------------------------
add_library(apphost STATIC
	app/host.cpp
)
target_link_libraries(apphost PUBLIC pc98emu shim SDL3::SDL3)

# ---------------------------------------------------------------------------
# th01op_core: TH01 OP.EXE's (title screen + options menu) dependency
# closure -- th01/op_01.cpp itself, plus every original ReC98 TU it actually
# calls into (see OP_DEPS.md and port/shim/masterlib/README.md for the
# survey this scope is derived from). Deliberately excludes REIIDEN.EXE/
# FUUIN.EXE-only code (gameplay, stages, bosses, tram_x16/scoredat/hiscore):
# op_01.cpp never reaches any of it.
#
# port/compat/x86real_shim.cpp and port/shim/op_globals.cpp are this
# component's own new shims (int86()/outportb()/inportb()/getvect()/
# setvect()/__emit__(), and the two REIIDEN-input globals + entrypoint_exec()
# stub op_01.cpp's closure needs -- see those files' header comments).
# ---------------------------------------------------------------------------
add_library(th01op_core STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/op_01.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/core/initexit.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/core/resident.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/graph.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grppsafx.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grp_text.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/egc.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/frmdelay.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/vsync.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/ztext.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../platform/x86real/pc98/font.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../platform/x86real/pc98/grcg.cpp
	shim/op_globals.cpp
	compat/x86real_shim.cpp
)
target_compile_definitions(th01op_core PRIVATE GAME=1)
target_compile_options(th01op_core PRIVATE
	-Wno-register
	# Matches Borland's assumed-unsigned `char` default: shiftjis.hpp makes
	# shiftjis_t plain `char` under REC98_PORT (so shiftjis string-table
	# literals type-match `const shiftjis_t*` without a cast at every one of
	# their many call sites), and this restores the unsigned-byte arithmetic
	# (_ismbblead() et al.) that code assumes of it.
	-funsigned-char
	-include ${CMAKE_CURRENT_SOURCE_DIR}/compat/borland.hpp
)
target_link_libraries(th01op_core PRIVATE compat masterlib th01formats sndmdrv2 apphost)

# ---------------------------------------------------------------------------
# th01op: the M1 port executable. port/app/main_op.cpp resolves an assets
# directory, fails with a clear message if TH01's data files aren't there
# (see that file's header comment for why this happens before any original
# game code runs), then calls th01/op_01.cpp's main_op() directly.
# ---------------------------------------------------------------------------
add_library(appassetsdir STATIC
	app/assets_dir.cpp
)
target_include_directories(appassetsdir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(th01op
	app/main_op.cpp
)
target_link_libraries(th01op PRIVATE th01op_core appassetsdir apphost compat)

# ---------------------------------------------------------------------------
# Tests: ppm golden-test helper + ctest unit/golden tests. No GitHub Actions
# dependency -- port/ci.sh drives configure+build+ctest as the local merge
# gate (see WORKFLOW.md).
# ---------------------------------------------------------------------------
enable_testing()

add_library(porttestutil STATIC
	tests/ppm.cpp
)
target_include_directories(porttestutil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_ppm tests/test_ppm.cpp)
target_link_libraries(test_ppm PRIVATE porttestutil)
add_test(NAME ppm_helper COMMAND test_ppm)

add_executable(test_render_golden tests/test_render_golden.cpp)
target_link_libraries(test_render_golden PRIVATE pc98emu porttestutil)
target_compile_definitions(test_render_golden PRIVATE
	PORT_TESTS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
)
add_test(NAME render_golden COMMAND test_render_golden)

add_executable(th01port_compat_test
	tests/compat_vram_ref_test.cpp
)
target_link_libraries(th01port_compat_test PRIVATE compat)
add_test(NAME compat_vram_ref COMMAND th01port_compat_test)

add_executable(test_shim_input tests/shim_input_test.cpp)
target_link_libraries(test_shim_input PRIVATE shim)
add_test(NAME shim_input COMMAND test_shim_input)

add_executable(pc98emu_egc_test tests/egc_test.cpp)
target_link_libraries(pc98emu_egc_test PRIVATE pc98emu)
add_test(NAME pc98emu_egc_test COMMAND pc98emu_egc_test)

add_executable(pc98emu_text_test tests/text_test.cpp)
target_link_libraries(pc98emu_text_test PRIVATE pc98emu porttestutil)
target_compile_definitions(pc98emu_text_test PRIVATE
	PORT_TESTS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
)
add_test(NAME pc98emu_text_test COMMAND pc98emu_text_test)

add_executable(test_snd_stub tests/snd_stub_test.cpp)
target_link_libraries(test_snd_stub PRIVATE sndstub compat)
add_test(NAME snd_stub COMMAND test_snd_stub)

# ---------------------------------------------------------------------------
# T13 (sound-m4) tests: OPN chip tone playback, MDT binary format parsing
# (synthetic fixtures, see tests/mdt_synth.hpp), and the MDRV2 sequencer
# engine driving it end-to-end. No real TH01 .MDT assets are available in
# this environment -- see mdt_format.hpp/mdrv2_engine.hpp for what's
# reverse-engineered vs. approximated.
# ---------------------------------------------------------------------------
add_executable(test_opn_chip tests/opn_chip_test.cpp)
target_link_libraries(test_opn_chip PRIVATE portaudio_opn)
add_test(NAME opn_chip COMMAND test_opn_chip)

add_executable(test_mdt_format tests/mdt_format_test.cpp)
target_link_libraries(test_mdt_format PRIVATE sndmdt)
add_test(NAME mdt_format COMMAND test_mdt_format)

add_executable(test_mdrv2_engine tests/mdrv2_engine_test.cpp)
target_link_libraries(test_mdrv2_engine PRIVATE sndmdt)
add_test(NAME mdrv2_engine COMMAND test_mdrv2_engine)

add_executable(test_masterlib_file_io tests/masterlib_file_io_test.cpp)
target_link_libraries(test_masterlib_file_io PRIVATE masterlib compat)
target_compile_definitions(test_masterlib_file_io PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME masterlib_file_io COMMAND test_masterlib_file_io)

add_executable(test_masterlib_resdata tests/masterlib_resdata_test.cpp)
target_link_libraries(test_masterlib_resdata PRIVATE masterlib compat)
add_test(NAME masterlib_resdata COMMAND test_masterlib_resdata)

add_executable(test_masterlib_random tests/masterlib_random_test.cpp)
target_link_libraries(test_masterlib_random PRIVATE masterlib compat)
add_test(NAME masterlib_random COMMAND test_masterlib_random)

add_executable(test_masterlib_gfx tests/masterlib_gfx_test.cpp)
target_link_libraries(test_masterlib_gfx PRIVATE masterlib compat)
add_test(NAME masterlib_gfx COMMAND test_masterlib_gfx)

add_executable(test_masterlib_keyboard tests/masterlib_keyboard_test.cpp)
target_link_libraries(test_masterlib_keyboard PRIVATE masterlib shim compat)
add_test(NAME masterlib_keyboard COMMAND test_masterlib_keyboard)

# ---------------------------------------------------------------------------
# th01formats tests: synthetic-data encoders (pf_synth.hpp) build minimal
# .PAR/.GRP/.GRC/.PTN/.BOS/.GRZ samples in memory, write them to temp files,
# and load them through the real th01/formats/*.cpp loaders, asserting on
# in-memory state and/or VRAM pixel content (via pc98emu).
# ---------------------------------------------------------------------------
add_executable(test_formats_pf tests/formats_pf_test.cpp)
target_link_libraries(test_formats_pf PRIVATE th01formats masterlib compat)
target_compile_definitions(test_formats_pf PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME formats_pf COMMAND test_formats_pf)

add_executable(test_formats_grp tests/formats_grp_test.cpp)
target_link_libraries(test_formats_grp PRIVATE th01formats masterlib compat pc98emu)
target_compile_definitions(test_formats_grp PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME formats_grp COMMAND test_formats_grp)

add_executable(test_formats_grc tests/formats_grc_test.cpp)
target_link_libraries(test_formats_grc PRIVATE th01formats masterlib compat pc98emu)
target_compile_definitions(test_formats_grc PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME formats_grc COMMAND test_formats_grc)

add_executable(test_formats_ptn tests/formats_ptn_test.cpp)
target_link_libraries(test_formats_ptn PRIVATE th01formats masterlib compat pc98emu)
target_compile_definitions(test_formats_ptn PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
	GAME=1
)
add_test(NAME formats_ptn COMMAND test_formats_ptn)

add_executable(test_formats_bos tests/formats_bos_test.cpp)
target_link_libraries(test_formats_bos PRIVATE th01formats masterlib compat)
target_compile_definitions(test_formats_bos PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME formats_bos COMMAND test_formats_bos)

add_executable(test_formats_grz tests/formats_grz_test.cpp)
target_link_libraries(test_formats_grz PRIVATE th01formats masterlib compat pc98emu)
target_compile_definitions(test_formats_grz PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME formats_grz COMMAND test_formats_grz)

add_executable(test_formats_scoredat tests/formats_scoredat_test.cpp)
target_link_libraries(test_formats_scoredat PRIVATE th01formats masterlib compat)
target_compile_definitions(test_formats_scoredat PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME formats_scoredat COMMAND test_formats_scoredat)

add_executable(test_formats_cfg tests/formats_cfg_test.cpp)
target_link_libraries(test_formats_cfg PRIVATE compat)
target_compile_definitions(test_formats_cfg PRIVATE GAME=1)
add_test(NAME formats_cfg COMMAND test_formats_cfg)

# ---------------------------------------------------------------------------
# th01op (T9 op-bringup) tests: the new compat/app shims this component adds
# -- int86()/outportb()/inportb() dispatch, the Shift-JIS byte-helper
# reimplementation, and the assets-directory resolution M1's "fail with a
# clear message, not a hang" acceptance criterion depends on.
# ---------------------------------------------------------------------------
add_executable(test_x86real_shim tests/x86real_shim_test.cpp)
target_link_libraries(test_x86real_shim PRIVATE th01op_core pc98emu shim compat SDL3::SDL3)
add_test(NAME x86real_shim COMMAND test_x86real_shim)

add_executable(test_mbstring_shim tests/mbstring_shim_test.cpp)
target_link_libraries(test_mbstring_shim PRIVATE compat)
add_test(NAME mbstring_shim COMMAND test_mbstring_shim)

add_executable(test_assets_dir tests/assets_dir_test.cpp)
target_link_libraries(test_assets_dir PRIVATE appassetsdir)
target_compile_definitions(test_assets_dir PRIVATE
	PORT_TESTS_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME assets_dir COMMAND test_assets_dir)
