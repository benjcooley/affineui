# external/

Pinned snapshots of upstream dependencies. **Nothing here is checked in
to git** — populate by running `./scripts/fetch_deps.sh` from the repo
root. The build configures cleanly without these (stub mode); it links
real rendering only when they're present.

## Inventory

| Dir | Upstream | License | Pinned at | Used for |
|---|---|---|---|---|
| `sokol/` | https://github.com/floooh/sokol | zlib | `master @ <fetch date>` | GPU + windowing (single-header) |
| `nanovg/` | https://github.com/memononen/nanovg | zlib | `master @ <fetch date>` | Vector painter |
| `lexbor/` | https://github.com/lexbor/lexbor | Apache-2.0 | `v2.4.0` | HTML5 parser, CSS parser, DOM, selector matching |
| `yoga/` | https://github.com/facebook/yoga | MIT | `v3.2.1` | Flexbox math |
| `stb/` | https://github.com/nothings/stb | MIT / public domain | `master @ <fetch date>` | `stb_truetype.h`, `stb_image.h` |
| `fontstash/` | https://github.com/memononen/fontstash | zlib | `master @ <fetch date>` | Glyph atlas |

## Rules of engagement

1. **Don't edit vendored sources directly.** Patches go upstream first.
   When a patch can't go upstream, put it in `patches/` and apply it
   from `scripts/fetch_deps.sh`.
2. **Pin versions explicitly.** Whatever `scripts/fetch_deps.sh` checks
   out is what we test against. Updating a pin = a PR with a CI run.
3. **License attribution.** Each library's own `LICENSE` file stays
   alongside its sources. The single-header release built by
   `tools/amalgamate.py` re-emits all license texts in its banner.

## How it integrates

- `sokol/`, `nanovg/`, `stb/`, `fontstash/` are header-only or
  near-header-only — `cmake/Dependencies.cmake` adds them as system
  include paths; a few `.c` TUs under `src/paint/` and `src/text/`
  hold their `*_IMPLEMENTATION` definitions.
- `lexbor/` is a real CMake project — we `add_subdirectory(EXCLUDE_FROM_ALL)`
  it and link `lexbor_static`.
- `yoga/` is a real CMake project — same treatment, links `yogacore`.

## Optional (not fetched by default)

| Dir | Why | When to enable |
|---|---|---|
| `harfbuzz/` | Complex-script shaping | `-DAFFINEUI_ENABLE_HARFBUZZ=ON` |
| `libunibreak/` | UAX #14 line breaking | `-DAFFINEUI_ENABLE_UNIBREAK=ON` |
