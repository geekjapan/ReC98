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
