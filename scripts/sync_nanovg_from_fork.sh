#!/usr/bin/env bash
# Sync the sibling AffineUI NanoVG fork into the checked-in vendor copy.
#
# The fork (affineui_nanovg) is the source of truth: NanoVG core + our
# sokol_gfx backend (nanovg_sokol.h) + patches. This copies its consumed
# source (src/) into external/nanovg so AffineUI builds self-contained.
#
# Default layout:
#   workspace/
#     affineui/           <-- this repo
#     affineui_nanovg/    <-- maintained fork (source of truth)
#
# Usage:
#   scripts/sync_nanovg_from_fork.sh
#   scripts/sync_nanovg_from_fork.sh /path/to/affineui_nanovg

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SRC="${1:-${AFFINEUI_NANOVG_FORK:-${REPO_ROOT}/../affineui_nanovg}}"
DST="${REPO_ROOT}/external/nanovg"

if [[ ! -f "${SRC}/src/nanovg.h" || ! -f "${SRC}/src/nanovg_sokol.h" ]]; then
    echo "error: '${SRC}' does not look like the affineui_nanovg fork" >&2
    echo "usage: $0 [/path/to/affineui_nanovg]" >&2
    exit 2
fi

mkdir -p "${DST}/src"

# Vendor the consumed source tree (src/). The fork's src/ holds exactly the
# files AffineUI compiles/includes (nanovg core, nanovg_sokol.h, the GL
# backend, fontstash, stb). --delete keeps the vendor copy clean.
rsync -a --delete --exclude='.git/' "${SRC}/src/" "${DST}/src/"

# Keep the license alongside the vendored source.
[[ -f "${SRC}/LICENSE.txt" ]] && cp -f "${SRC}/LICENSE.txt" "${DST}/LICENSE.txt"

echo "Synced NanoVG fork:"
echo "  from: ${SRC}"
echo "  to:   ${DST}"
if git -C "${SRC}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    branch="$(git -C "${SRC}" branch --show-current 2>/dev/null || true)"
    commit="$(git -C "${SRC}" rev-parse --short HEAD)"
    if [[ -n "${branch}" ]]; then
        echo "  ref:  ${branch} @ ${commit}"
    else
        echo "  ref:  ${commit}"
    fi
fi
