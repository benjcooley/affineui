# external/

Pinned snapshots of upstream dependencies. These directories are checked
in so a normal clone can configure and build without network access.
Refreshes are explicit maintenance steps, not part of CMake configure or
build.

## Inventory

| Dir | Upstream | License | Pinned at | Used for |
|---|---|---|---|---|
| `sokol/` | https://github.com/floooh/sokol | zlib | `master @ <fetch date>` | GPU + windowing (single-header) |
| `nanovg/` | https://github.com/memononen/nanovg | zlib | `master @ <fetch date>` | Vector painter |
| `lexbor/` | local AffineUI fork of https://github.com/lexbor/lexbor | Apache-2.0 | `affineui/v2.4.0` | HTML5 parser, CSS parser, DOM, selector matching |
| `yoga/` | https://github.com/facebook/yoga | MIT | `v3.2.1` | Flexbox math |
| `stb/` | https://github.com/nothings/stb | MIT / public domain | `master @ <fetch date>` | `stb_truetype.h`, `stb_image.h` |

## Rules of engagement

1. **Don't edit vendored sources directly.** For Lexbor, make the change
   in the sibling fork (`../lexbor`) on the `affineui/v2.4.0` branch,
   then run `scripts/sync_lexbor_from_fork.sh`.
2. **Pin versions explicitly.** Whatever is committed under `external/`
   is what we test against. Updating a pin = a PR with a CI run.
3. **License attribution.** Each library's own `LICENSE` file stays
   alongside its sources. Distribution artifacts must keep the license
   banner and any required notices.

## How it integrates

- `sokol/`, `nanovg/`, and `stb/` are header-only or near-header-only.
  NanoVG carries the fontstash/stb copies it needs under `nanovg/src/`.
  `cmake/Dependencies.cmake` adds the curated include paths; a few `.c`
  TUs under `src/paint/` and `src/text/` hold their implementation
  definitions.
- `lexbor/` is a real CMake project — we `add_subdirectory(EXCLUDE_FROM_ALL)`
  it and link `lexbor_static`.
- `yoga/` is a real CMake project — same treatment, links `yogacore`.

## Refreshing Lexbor

The maintained Lexbor patch stack lives next to this repo:

```bash
workspace/
  affineui/
  lexbor/      # branch: affineui/v2.4.0
```

After committing changes in `../lexbor`, copy the fork into the vendored
tree:

```bash
./scripts/sync_lexbor_from_fork.sh
```

The script accepts an explicit fork path if the checkout lives elsewhere.

## Optional (not vendored by default)

| Dir | Why | When to enable |
|---|---|---|
| `harfbuzz/` | Complex-script shaping | `-DAFFINEUI_ENABLE_HARFBUZZ=ON` |
| `libunibreak/` | UAX #14 line breaking | `-DAFFINEUI_ENABLE_UNIBREAK=ON` |
