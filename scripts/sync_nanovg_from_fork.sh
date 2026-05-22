#!/usr/bin/env bash
# external/nanovg is a git SUBTREE of the affineui_nanovg fork (branch
# `master`): NanoVG core + our sokol_gfx backend (nanovg_sokol.h) + patches.
# It is the canonical, in-tree, editable source — edit it here and it ships.
#
#   Pull the fork's latest into the subtree:   scripts/sync_nanovg_from_fork.sh
#   Push local nanovg changes back to the fork:
#       git subtree push --prefix=external/nanovg <fork-path> master
#
# Pass an explicit fork path or set AFFINEUI_NANOVG_FORK if it isn't a sibling.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORK="${1:-${AFFINEUI_NANOVG_FORK:-${ROOT}/../affineui_nanovg}}"
cd "$ROOT"
git subtree pull --prefix=external/nanovg "$FORK" master --squash
echo "Pulled affineui_nanovg (branch master) into external/nanovg."
