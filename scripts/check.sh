#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
preset="${1:-debug}"
case "$preset" in
    debug|asan|release) ;;
    *) echo "Usage: scripts/check.sh [debug|asan|release]" >&2; exit 2 ;;
esac
python3 scripts/generate_conformance.py --check
python3 examples/crystal_courier/generate.py --check
python3 examples/crystal_courier_3d/generate.py --check
mapfile -t sources < <(find include src apps tests -type f \( -name '*.cpp' -o -name '*.hpp' \) | sort)
clang-format --dry-run --Werror "${sources[@]}"
cmake --preset "$preset"
cmake --build --preset "$preset" --parallel "${VSIM_BUILD_JOBS:-4}"
ctest --preset "$preset"
