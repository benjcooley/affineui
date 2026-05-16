# AffineUI

> A tiny, GPU-accelerated HTML5/CSS UI engine for C++ games and tools.

AffineUI is an HTML5/CSS UI engine you drop into the game you already
have. You bring the window, the event loop, and the GPU context;
AffineUI brings the parser, the cascade, the layout, the paint, the
hit-testing, and the rendering — composited into your frame as a
single overlay.

The engine itself is built from focused parts: **lexbor** for
spec-correct HTML/CSS parsing and selector matching, **Yoga** for
flexbox math, **NanoVG** on **OpenGL 3** for GPU vector painting. The
cascade, layout adapter, paint driver, click routing, and immediate-
mode reconciler are ours. **First-class adapters ship for SDL2 and
sokol_app** — the two windowing toolkits most C++ games use. Other
toolkits work via a manual `affineui::Event` / `Ui::render` path.

**Status:** working engine — text wrapping, padding/margin/border,
rounded corners, flex layout, click handlers, cursor changes. Real
features land each session; tracker in [`docs/ROADMAP.md`](docs/ROADMAP.md).

## What it is, what it isn't

| ✅ Is | ❌ Isn't |
|---|---|
| HTML5 + CSS (the subset we test) | A JavaScript engine |
| Pure native, GPU-rendered | A web browser |
| Bootstrap / Material-friendly | An accessibility-complete platform widget set |
| Drops into your existing game | A windowing toolkit |

The compiled engine target: **< 2 MB**. JavaScript is intentionally
out of scope; drive UI state from your C++ code (or via the
immediate-mode API).

## Integration

You pick the adapter for the windowing toolkit your game already
uses. The same `affineui::Ui` facade sits underneath; only the glue
differs.

### SDL2 (the most common engine path)

```cpp
#include <affineui/affineui.h>     // CMake target affineui::sdl
                                   // defines AFFINEUI_WITH_SDL for you
#include <SDL.h>

int main() {
    SDL_Init(SDL_INIT_VIDEO);
    auto* window = SDL_CreateWindow("My Game",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_GL_CreateContext(window);

    affineui::Ui ui;
    ui.html(R"(<button id="quit">Quit</button>)");

    bool running = true;
    ui.on_click("#quit", [&]{ running = false; });

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (affineui::sdl::dispatch(ui, e)) continue;  // UI consumed it
            // your game's input handling
        }
        // your game's render pass
        affineui::sdl::render(ui, window);
        SDL_GL_SwapWindow(window);
    }
    SDL_Quit();
    return 0;
}
```

CMake:
```cmake
add_subdirectory(third_party/affineui)
target_link_libraries(my_game PRIVATE affineui::sdl)
```

### sokol_app (personal projects, tools, single-file builds)

```cpp
#include <affineui/affineui.h>     // CMake target affineui::sokol
                                   // defines AFFINEUI_WITH_SOKOL for you
#include <sokol_log.h>

int main() {
    affineui::Ui ui;
    ui.html(R"(<button id="quit">Quit</button>)");
    ui.on_click("#quit", []{ sapp_request_quit(); });

    sapp_desc desc{};
    desc.width = 1024; desc.height = 768;
    desc.window_title = "My Game";
    desc.high_dpi = true;
    desc.logger.func = slog_func;
    affineui::sokol::wire(desc, ui);   // installs frame + event callbacks
    sapp_run(&desc);
    return 0;
}
```

CMake:
```cmake
add_subdirectory(third_party/affineui)
target_link_libraries(my_game PRIVATE affineui::sokol)
```

### Manual (any other windowing toolkit)

If your game uses glfw, raylib, or a custom event loop, you build
`affineui::Event` yourself and call `Ui::render(fb_w, fb_h, dpi)` from
inside your render pass. ~30 lines of translation glue; the API is
fully exposed.

## Adapter coverage

| Adapter | Windowing | Graphics (today) | Graphics (Phase 3+) |
|---|---|---|---|
| `affineui::sokol` | sokol_app — Win/Mac/Linux/iOS/Android/Web | GL3 | Metal, D3D11, WebGPU via sokol_gfx |
| `affineui::sdl` | SDL2 — Win/Mac/Linux/iOS/Android/Web | GL3 | Metal (SDL_Metal), D3D11 |
| Manual | yours | GL3 | per your stack |

Both adapter paths give you **HiDPI handling, cursor changes,
high-precision input, and CSS-selector click routing** out of the box.

## What you get without writing CSS

Default styles via the user-agent stylesheet ship with the engine. A
blank document still renders readable, sensibly-spaced HTML.

## Two ways to drive content

**Retained:** call `ui.html("...")` once at setup; mutate via DOM-ish
methods or replace wholesale when state changes.

**Immediate (Dear ImGui-flavored):** describe the UI in a function
that calls `imm::div()`, `imm::button()`, etc. AffineUI diffs that
against the previous tree and patches the retained DOM (React-style
reconciliation). The view function only re-runs when state or events
have plausibly changed — painting still happens every frame off the
retained DOM, so CSS transitions / hover effects / animations work
without re-entering your view function.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the engine's
internal shape.

## Stack

| Layer | Library | License | Why |
|---|---|---|---|
| HTML5 + CSS parsing, DOM, selector matching | [lexbor](https://github.com/lexbor/lexbor) | Apache-2 | Spec-pedantic, maintained |
| Flexbox math | [Yoga](https://github.com/facebook/yoga) | MIT | Battle-tested via React Native |
| 2D vector painter | [NanoVG](https://github.com/memononen/nanovg) | zlib | Antialiased strokes/fills/gradients/text |
| Sokol windowing | [sokol](https://github.com/floooh/sokol) | zlib | Cross-platform window + GPU abstraction |
| Fonts | fontstash + stb_truetype | zlib / MIT | Atlas-based glyph cache |
| Raster images | stb_image | MIT / public | `<img>` decode for PNG/JPG/GIF |
| Tests | doctest | MIT | Fastest-compiling C++ test framework |

**Owned by us:** cascade resolution, computed style, padding /
margin / border / flex Yoga adapter, paint driver, hit-test, click
routing, immediate-mode reconciler. Everything where design judgment
matters.

**Delegated:** HTML5 tokenization, CSS3 tokenization, selector
matching, flexbox math, glyph rasterization, vector painting, window
+ input. Everything where spec compliance and battle-testing matter.

## Building

```bash
git clone https://github.com/youruser/affineui.git
cd affineui
./scripts/fetch_deps.sh         # one-time: pulls lexbor, yoga, sokol, nanovg, stb
cmake -S . -B build -G Ninja
cmake --build build
./build/examples/hello/hello            # sokol demo
./build/examples/hello_sdl/hello_sdl    # SDL2 demo (if SDL2 was found)
```

See [`docs/BUILDING.md`](docs/BUILDING.md) for platform-specific notes.

## Tree layout

```
include/affineui/      ← public headers; affineui.h is the umbrella
src/                   ← implementation
external/              ← vendored single-file deps (fetched by script)
examples/              ← runnable demos (each in its own dir)
tests/                 ← doctest unit tests
docs/                  ← architecture, design, contributor docs
cmake/                 ← reusable CMake modules
tools/                 ← ui-preview, benchmark
```

## License

[MIT](LICENSE). Vendored third-party components retain their original
licenses; see [external/README.md](external/README.md).

## Status & roadmap

See [`docs/ROADMAP.md`](docs/ROADMAP.md).
