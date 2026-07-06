#!/usr/bin/env bash
# port/ci.sh -- local CI gate: configure + build + test port/.
#
# GitHub-Actions-free merge gate. Exits nonzero if configure, build, or any
# ctest fails. Run this before merging a worker branch into sdl-port (see
# /Users/geekjapan/dev/projects/touhou/WORKFLOW.md's merge gate rule).
#
# Usage: port/ci.sh [--clean]
#   --clean   remove port/build/ before configuring (forces a full rebuild)

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${script_dir}/build"

if [[ "${1:-}" == "--clean" ]]; then
	rm -rf "${build_dir}"
fi

cmake -B "${build_dir}" -S "${script_dir}"
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure
