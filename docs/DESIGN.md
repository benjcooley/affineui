# Design notes

Rationale behind specific choices. Read [ARCHITECTURE.md](ARCHITECTURE.md)
first for the big picture.

## Why we don't use litehtml or RmlUi

Both are credible candidates for "small embeddable HTML/CSS engine."
Both were considered and ruled out. The reasons are different and
worth recording.

**litehtml.** Renderer-agnostic, BSD-3, ~30k LOC, used in production
(Sumatra PDF, FreeOrion). Looks right on paper. The problems show up
in practice:

- A documented list of HTML tags it has *never* supported and *will
  not* support. After more than a decade, the gap is structural, not
  pending.
- `document_container` is a leaky abstraction. The ~30 callbacks have
  underspecified contracts; you discover the rules by shipping and
  hitting bugs.
- No incremental layout. Any DOM mutation re-lays-out the affected
  subtree wholesale; selectors re-match against the whole document.
- CSS3 gaps in awkward places — custom properties, `calc()`, modern
  pseudo-classes — all partial in undocumented ways.
- Memory layout is pointer-soup. `shared_ptr` on every box, heavy
  cache misses on hot loops.

**RmlUi** (formerly libRocket). MIT, game-focused, well-maintained.
But: it's *RML*, not *HTML*. The whole motivation for "real HTML5 + CSS"
is that you can paste in Bootstrap, Material, designer-authored
markup. RmlUi requires reauthoring those into its dialect. Disqualifies
itself on the headline use case.

**Our position.** Own the engine. The boring layers (HTML5 parsing,
CSS tokenizing, DOM, selector matching) are delegated to **lexbor**,
which is genuinely correct and maintained. The interesting layers
(cascade, computed style, block/inline layout, paint, imm
reconciliation) are ours, and we hold them to a "every claimed feature
passes its tests" discipline.

## Why lexbor for parsing / DOM / selectors

lexbor is Apache-2, modular, fast, spec-pedantic. It implements:

- HTML5 tokenizer + tree builder (full spec)
- CSS3 tokenizer + rule parser
- DOM with mutation API
- CSS selector matching against the DOM (`lexbor/selectors`)
- Encoding, URL parsing, namespace handling

These are exactly the parts of "an HTML engine" where:

1. The spec is brutal and well-defined.
2. There's no design freedom for us to add value.
3. Years of edge-case bugs are already paid down upstream.
4. Writing it ourselves would mean shipping at "litehtml quality" for a
   year before reaching parity.

We adopt lexbor at the bottom and own everything above the matched-
selector level. Their DOM is the *retained tree*; our `ComputedStyle`
table sits alongside, keyed by node pointer; our box tree references
both during layout.

**Trade-off accepted:** the DOM is lexbor's structure, not ours.
Attaching engine state per-node goes through lexbor's custom-data API.
Slightly less ergonomic than fully-owned, but architecturally fine and
worth the win.

## Why NanoVG, not Skia

| | NanoVG | Skia |
|---|---|---|
| Source size | ~5k LOC | ~1M+ LOC |
| Binary footprint | ~150 KB | ~10 MB |
| Build time | seconds | hours |
| GPU backends | thin shim per backend | many built in |
| sRGB-correct | approximate | yes |
| Subpixel text | no | yes |
| **Good enough for app UI** | yes | yes |
| **In scope for this project** | yes | no |

Need pixel-parity with Chrome? Use WebView2 / WKWebView / CEF. AffineUI
is for "looks good" beating "looks identical to Chrome."

## Why Yoga for flexbox

Flexbox is one of the most subtle layout algorithms in the spec.
Baseline alignment, percentage gaps inside content-sized items,
`flex-basis: auto` semantics, wrap-reverse — corners that took the
React Native team years to find and fix. Yoga is ~5k LOC of paid-down
flexbox bugs.

Cost: Yoga's C++ API drifts. We pin a commit, vendor it, don't track
HEAD. Adapter glue between our `ComputedStyle` and Yoga's
`YGNodeStyleSet*` is ~200 LOC. Net win: we don't write the next two
years of "your flex container doesn't align right when…" bug reports.

Block and inline layout are *ours* — Yoga doesn't pretend to handle
those, and the algorithms are well-specified and finite-scope.

## Why we encapsulate sokol

The natural integration target — a C++ game using sokol for its own
rendering — would link-fail if both the host and AffineUI defined
`SOKOL_IMPL` separately. Sokol has process-global state, so even if
the link succeeded, two contexts would step on each other at runtime.

The solution lives at three levels:

1. **All sokol symbols are file-local.** The painter's implementation
   TU compiles sokol with `#define SOKOL_API_DECL static` and
   `#define SOKOL_API_IMPL static`. Even at link time, `sg_*` symbols
   never enter the global namespace from AffineUI.
