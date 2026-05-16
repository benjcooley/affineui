#!/usr/bin/env bash
# Fetch pinned vendored dependencies into external/.
#
# Idempotent: re-running re-fetches at the pinned ref. Pass --force to
# blow away existing checkouts. No network access happens at CMake
# configure or build time after this script has run successfully.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
EXT="${REPO_ROOT}/external"

force=false
for arg in "$@"; do
    case "$arg" in
        --force|-f) force=true ;;
        --help|-h)
            echo "Usage: $0 [--force]"
            echo "  Pulls pinned upstreams into external/. --force re-fetches."
            exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

mkdir -p "${EXT}"

# name url ref(commit or tag) -- populate one dir per dep
DEPS=(
    "sokol     https://github.com/floooh/sokol.git              master"
    "nanovg    https://github.com/memononen/nanovg.git          master"
    "lexbor    https://github.com/lexbor/lexbor.git             v2.4.0"
    "yoga      https://github.com/facebook/yoga.git             v3.2.1"
    "stb       https://github.com/nothings/stb.git              master"
    "fontstash https://github.com/memononen/fontstash.git       master"
)

fetch_one() {
    local name url ref
    name=$1; url=$2; ref=$3
    local dest="${EXT}/${name}"

    if [[ -d "${dest}/.git" ]]; then
        if [[ "$force" == "true" ]]; then
            echo ">> [${name}] --force: removing ${dest}"
            rm -rf "${dest}"
        else
            echo ">> [${name}] already present, fetching latest at pin"
            git -C "${dest}" fetch --tags --depth=1 origin "${ref}" 2>/dev/null || true
            git -C "${dest}" checkout --quiet "${ref}" 2>/dev/null \
                || git -C "${dest}" checkout --quiet "FETCH_HEAD"
            return
        fi
    fi

    echo ">> [${name}] cloning ${url} @ ${ref}"
    git clone --quiet --depth=1 --branch "${ref}" "${url}" "${dest}" \
        2>/dev/null \
    || {
        # branch flag fails for raw commit refs — fall back to full clone + checkout
        git clone --quiet "${url}" "${dest}"
        git -C "${dest}" checkout --quiet "${ref}"
    }
    # Trim git history we don't need.
    rm -rf "${dest}/.git"
}

for line in "${DEPS[@]}"; do
    # shellcheck disable=SC2086
    set -- $line
    fetch_one "$1" "$2" "$3"
done

# Some libraries ship their headers under src/ — flatten so paths stay
# stable regardless of upstream reshuffles.
if [[ -d "${EXT}/nanovg/src" ]]; then
    # NanoVG's headers live under src/; expose them at the top level too.
    for h in "${EXT}"/nanovg/src/*.h; do
        ln -sf "${h}" "${EXT}/nanovg/$(basename "${h}")" 2>/dev/null || cp -n "${h}" "${EXT}/nanovg/"
    done
fi
if [[ -d "${EXT}/fontstash/src" ]]; then
    for h in "${EXT}"/fontstash/src/*.h; do
        ln -sf "${h}" "${EXT}/fontstash/$(basename "${h}")" 2>/dev/null || cp -n "${h}" "${EXT}/fontstash/"
    done
fi

# Apply pinned upstream patches. Files in patches/*.patch contain a
# top-of-file header (LIB / TAG / APPLY / TARGET) plus a unified diff
# starting at `---`. We feed only the diff portion through
# `patch -p1` from the matching library root.
PATCH_DIR="${REPO_ROOT}/patches"
if [[ -d "${PATCH_DIR}" ]]; then
    shopt -s nullglob
    for p in "${PATCH_DIR}"/*.patch; do
        # Filename convention: <lib>-<short-issue>.patch
        lib="$(basename "${p}" .patch)"
        lib="${lib%%-*}"
        target_dir="${EXT}/${lib}"
        if [[ ! -d "${target_dir}" ]]; then
            echo ">> [patch] skipping ${p##*/} (no ${lib}/ in external/)"
            continue
        fi
        echo ">> [patch] applying ${p##*/} → ${lib}/"
        # Locate the diff section (first `---` line) and pipe from there.
        diff_start="$(grep -n '^---' "${p}" | head -1 | cut -d: -f1)"
        if [[ -z "${diff_start}" ]]; then
            echo "   skip — no diff section found"
            continue
        fi
        tail -n +"${diff_start}" "${p}" \
            | (cd "${target_dir}" && patch -p1 --forward --silent) \
            || echo "   patch did not apply cleanly (already applied?) — continuing"
    done
fi

echo
echo "All dependencies fetched into external/."
echo "Re-run CMake configure: cmake -S . -B build -G Ninja"
