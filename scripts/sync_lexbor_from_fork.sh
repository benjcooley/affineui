#!/usr/bin/env bash
# external/lexbor is a git SUBTREE of the affineui_lexbor fork (branch
# `affineui`). It is the canonical, in-tree, editable lexbor source — extend
# CSS here (declarations + parser + serializer, or .ton + regenerate via
# utils/), build, and it ships in the repo.
#
#   Pull the fork's latest into the subtree:   scripts/sync_lexbor_from_fork.sh
#   Push local lexbor changes back to the fork:
#       git subtree push --prefix=external/lexbor <fork-path> affineui
#
# Pass an explicit fork path or set AFFINEUI_LEXBOR_FORK if it isn't a sibling.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORK="${1:-${AFFINEUI_LEXBOR_FORK:-${ROOT}/../affineui_lexbor}}"
cd "$ROOT"
git subtree pull --prefix=external/lexbor "$FORK" affineui --squash
echo "Pulled affineui_lexbor (branch affineui) into external/lexbor."
