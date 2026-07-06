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
)
target_include_directories(pc98emu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

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
# tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(th01port_compat_test
	tests/compat_vram_ref_test.cpp
)
target_link_libraries(th01port_compat_test PRIVATE compat)
add_test(NAME compat_vram_ref COMMAND th01port_compat_test)
