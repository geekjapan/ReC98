cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

project(th01port CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH /opt/homebrew)
find_package(SDL3 CONFIG REQUIRED)
find_package(Threads REQUIRED)

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
# compat/host_headers/: stand-ins for a couple of Turbo C++ standard headers
# (currently just <malloc.h>) that have a direct <stdlib.h>/etc. equivalent
# on a normal host but no file of that name at all outside DOS/Windows.
# BEFORE + listed first so these shadow the (nonexistent) system ones.
target_include_directories(compat BEFORE INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/compat/host_headers
)
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
	shim/masterlib/math.cpp
	shim/masterlib/text.cpp
)
target_link_libraries(masterlib PRIVATE compat shim portsjis)

# ---------------------------------------------------------------------------
# portsjis: SJIS(cp932) -> UTF-8 filename translation (see
# compat/sjis_filename.hpp's header comment) -- APFS/host filesystems expect
# UTF-8 names, but original TH01 code opens files by raw Shift-JIS byte
# strings (e.g. th01/shiftjis/fns.hpp's ARC_FN). Shared by masterlib's
# file_* open paths and port/app's assets-directory canary checks.
# ---------------------------------------------------------------------------
add_library(portsjis STATIC
	compat/sjis_filename.cpp
)
target_include_directories(portsjis PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
find_library(ICONV_LIBRARY NAMES iconv libiconv)
if(ICONV_LIBRARY)
	target_link_libraries(portsjis PUBLIC ${ICONV_LIBRARY})
endif()

# ---------------------------------------------------------------------------
# th01formats: th01/formats/*.cpp (grp/grc/grz/pf/ptn/bos/scoredat -- cfg has
# no .cpp, just struct declarations) compiled as-is under REC98_PORT, plus
# port/shim/piload.cpp (a real C++ port of libs/piloadc's 16-bit-assembly
# .PI/.GRP decoder, see that file's header comment).
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
	shim/piload.cpp
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
target_link_libraries(appassetsdir PRIVATE portsjis)

add_executable(th01op
	app/main_op.cpp
)
target_link_libraries(th01op PRIVATE th01op_core appassetsdir apphost compat)

# ---------------------------------------------------------------------------
# bmp2arr_host: a *native host* build of Pipeline/bmp2arr[l].c, the same
# BMP-to-planar-array converter Tupfile.lua's BMP()/Sprites() helpers run
# under DOS to turn th01/sprites/*.bmp into the sSPARK/sPELLET/etc. C++
# arrays th01/main/**'s gameplay code links against. The tool's source is
# already host-portable (falls back to <stdint.h> when neither
# REC98_PROJECT nor a DOS/Windows compiler is defined) except for two
# headers no macOS/Linux libc ships: <malloc.h> (superseded by <stdlib.h>)
# and <endian.h> (no macOS equivalent) -- tools/bmp2arr_hostshim/ provides
# both, ahead of the real system include path, so the upstream .c files run
# completely unmodified. This regenerates real, correct sprite data (not
# placeholder zeros) without needing a DOS toolchain or Tup.
# ---------------------------------------------------------------------------
add_executable(bmp2arr_host
	${CMAKE_CURRENT_SOURCE_DIR}/../Pipeline/bmp2arr.c
	${CMAKE_CURRENT_SOURCE_DIR}/../Pipeline/bmp2arrl.c
)
target_include_directories(bmp2arr_host BEFORE PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/tools/bmp2arr_hostshim
)
target_include_directories(bmp2arr_host PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../Pipeline
)

# ---------------------------------------------------------------------------
# th01_sprite_gen: sSPARK/sLEAF_LEFT/sLEAF_RIGHT/sINTERLEAVE_MASKS/
# sSHOOTOUT_LASER/sPELLET/sPELLET_CLOUD/sPILLAR/sSHAPE8X8/sSTAGEBONUS_BOX --
# every sprite array th01/main/**'s REIIDEN.EXE gameplay closure needs,
# generated at build time from th01/sprites/*.bmp by bmp2arr_host, exactly
# matching Tupfile.lua's Sprites() call (same -sym/-sw/-sh/-pshf arguments,
# see that table around line 380).
#
# Unlike the font asset or a normal generated TU, these aren't linked in:
# every consumer (th01/main/bullet/pellet_c.cpp, .../boss/b20m.cpp, etc.)
# does `#include "th01/sprites/<name>.csp"` directly inline, exactly like
# Tup's BMP() rule writes its .csp output right next to the source .bmp/.hpp
# (`bmp[1]:gsub("%..+$", ".csp")`) for the including TU to pick up. So
# bmp2arr_host's outputs go into a generated include root that mirrors that
# same "th01/sprites/" relative layout, prepended to th01main_core's include
# path -- no separate compiled object, no risk of the inline-included arrays
# colliding with a second, separately-linked definition.
# ---------------------------------------------------------------------------
set(TH01SPRITES_GEN_ROOT ${CMAKE_CURRENT_BINARY_DIR}/generated/th01_sprites_inc)
set(TH01SPRITES_GEN_DIR ${TH01SPRITES_GEN_ROOT}/th01/sprites)
file(MAKE_DIRECTORY ${TH01SPRITES_GEN_DIR})

function(th01_sprite_csp OUT_VAR BMP_RELPATH SYM W H)
	get_filename_component(bmp_base ${BMP_RELPATH} NAME_WE)
	set(bmp_abspath ${CMAKE_CURRENT_SOURCE_DIR}/../${BMP_RELPATH})
	set(out_csp ${TH01SPRITES_GEN_DIR}/${bmp_base}.csp)
	add_custom_command(
		OUTPUT ${out_csp}
		COMMAND bmp2arr_host -q -i ${bmp_abspath} -o ${out_csp}
			-sym ${SYM} -of cpp -sw ${W} -sh ${H} ${ARGN}
		DEPENDS bmp2arr_host ${bmp_abspath}
		COMMENT "bmp2arr: ${BMP_RELPATH} -> ${SYM}"
		VERBATIM
	)
	set(${OUT_VAR} ${out_csp} PARENT_SCOPE)
endfunction()

th01_sprite_csp(SPR_LEAF_S    th01/sprites/leaf_s.bmp    sSPARK             8  8)
th01_sprite_csp(SPR_LEAF_L    th01/sprites/leaf_l.bmp    sLEAF_LEFT         8  8)
th01_sprite_csp(SPR_LEAF_R    th01/sprites/leaf_r.bmp    sLEAF_RIGHT        8  8)
th01_sprite_csp(SPR_ILEAVE_M  th01/sprites/ileave_m.bmp  sINTERLEAVE_MASKS  8  8)
th01_sprite_csp(SPR_LASER_S   th01/sprites/laser_s.bmp   sSHOOTOUT_LASER    16 8)
th01_sprite_csp(SPR_PELLET    th01/sprites/pellet.bmp    sPELLET            8  8 -pshf inner)
th01_sprite_csp(SPR_PELLET_C  th01/sprites/pellet_c.bmp  sPELLET_CLOUD      16 16)
th01_sprite_csp(SPR_PILLAR    th01/sprites/pillar.bmp    sPILLAR            32 8)
th01_sprite_csp(SPR_SHAPE8X8  th01/sprites/shape8x8.bmp  sSHAPE8X8          8  8)
th01_sprite_csp(SPR_BONUSBOX  th01/sprites/bonusbox.bmp  sSTAGEBONUS_BOX    8  4)

add_custom_target(th01_sprite_gen DEPENDS
	${SPR_LEAF_S} ${SPR_LEAF_L} ${SPR_LEAF_R} ${SPR_ILEAVE_M} ${SPR_LASER_S}
	${SPR_PELLET} ${SPR_PELLET_C} ${SPR_PILLAR} ${SPR_SHAPE8X8} ${SPR_BONUSBOX}
)

# ---------------------------------------------------------------------------
# th01main_core: TH01 REIIDEN.EXE's (stages/player/bullets/bosses) dependency
# closure -- th01/main_01.cpp (main_main()) plus every main_NN.cpp segment
# stub and the handful of un-numbered root th01/*.cpp stubs that pull in
# th01/main/**/*.cpp, plus its own copies of the shared hardware TUs
# (see th01op_core above for why each binary compiles its own copies rather
# than sharing one lib -- OP.EXE and REIIDEN.EXE are separate binaries on
# real hardware too).
# ---------------------------------------------------------------------------
add_library(th01main_core STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_01.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_09.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_14.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_15.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_17.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_18.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_20.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_21.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_23.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_24.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_25.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_26.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/player.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/stages.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_28.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_29.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_30.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_31.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_32.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/particle.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_33.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_34.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_35.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_36.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_37.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/main_38.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/bullet_l.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/grpinv32.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/scrollup.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/pgtrans.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/grph1to0.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/core/initexit.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/core/resident.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/graph.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grppsafx.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grp_text.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/egc.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/frmdelay.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/vsync.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/ztext.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/input.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/tram_x16.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grp2xscs.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hiscore/regist.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../platform/x86real/pc98/font.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../platform/x86real/pc98/grcg.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../platform/x86real/pc98/grp_clip.cpp
	shim/main_globals.cpp
	shim/blitter_stub.cpp
	shim/egc_copy_shim.cpp
	compat/x86real_shim.cpp
)
target_compile_definitions(th01main_core PRIVATE GAME=1)
target_include_directories(th01main_core BEFORE PRIVATE ${TH01SPRITES_GEN_ROOT})
add_dependencies(th01main_core th01_sprite_gen)
target_compile_options(th01main_core PRIVATE
	-Wno-register
	-funsigned-char
	# Decompiled data tables (e.g. th01/main/stage/stageobj.cpp's raw
	# "glitch stage" memory dump) are plain `int8_t foo[] = { 184, ... }`
	# braced initializers with values >127 -- fine as a truncating bit
	# pattern under Borland, an ISO C++ list-initialization narrowing error
	# here. The values are intentional (byte-for-byte memory dumps), so
	# silence rather than rewrite them.
	-Wno-c++11-narrowing
	-include ${CMAKE_CURRENT_SOURCE_DIR}/compat/borland.hpp
)
target_link_libraries(th01main_core PRIVATE
	compat masterlib th01formats sndstub apphost
)

# ---------------------------------------------------------------------------
# th01main: the REIIDEN.EXE port executable. port/app/main_reiiden.cpp
# resolves an assets directory, fails with a clear message if TH01's data
# files aren't there (see that file's header comment), then calls
# th01/main_01.cpp's main_main() directly.
# ---------------------------------------------------------------------------
add_executable(th01main
	app/main_reiiden.cpp
)
target_compile_definitions(th01main PRIVATE GAME=1)
target_link_libraries(th01main PRIVATE th01main_core appassetsdir apphost compat)

# ---------------------------------------------------------------------------
# th01fuuin_core: TH01 FUUIN.EXE's (ending slideshow, verdict screen, and
# high-score registration) dependency closure -- th01/fuuin_01.cpp and
# th01/fuuin_03.cpp (in turn th01/end/end.cpp, see that file's forwarding
# #include) plus every original ReC98 TU they actually call into. Much of
# this list mirrors th01op_core/th01main_core (each binary compiles its own
# copies of the shared hardware TUs, see th01op_core's header comment for
# why), with th01/hiscore/regist.cpp added for the high-score name-entry
# screen (verdict_animate_and_regist_menu() -> regist_menu()) and
# th01/hardware/input.cpp compiled in full (not just the two globals
# op_globals.cpp extracts) since regist.cpp's regist_name_enter() actually
# calls input_sense()/input_reset_sense() and reads input_up/down/lr/strike,
# unlike op_01.cpp which senses BIOS keygroups directly.
#
# game/cutscene.cpp defines CUTSCENE_QUARTERS (the four ending-picture quarter
# rects end_pic_show() blits from), a small self-contained cross-game TU with
# no REC98_PORT concerns of its own.
#
# port/shim/fuuin_globals.cpp is this component's own new shim
# (entrypoint_exec() stub; `rank`, a th01/resident.hpp global otherwise only
# defined in th01/main_01.cpp, same "each entry TU needs its own copy of the
# other two's globals" story as op_globals.cpp's frame_rand/end_flag; and
# link-time-only stubs for the th01/main/debug.hpp / th01/main/player/
# bomb.hpp names input.cpp's unreachable-here debug-menu branch references --
# see that file's header comment). port/shim/masterlib/gfx.cpp also gained
# graph_scrollup() (a documented no-op -- pc98emu has no raster-scroll model
# yet) for th01/end/end.cpp's shake_then_boom().
# ---------------------------------------------------------------------------
add_library(th01fuuin_core STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/../game/cutscene.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/fuuin_01.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/fuuin_03.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/core/initexit.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/core/resident.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/graph.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grppsafx.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grp_text.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/egc.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/frmdelay.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/vsync.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/ztext.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/input.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hardware/grp2xscs.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../th01/hiscore/regist.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../platform/x86real/pc98/font.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../platform/x86real/pc98/grcg.cpp
	shim/fuuin_globals.cpp
	compat/x86real_shim.cpp
)
target_compile_definitions(th01fuuin_core PRIVATE GAME=1)
target_compile_options(th01fuuin_core PRIVATE
	-Wno-register
	-funsigned-char
	-include ${CMAKE_CURRENT_SOURCE_DIR}/compat/borland.hpp
)
target_link_libraries(th01fuuin_core PRIVATE compat masterlib th01formats sndstub apphost)

# ---------------------------------------------------------------------------
# th01fuuin: the FUUIN.EXE port executable. port/app/main_fuuin.cpp resolves
# an assets directory, fails with a clear message if TH01's data files
# aren't there (see that file's header comment for why this happens before
# any original game code runs), then calls th01/fuuin_01.cpp's
# main_cutscene() directly.
# ---------------------------------------------------------------------------
add_executable(th01fuuin
	app/main_fuuin.cpp
)
target_compile_definitions(th01fuuin PRIVATE GAME=1)
target_link_libraries(th01fuuin PRIVATE th01fuuin_core appassetsdir apphost compat)

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
# engine driving it end-to-end. T20 added mdt_real_songs, an assets-gated
# smoke test that renders the whole real TH01 .MDT corpus (see
# mdt_format.hpp/mdrv2_engine.hpp for what's reverse-engineered vs.
# approximated), and tools/mdt_render.cpp, a CLI dev-tool for rendering one
# song to a .wav for a human to listen to.
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

add_executable(mdt_render tools/mdt_render.cpp)
target_link_libraries(mdt_render PRIVATE sndmdt)

add_executable(test_mdt_real_songs tests/mdt_real_songs_test.cpp)
target_link_libraries(test_mdt_real_songs PRIVATE sndmdt appassetsdir)
add_test(NAME mdt_real_songs COMMAND test_mdt_real_songs)
set_tests_properties(mdt_real_songs PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

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
# th01main (T11a REIIDEN.EXE bring-up) tests: the new shims this component
# adds -- master.lib's SinTable8/CosTable8/iatan2/isqrt/ihypot and raw TRAM
# text_clear()/text_fillca() (unreached by the OP.EXE closure), EGCCopy
# forwarding to pc98emu's already-tested EGC engine, and the deliberately-
# stubbed blitter (see shim/blitter_stub.cpp's header comment).
# ---------------------------------------------------------------------------
add_executable(test_masterlib_math tests/masterlib_math_test.cpp)
target_link_libraries(test_masterlib_math PRIVATE masterlib compat)
add_test(NAME masterlib_math COMMAND test_masterlib_math)

add_executable(test_masterlib_text tests/masterlib_text_test.cpp)
target_link_libraries(test_masterlib_text PRIVATE masterlib compat)
add_test(NAME masterlib_text COMMAND test_masterlib_text)

add_executable(test_main_egc_copy_shim tests/main_egc_copy_shim_test.cpp)
target_link_libraries(test_main_egc_copy_shim PRIVATE th01main_core compat pc98emu)
add_test(NAME main_egc_copy_shim COMMAND test_main_egc_copy_shim)

add_executable(test_blitter_stub tests/blitter_stub_test.cpp)
target_link_libraries(test_blitter_stub PRIVATE th01main_core compat)
add_test(NAME blitter_stub COMMAND test_blitter_stub)

# T15: TRAMCursor::putkanji() (tram_x16.cpp) and z_text_putsa() (ztext.cpp)
# REC98_PORT branches, routed to pc98emu/text.hpp's cell API instead of
# raw SEG_TRAM_JIS/ATRB MK_FP pokes (see port/compat/borland.hpp's
# resolve_far() comment).
add_executable(test_tram_text tests/tram_text_test.cpp)
target_link_libraries(test_tram_text PRIVATE th01main_core compat pc98emu)
add_test(NAME tram_text COMMAND test_tram_text)

# ---------------------------------------------------------------------------
# th01fuuin (T12a fuuin-bringup) tests: the new shim this component adds --
# port/shim/fuuin_globals.cpp's link-time-only globals/stubs (rank, mode_test,
# bomb_doubletap_frame, debug_mem()/debug_show_game()) and
# port/shim/masterlib/gfx.cpp's graph_scrollup() (tested alongside gfx.cpp's
# other entries in masterlib_gfx_test.cpp above). th01fuuin_core's own TUs
# (fuuin_01.cpp/fuuin_03.cpp/end.cpp/hiscore-regist.cpp) are exercised by
# actually running the th01fuuin executable (port/ci.sh), same as
# th01op/th01main's no-assets acceptance check.
# ---------------------------------------------------------------------------
add_executable(test_fuuin_globals tests/fuuin_globals_test.cpp)
target_link_libraries(test_fuuin_globals PRIVATE th01fuuin_core compat)
add_test(NAME fuuin_globals COMMAND test_fuuin_globals)

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

# M1 (SPEC.md) headless pixel verification: decodes the real REIIDEN2.GRP
# title-screen asset through PiBlitL and compares against tests/golden/
# title_reference.ppm. Skips (doesn't fail) when the real TH01 assets aren't
# available on this machine -- see that test's own header comment.
add_executable(test_title_screen_golden tests/title_screen_golden_test.cpp)
target_link_libraries(test_title_screen_golden PRIVATE
	th01op_core appassetsdir pc98emu shim compat SDL3::SDL3
)
target_compile_definitions(test_title_screen_golden PRIVATE
	PORT_TESTS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
)
add_test(NAME title_screen_golden COMMAND test_title_screen_golden)
set_tests_properties(title_screen_golden PROPERTIES SKIP_RETURN_CODE 77)

# M2 (SPEC.md) headless menu navigation: drives the real main_op() closure
# through HIT KEY -> main menu -> Config -> Music Test -> quit by injecting
# synthetic key states (port::input_test_set_keys()), verifying via
# th01/op_01.cpp's own menu_sel/menu_id/quit globals. Skips (doesn't fail)
# when the real TH01 assets aren't available, same as title_screen_golden.
add_executable(test_menu_nav tests/menu_nav_test.cpp)
target_link_libraries(test_menu_nav PRIVATE
	th01op_core appassetsdir apphost pc98emu shim compat SDL3::SDL3 Threads::Threads
)
add_test(NAME menu_nav COMMAND test_menu_nav)
set_tests_properties(menu_nav PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

# M3 (SPEC.md) headless stage-1 gameplay bringup: seeds a fresh resident
# structure (port/app/reiiden_bootstrap.hpp) and drives the real REIIDEN.EXE
# closure's main_main() through the stage 1 intro and into live gameplay by
# injecting synthetic key states, verifying via th01/main/player/player.hpp's
# player_left that movement input actually takes effect. Skips (doesn't
# fail) when the real TH01 assets aren't available, same as menu_nav.
add_executable(test_stage1_bringup tests/stage1_bringup_test.cpp)
target_compile_definitions(test_stage1_bringup PRIVATE GAME=1)
target_link_libraries(test_stage1_bringup PRIVATE
	th01main_core appassetsdir apphost pc98emu shim compat SDL3::SDL3 Threads::Threads
)
add_test(NAME stage1_bringup COMMAND test_stage1_bringup)
set_tests_properties(stage1_bringup PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
