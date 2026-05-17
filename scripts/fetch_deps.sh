#!/usr/bin/env bash
# Historical compatibility shim.
#
# AffineUI now checks in curated source snapshots under external/. Do
# not clone full upstream trees into external/; that reintroduces tests,
# CI, examples, packaging, and language bindings that are intentionally
# left out of the two-file distribution source inventory.

set -euo pipefail

cat >&2 <<'EOF'
external/ is checked in and curated.

To update Lexbor:
  1. Make normal commits on the sibling ../lexbor branch affineui/v2.4.0.
  2. Run scripts/sync_lexbor_from_fork.sh from the AffineUI repo.

Other vendored updates should be curated manually: copy only the source,
headers, license, and provenance files needed to produce affineui.h and
affineui.cpp.
EOF

exit 1
