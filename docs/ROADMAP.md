# Roadmap

Rough sequencing. Items inside a phase are not strictly ordered. Items
marked ✅ are landed; ⏳ are in progress; otherwise open.

## Phase 0 — Scaffolding ✅

- [x] Project layout, license, build system
- [x] Documentation (architecture, design, building, contributing)
- [x] Dependency fetch script
- [x] CI skeleton — three-OS stub-mode on PRs, full-deps on push-to-main
- [x] Stub public API that compiles

## Phase 1 — Minimum viable render ✅

Goal: render `<h1>Hello</h1>` against a colored body on Metal + GL.

- [x] sokol_app + sokol_gfx + NanoVG painter wired, all symbols
      file-local in one TU (encapsulation contract honored)
- [x] fontstash + stb_truetype font pipeline
- [x] lexbor integration: HTML5 parse → DOM walkable through the bridge
- [x] lexbor CSS parse → cascade-ready rule trees
- [x] Cascade resolver covering color, background-color, font-family,
      font-size, font-weight, padding, margin, border, line-height
- [x] Block + flex + inline layout via the Yoga adapter (exceeds the
      original "one block" target)
- [x] Paint driver emits fill_rect + draw_text against `Painter`
- [x] `examples/00_hello` rendering visually correct on Metal + GL
- [x] CI builds + ctest on Linux + macOS + Windows
- [ ] Pixel-level snapshot test for `00_hello` (the file-tree slot
      `tests/snapshots/` is reserved; goldens not generated yet)

## Phase 1.5 — Library shaping ✅

Wasn't on the original roadmap but happened during Phase 1 once it
became obvious users wanted to drop AffineUI into their own window
loops, not the other way around.

- [x] Split `App` into `Renderer` (graphics-only) + `Ui` (facade) +
      window adapters — embedders own their event loop
- [x] First-class sokol adapter (`affineui::sokol::{wire,dispatch,render}`)
- [x] First-class SDL2 adapter (`affineui::sdl::{wire,dispatch,render}`)
- [x] Single umbrella include `<affineui/affineui.h>` — adapters
      autoload via `AFFINEUI_WITH_{SOKOL,SDL}`
- [x] `Ui::on_click("#id" | ".cls" | "tag" | "a,b")` selector handlers

## Phase 2 — Interactive ⏳

- [x] Mouse routing (move + button down/up) wired through `Ui::dispatch`
- [x] Cursor sync (pointer/text/crosshair) inside the event callback so
      Cocoa's `mouseMoved:` lands in the same run-loop turn
- [x] `:hover` pseudo-class via side-table overlay — single-compound
      selectors only (`button:hover`, `.btn:hover`, `#id:hover`).
      Ancestor chain tracking + per-element re-resolve on toggle.
- [ ] `:hover` widened to descendant / multi-class compounds via
      `lxb_selectors_find` with a state-aware match callback
- [ ] `:active` (mouse-down state) reuses the same overlay machinery
- [ ] Scroll (wheel + drag) on `overflow: auto|scroll` containers
- [ ] Keyboard routing for focused inputs (`on_key_press`, IME stub)
- [ ] Inline-style scan for properties lexbor 2.4 doesn't expose
      (`border-radius`, `gap`, `cursor`, `background:` shorthand)
- [ ] `examples/01_bootstrap` — Bootstrap-CSS-styled page with a
      working nav, validates the cascade against real-world CSS

## Phase 3 — Imm-mode layer ⏳

Phase 2D shipped: clear-and-rebuild reconciler driving lexbor DOM
mutation. Public surface (`mount`, `use_state`, `on_click`,
`invalidate`) is stable; the body is the next swap.

- [x] Call-site-path identity via `std::source_location` hash + key
      override surface
- [x] `use_state<T>` slot map keyed by call-site, persists across
      re-renders, dtor on document destruction
- [x] `on_click` handler routing through auto-stamped
      `aui-imm-{hash}` element ids
