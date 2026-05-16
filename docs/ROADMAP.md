# Roadmap

Rough sequencing. Items inside a phase are not strictly ordered.

## Phase 0 — Scaffolding ✅

- [x] Project layout, license, build system
- [x] Documentation (architecture, design, building, contributing)
- [x] Dependency fetch script
- [x] CI skeleton
- [x] Stub public API that compiles

## Phase 1 — Minimum viable render

Goal: render `<h1>Hello</h1>` against a colored body on Metal + GL + D3D11.

- [ ] sokol_app + sokol_gfx + NanoVG painter wired up, all symbols
      file-local in one TU (encapsulation contract)
- [ ] fontstash + stb_truetype font pipeline
- [ ] lexbor integration: HTML5 parse → lexbor DOM walkable from our
      bridge layer
- [ ] lexbor CSS parse → cascade-ready rule trees
- [ ] Minimal cascade resolver (declarations → ComputedStyle for one
      element). Properties: color, background-color, font-family,
      font-size, font-weight, padding, margin
- [ ] Minimal block layout (one block, no inline, no flex)
- [ ] Paint driver emits fill_rect + draw_text against `Painter`
- [ ] `examples/00_hello` rendering visually correct, pixel-tested
- [ ] CI builds the example on Linux + macOS + Windows

## Phase 2 — Interactive

- [ ] Mouse routing → `on_mouse_move` / `on_lbutton_down|up`
- [ ] Keyboard routing → `on_key_press` (for focused inputs)
- [ ] `:hover` / `:active` working
- [ ] Scroll (wheel + drag) on overflow containers
- [ ] Cursor changes (pointer over `<a>`, text over editable, etc.)
- [ ] `examples/01_bootstrap` — Bootstrap CSS demo with working nav

## Phase 3 — Imm-mode layer

- [ ] Virtual DOM data structures
- [ ] Call-site-path identity (compiler intrinsics + key override)
- [ ] Reconciler (diff + patch ops against `litehtml::document`)
- [ ] `use_state<T>` with auto-cleanup on unmount
- [ ] `on_click` / `on_change` / `on_input` handlers
- [ ] `imm::invalidate()` + dirty tracking so view fn doesn't run per
      frame
- [ ] `examples/04_imm_counter` — minimal hooks demo
- [ ] `examples/05_imm_todo` — list rendering with keys

## Phase 4 — Polish & performance

- [ ] `box-shadow` (drop + inset)
- [ ] `border-radius` per-corner correctness
- [ ] CSS transitions reaching the painter (animations driven by
      retained DOM, no imm re-render)
- [ ] Background image: `cover`, `contain`, `repeat-x/y`, `no-repeat`
- [ ] Form widgets: `<input type=text|button|checkbox|radio>`
- [ ] DPI-aware text rendering (HiDPI on Retina / Windows scaling)
- [ ] Image cache LRU with size cap
- [ ] Layout-cache invalidation (don't relayout the whole document for
      a single attribute change)

## Phase 5 — Reach

- [ ] Material Design 3 sample (`examples/02_material`)
- [ ] CSS hot-reload tool (`tools/ui-preview` watches files)
- [ ] WebGPU backend wired through sokol_gfx
- [ ] C ABI completed and documented
- [ ] Rust bindings example (off-tree)
- [ ] Snapshot test corpus (golden images for the painter)
- [ ] HarfBuzz integration hook for complex shaping

## Phase 6 — Maybe

Things that *could* land if there's demand. None are committed:

- [ ] CSS Grid (filling litehtml gaps, possibly upstreaming)
- [ ] SVG inline rendering (NanoVG handles paths; just need parsing)
- [ ] Accessibility tree → platform AX bridges
- [ ] Embedded JS via QuickJS (one possible direction; controversial)

## Explicit non-goals

See [ARCHITECTURE.md § Non-goals](ARCHITECTURE.md#non-goals-explicit).
