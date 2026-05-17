#!/usr/bin/env bash
# Sync the sibling AffineUI Lexbor fork into the checked-in vendor copy.
#
# Default layout:
#   workspace/
#     affineui/   <-- this repo
#     lexbor/     <-- maintained fork / patch stack
#
# Usage:
#   scripts/sync_lexbor_from_fork.sh
#   scripts/sync_lexbor_from_fork.sh /path/to/lexbor-fork

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SRC="${1:-${AFFINEUI_LEXBOR_FORK:-${REPO_ROOT}/../lexbor}}"
DST="${REPO_ROOT}/external/lexbor"

if [[ ! -f "${SRC}/CMakeLists.txt" ||
      ! -f "${SRC}/source/lexbor/html/html.h" ||
      ! -f "${SRC}/source/lexbor/css/css.h" ]]; then
    echo "error: '${SRC}' does not look like a Lexbor checkout" >&2
    echo "usage: $0 [/path/to/lexbor-fork]" >&2
    exit 2
fi

mkdir -p "${REPO_ROOT}/external"

rsync -a --delete \
    --delete-excluded \
    --exclude='.git/' \
    --exclude='.github/' \
    --exclude='.travis.yml' \
    --exclude='INSTALL.md' \
    --exclude='pvs_studio.sh' \
    --exclude='build/' \
    --exclude='build-*/' \
    --exclude='cmake-build-*/' \
    --exclude='_build/' \
    --exclude='examples/' \
    --exclude='packaging/' \
    --exclude='test/' \
    --exclude='utils/' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='*.dylib' \
    --exclude='*.so' \
    --exclude='*.dll' \
    "${SRC}/" "${DST}/"

echo "Synced Lexbor fork:"
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