2. **Public API never mentions sokol.** The `Painter` abstract class is
   the only seam visible from outside; nothing in
   `include/affineui/` includes `sokol_*.h`. A host can use AffineUI
   without knowing we use sokol.
3. **BYO escape hatch.** For hosts that *want* AffineUI to render into
   their existing sokol context (game already running sokol's frame
   loop), the `AFFINEUI_HOST_PROVIDES_SOKOL=ON` CMake option suppresses
   our impl TU and uses the host's sokol context via a host-supplied
   `sg_pipeline`. Standalone mode (the default) and embedded mode
   (BYO) share the same painter code; only the lifecycle differs.

Same encapsulation pattern is applied to NanoVG, stb_image,
stb_truetype, and fontstash. Each has a `AFFINEUI_HOST_PROVIDES_*`
flag.

## Why two front-ends (retained + imm) instead of one

Different surfaces want different ergonomics:

- A **settings dialog** authored by a designer in HTML/CSS wants
  retained mode. They edit the HTML, they edit the CSS, they save.
- A **HUD in a game** that mirrors player health, mana, inventory
  wants imm mode. Engine state is the source of truth; React-style
  reconciliation is the cleanest way to keep the DOM in sync without
  manual `setText` / `setVisible` / `addChild` calls.

Both modes share *everything below them*: same DOM (lexbor's), same
cascade, same painter, same event routing. The imm runtime is a thin
translation layer between virtual nodes and lexbor's mutation API.

You can mount an imm island inside a retained shell. This is the way
to get designer-authored chrome with engine-driven content.

## Why imm doesn't re-run every frame

Dear ImGui rebuilds everything every frame because (a) its UI
construction is cheap and (b) there's no expensive layout step
downstream. Neither holds here — CSS cascade + box layout for a
non-trivial DOM is more expensive than the painter. Doing it 60 times
a second when nothing changed would burn battery for no reason.

Instead: React's model. A render pass only happens when a state hook
mutated, an event handler ran, or the embedder explicitly invalidated.
The retained DOM keeps repainting on the GPU between passes, and CSS
transitions / animations run there — so the *visual* frame rate is
decoupled from the *logical* re-render rate.

## Packaging plan

Three forms ship from the same source:

| Form | Files | Best for |
|---|---|---|
| **Two-file amalgamation** | `affineui.h` + `affineui.cpp` | "Just drop it in" — the default integration story |
| **Static lib + header** | `libaffineui.a` (or `.lib`) + `affineui.h` | Avoid recompiling the impl in your builds |
| **CMake source repo** | the repo as-is | Contributors and people who want to tweak |

Single-file header was considered and rejected. At ~100k LOC
amalgamated, the costs (per-TU parse, editor experience, debug stack
traces) outweigh the marketing benefit of "world's largest single
header." Two-file pair delivers ~95% of the "feels easy" win at ~0% of
the cost. sqlite ships exactly this way; we follow the precedent.

The amalgamator (`tools/amalgamate.py`) produces the two-file output
from the modular source. It runs on every release and as a CI smoke
test.

## Why a thin C ABI on top of C++

Three reasons:

1. **Bindings.** Driving AffineUI from Rust, Zig, Odin, Python via FFI
   gets a stable surface without a C++ ABI nightmare.
2. **DLL-friendly.** A C ABI survives MSVC vs MinGW, libstdc++ vs
   libc++, debug-CRT vs release-CRT.
3. **It costs little.** The C surface is ~30 functions wrapping the
   C++ classes. The C++ surface remains the primary, idiomatic API.

## Why MIT despite mixed upstream licenses

Vendored dependencies retain their licenses:

- lexbor: Apache-2.0
- Yoga: MIT
- sokol: zlib
- NanoVG: zlib
- stb_*: MIT / public domain dual
- fontstash: zlib

All are permissive and pairwise compatible. Apache-2 is technically
more restrictive than MIT (patent grant, attribution file) but doesn't
contaminate our MIT licensing in any practical sense — we just have a
NOTICES file listing attributions. See
[external/README.md](../external/README.md) for the inventory.

## Why vendor instead of FetchContent

Curated dependency sources are checked in under `external/`. A normal
clone can run CMake and the two-file generator offline,
deterministically, on any machine with a supported toolchain.
FetchContent fails on locked-down build agents, behind corporate
proxies, and on CI runners with cold caches. Curated vendoring keeps the
source inventory explicit and reviewable.

## Why C++20

Yoga, our flexbox helper, is a C++20 project — adopting it set the
floor. Once we're paying for C++20 anyway, the language wins are
worth using directly: `std::source_location` for clean call-site
identity in imm-mode (no more `__builtin_LINE` fallback), concepts
for clearer template constraints, designated initializers, three-way
comparison, `std::span`, `consteval`. We avoid modules — they still
fight single-header deps like sokol and complicate the amalgamated
distribution.

Minimum compiler floor: MSVC 19.30+ (VS 2022), Clang 13+, GCC 11+,
Apple Clang 14+. All shipping for years.

## Real-time render architecture

AffineUI targets **real-time animated UI** — game HUDs, in-engine
tools, anything where the host needs to keep its own GPU budget free
and AffineUI must stay essentially invisible on idle frames. That
constraint is the single most important architectural pressure on
the project. Correctness without this performance shape is not
shippable.

### Five-stage pipeline, every stage cached

```
Style    →  Layout    →  Paint        →  Rasterize    →  Composite
(cascade)   (boxes)      (display       (DL → GPU       (textured
                          lists)         texture)         quads)
   ▲           ▲            ▲                ▲              ▲
   style-      layout-      paint-           layer-         every
   dirty       dirty        dirty            dirty          frame
```

The bottom stage — Composite — is the only thing that runs every
frame, and it's a handful of textured quads. Idle UIs cost
microseconds. Each stage above is gated on its own dirty bit; clean
stages skip themselves entirely.

### What invalidates what

| Change | Style | Layout | Paint | Rasterize | Composite |
|---|---|---|---|---|---|
| `color` change | ✓ | | ✓ | ✓ | ✓ |
| `padding` change | ✓ | ✓ | ✓ | ✓ | ✓ |
| Add a child | | ✓ | ✓ | ✓ | ✓ |
| Text content change | | (maybe) | ✓ | ✓ | ✓ |
| `transform` animating | | | | | ✓ |
| `opacity` animating | | | | | ✓ |
| Scroll within overflow | | | | | ✓ |
| Mouse move (no `:hover`) | | | | | ✓ |
| Mouse move over `:hover` rule | ✓ (subtree) | | ✓ | ✓ (layer) | ✓ |

**Transforms and opacity are composite-only.** A flashing health
bar, a fading tooltip, a scrolling list — all of these touch zero
paint work, zero rasterize work. They update a single uniform on the
composite quad.

### Layer model

A "layer" is a chunk of content that can be **rasterized once and
composited many times**. By default the whole document is one
layer. A new layer is created when an element has any of:

- `will-change: transform | opacity` (explicit CSS hint)
- non-identity `transform`
- `opacity < 1`
- `overflow: hidden` with scrolling content
- 3D transforms or `perspective`
- `position: fixed`
- explicit imm-mode `key()` on a list item the embedder marks as
  animatable

Each layer owns a GPU texture (allocated on first rasterize, freed
when the layer is gone). Rasterize re-runs only when the layer's
display list has changed. Composite uses the cached texture.

We don't promote every element to its own layer — too many small
layers becomes a GPU-memory bottleneck. The heuristics above are
the same ones Chrome's compositor uses, for the same reasons.

### Display lists as the paint output

Paint walks the box tree and emits **display lists** —
`vector<PaintOp>` where `PaintOp` is one of `FillRect`, `RoundRect`,
`DrawText`, `DrawImage`, `PushClip`, `PopClip`, `BeginLayer`,
`EndLayer`. Display lists are:

- Cheap to build (no GPU work involved)
- Cheap to diff (so we can detect when a layer's content is
  unchanged and skip rasterize)
- Cheap to cache (they're plain POD vectors)

Rasterize takes a display list and runs it through NanoVG into a
GL framebuffer object (`NVGLUframebuffer`). The compositor never
touches NanoVG — it issues a small hand-written GL shader against
textured quads.

### GPU utilization

The compositor is where the per-frame GPU work concentrates because
it's the only thing running every frame for idle UI. It's
deliberately minimal: one VAO of textured quads, one shader (texture
sample × opacity × transform), one draw call per layer (batched into
fewer when textures fit in an atlas).

NanoVG remains the *rasterizer* — used inside the rasterize stage to
turn display lists into layer textures. It's not in the per-frame
hot path.

### Frame budget contract (aspirational)

```
Idle UI (nothing changed):
    composite pass only           ~50 μs        on an M1 with 5 layers

Hover effect:
    style recalc (1 elem)         ~5 μs
    paint dirty layer             ~20 μs
    rasterize that layer          ~200 μs
    composite                     ~50 μs
                                 ─────────
                                  ~275 μs       (0.3 ms of 16.6 ms)

Full re-layout (window resize):
    style                         ~500 μs
    layout                        ~1.5 ms
    paint                         ~500 μs
    rasterize all layers          ~5 ms
    composite                     ~50 μs
                                 ─────────
                                  ~7.5 ms       (worst case, once per
                                                 resize)
```

These numbers are aspirational; what's load-bearing is the *shape*
of the budget — idle frames cost nothing, animations cost composite-
only, content changes cost only the affected layer.

### Why this constrains every other decision

Every design choice elsewhere in the engine has to be evaluated
against this pipeline. Examples:

- **Lexbor's cascade.** We use it because it's correct. If it
  becomes the bottleneck for incremental style invalidation, we
  swap in our own resolver behind the `StyleResolver` interface —
  the rest of the engine doesn't know.
- **NanoVG.** Stays as the rasterizer. Doesn't run on idle frames.
  If subpixel correctness needs change later, we can replace it
  without disturbing layout or composite.
- **Sokol.** Provides the GL context the rasterizer renders into
  and the compositor draws from. The compositor's draw calls are
  not routed through any NanoVG indirection.
- **imm-mode reconciliation.** Patches the retained DOM, which
  flips style-dirty / layout-dirty / paint-dirty bits as needed.
  The five-stage pipeline picks up from there.

### Style invalidation strategy (subsection of the above)

Browsers spend significant engineering on the style system because the
naive "re-run the cascade on every change" approach is what makes
desktop UI feel slow. The well-known optimizations are:

1. **Bloom filter prefilter** over ancestor classes/tags/ids to drop
   rules that can't possibly match.
2. **Rule indexing by rightmost simple selector** so an element only
   considers a fraction of the stylesheet.
3. **Invalidation sets** — per-selector "what DOM mutation could
   invalidate me," so attribute changes touch a small slice of the
   rule index.
4. **Computed-style sharing** — siblings with identical matched-rule
   sets + identical inherited values share one `ComputedStyle`.
5. **Inheritance cache** — inherited properties point up the tree.
6. **Subtree dirty bits** — DOM mutations mark subtrees; clean
   subtrees skip cascade entirely.
7. **Sibling invalidator graphs** for `+`/`~` combinators.

Phase 2 doesn't implement most of those. But the architecture has to
**not preclude them** — once they're needed, retrofitting is
expensive if the seams are wrong. Our concrete commitments:

- **`ComputedStyle` is stored per-element, not recomputed per frame.**
  Attached to the lexbor DOM node via its user-data slot.
- **Per-element style dirty bit.** `compute_style(element)` returns
  the cached struct when clean. DOM mutations set the bit and
  propagate to descendants only for inherited properties.
- **Resolver interface.** Style resolution lives behind a
  `style::Resolver` abstraction (one method:
  `ComputedStyle resolve(Element*)`). Phase 2 ships an impl that
  delegates to lexbor's cascade. Later phases can swap in a custom
  matcher with the optimizations above without touching the rest of
  the engine.
- **Cascade is a separate engine phase**, not interleaved with
  layout. Profiling and caching live on the cascade side; layout
  consumes immutable ComputedStyle.

What we will *not* try in Phase 2: bloom filters, invalidation sets,
computed-style sharing, sibling invalidator graphs. Those land if
profiling shows lexbor's cascade is a bottleneck — until then they
are speculative complexity.

## Correctness discipline

"Every CSS property we claim to support has tests that pass."
Non-negotiable. The litehtml failure mode — shipping half-working
features and never closing them — is the failure we are explicitly
designed against. Where the spec has gnarly corners (margin
collapsing, inline baseline alignment, percentage `min-height` inside
`auto` parents, flex baseline cross-alignment) the tests cover the
corners, not just the happy path.

The corollary: we ship a *smaller* feature surface than litehtml,
and everything in that surface is rock solid. Bootstrap and Material
work end to end on the supported subset; anything outside the subset
is documented as out of scope rather than "partial."

Tools:
- **doctest** for unit tests (DOM, cascade, layout, paint primitives).
- **WPT subset** mirrored as integration tests for the parser/CSS
  layers (mostly proves we're using lexbor correctly).
- **Golden image tests** for the painter — committed PNGs for ~50
  canonical mini-pages, diffed in CI.

## Open questions / things still being figured out

- **Subpixel positioning.** NanoVG rounds to pixel grid. Fine for most
  UI; bad for tight typography. Possible to override in the painter.
- **Hot reload.** Want `.css` files to live-reload during dev. Easy on
  the retained path, slightly tricky when imm has set inline styles.
- **DPI scaling.** sokol_app reports DPI scale per platform; we honor
  it for layout but text rasterization is still figuring out the right
  hinting story at non-integer scales.
- **HarfBuzz wiring.** Complex-script shaping is opt-in. Need to land
  the interface seam before opting in is meaningful.