- [x] `imm::invalidate()` + dirty tracking — view fn doesn't run per
      frame
- [x] `examples/04_imm_counter`
- [x] Clear-and-rebuild reset path (nulls `doc->ev_remove` /
      `ev_destroy` to dodge lexbor's broken cascade-tear-down on
      cascade-attached subtrees; see *Known issues* below)
- [ ] Dumb-reconcile upgrade: set-until-mismatch traversal, only
      rebuild the subtree below a fatal diff. Same public surface,
      different body. Drops the destroy walk and the orphaned style
      entries it leaves behind, and lets state slot mapping stay tied
      to the same DOM nodes across renders.
- [ ] Virtual-DOM data structures (only needed if we promote past
      dumb-reconcile to keyed-list reconciliation)
- [ ] `on_change` / `on_input` handlers (today they're stubs)
- [ ] `examples/05_imm_todo` — keyed list rendering, exercises
      add/remove/reorder

## Phase 4 — Polish & performance

- [ ] `border-radius` per-corner correctness (currently uniform only)
- [ ] `box-shadow` (drop + inset)
- [ ] CSS transitions reaching the painter (driven by retained DOM,
      no imm re-render)
- [ ] Background image: `cover`, `contain`, `repeat-x/y`, `no-repeat`
- [ ] Form widgets: `<input type=text|button|checkbox|radio>`
- [ ] DPI-aware text rendering corrections (we plumb fb pixels vs
      points correctly; intrinsic text height still uses the parked
      hand-tuned constant — fix is to lean on real `nvgTextMetrics`)
- [ ] Image cache LRU with size cap
- [ ] Layout-cache invalidation per dirty subtree (don't relayout the
      whole document for one attribute change)

## Phase 5 — Reach

- [ ] Material Design 3 sample (`examples/02_material`)
- [ ] CSS hot-reload tool (`tools/ui-preview` watches files)
- [ ] WebGPU backend wired through sokol_gfx
- [ ] C ABI completed and documented (skeleton lives in
      `src/c_api.cpp`)
- [ ] Rust bindings example (off-tree)
- [ ] Snapshot test corpus (golden images for the painter)
- [ ] HarfBuzz integration hook for complex shaping

## Phase 6 — Maybe

Things that *could* land if there's demand. None are committed:

- [ ] CSS Grid (filling lexbor gaps, possibly upstreaming)
- [ ] SVG inline rendering (NanoVG handles paths; just need parsing)
- [ ] Accessibility tree → platform AX bridges
- [ ] Embedded JS via QuickJS (one possible direction; controversial)

## Known issues / parked

- **Lexbor destroy-after-cascade corruption.** Destroying cascade-
  attached elements via `lxb_dom_node_destroy_deep` corrupts
  `doc->css.styles` AVL pool — next insert hangs in
  `lexbor_avl_node_balance`. Even `lxb_html_element_inner_html_set`
  (lexbor's canonical mutation API) hangs the same way under our
  setup. Most lexbor users parse → walk → drop document, so this
  path is unexercised upstream. Our workaround in
  `src/imm/imm_runtime.cpp::clear_children` nulls `doc->ev_remove` +
  `doc->ev_destroy` for the destroy walk, leaving orphan cascade
  entries that get reclaimed with the document. Phase 3 dumb-
  reconcile sidesteps the issue entirely.
- **Lexbor `event_destroy` NULL deref.** Patched in
  `patches/lexbor-html-event-destroy-null-list.patch`; auto-applied
  by `scripts/fetch_deps.sh`. Real upstream bug, narrow path, no
  upstream issue filed yet.
- **Text vertical metric.** Intrinsic height in the Yoga adapter
  uses a hand-tuned constant. Plumbing real `nvgTextMetrics` is a
  trivial fix that's parked behind Phase 4 polish.

## Explicit non-goals

See [ARCHITECTURE.md § Non-goals](ARCHITECTURE.md#non-goals-explicit).
