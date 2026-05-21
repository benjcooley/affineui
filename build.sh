#!/usr/bin/env bash
#
# AffineUI dev task runner (Linux / macOS / WSL / git-bash).
# Windows users: use build.ps1 (sets up the MSVC environment).
#
#   ./build.sh <command> [args]
#
# Commands:
#   configure [debug|release]  CMake configure (Ninja) into ./build
#   build     [debug|release]  configure if needed, then build everything
#   test                       run the unit tests (ctest)
#   run       [example]        run an example exe (default: hello)
#   dist                       regenerate the two-file SDK -> dist/affineui.{h,cpp}
#   sync-nanovg                vendor the affineui_nanovg fork into external/nanovg
#   sync-lexbor                vendor the affineui_lexbor fork into external/lexbor
#   clean                      remove ./build
#   help                       show this help
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${ROOT}/build"

py() { command -v python3 || command -v python; }

build_type() {
    case "${1:-release}" in
        debug|Debug)            echo "Debug" ;;
        relwithdebinfo|RelWithDebInfo) echo "RelWithDebInfo" ;;
        *)                      echo "Release" ;;
    esac
}

do_configure() {
    cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$(build_type "${1:-}")"
}

usage() { sed -n '2,/^set -euo/p' "$ROOT/build.sh" | sed 's/^#\s\?//; /^set -euo/d'; }

cmd="${1:-help}"; shift || true
case "$cmd" in
    configure)   do_configure "${1:-}" ;;
    build)       [ -f "$BUILD/CMakeCache.txt" ] || do_configure "${1:-}"; cmake --build "$BUILD" --parallel ;;
    test)        ctest --test-dir "$BUILD" --output-on-failure ;;
    run)         ex="${1:-hello}"; "$BUILD/examples/${ex}/${ex}" ;;
    dist)        "$(py)" "$ROOT/tools/amalgamate.py" --root "$ROOT" --out "$ROOT/dist" ;;
    sync-nanovg) bash "$ROOT/scripts/sync_nanovg_from_fork.sh" ;;
    sync-lexbor) bash "$ROOT/scripts/sync_lexbor_from_fork.sh" ;;
    clean)       rm -rf "$BUILD" ;;
    help|*)      usage ;;
esac
