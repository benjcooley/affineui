#pragma once

// affineui::Ui — the "one type" facade game engineers actually use.
//
// Composes a Document (parsed HTML/CSS + layout + paint output) and a
// Renderer (graphics-API-specific drawing). Adds a friendly surface:
// CSS-selector click handlers, lazy GPU init, a single `render(w, h,
// dpi)` call. Designed to be IDE-discoverable without reading docs.
//
// Typical use through a windowing-toolkit adapter:
//
//     #define AFFINEUI_WITH_SOKOL
//     #include <affineui/affineui.h>
//
//     affineui::Ui ui;
//     ui.html("<button id='quit'>Quit</button>");
//     ui.on_click("#quit", []{ sapp_request_quit(); });
//
//     sapp_desc d{};
//     affineui::sokol::wire(d, ui);
//     sapp_run(&d);
//
// Threading: single-threaded. All methods must be called on the same
// thread that owns your graphics context.

#include "affineui/document.h"
#include "affineui/renderer.h"
#include "affineui/types.h"

#include <functional>
#include <memory>
#include <string_view>

namespace affineui {

namespace detail { struct UiImpl; }

class Ui {
public:
    Ui();
    ~Ui();

    Ui(const Ui&)            = delete;
    Ui& operator=(const Ui&) = delete;
    Ui(Ui&&) noexcept;
    Ui& operator=(Ui&&) noexcept;

    // ── Content ─────────────────────────────────────────────────────

    /// Replace the document HTML. Re-parses, re-cascades, re-lays-out
    /// on the next render(). Cheap to call infrequently.
    void html(std::string_view source);

    /// Set the user stylesheet — sits above the document's <style>
    /// blocks in the cascade. Call this once at setup for app theming.
    void css(std::string_view source);

    /// Load HTML from a file. Returns true on success. Convenience —
    /// equivalent to read-the-file-and-call-html.
    bool load(std::string_view path);

    // ── Rendering ───────────────────────────────────────────────────

    /// Render one frame of the document into the current framebuffer.
    /// You supply the framebuffer dimensions in pixels and the DPI
    /// scale (pixels-per-point — 1.0 standard, 2.0 Retina). Call this
    /// once per game-frame from inside your render pass.
    ///
    /// First call lazily initializes GPU resources against the
    /// currently bound graphics context — no explicit init needed.
    void render(int fb_w, int fb_h, float dpi_scale);

    // ── Immediate mode ──────────────────────────────────────────────

    /// Install an immediate-mode view function. The function is
    /// called on the next render (and whenever state mutates / a click
    /// handler runs / `invalidate()` is called). Inside it, call
    /// affineui::imm::div(), button(), use_state(), etc. to describe
    /// the desired UI. The Document mutates its DOM in place to
    /// match.
    void mount(std::function<void()> view_fn);

    /// Mark the imm view as needing re-evaluation before the next
    /// render. Safe to call from anywhere on the same thread; useful
    /// when external state outside an imm `use_state` slot has
    /// changed.
    void invalidate();

    // ── Input ───────────────────────────────────────────────────────

    /// Forward a translated event to the document. Returns whether
    /// the UI "consumed" the event (e.g. a click hit a registered
    /// handler). If true, your game should suppress its own handling.
    bool dispatch(const Event& e);

    /// Cursor the OS should display under the last hovered position.
    /// Maps onto your windowing toolkit's cursor enum.
    ///   0 = default, 1 = pointer, 2 = text, 3 = crosshair, 4 = move,
    ///   5 = not-allowed, 6 = ew-resize, 7 = ns-resize
    int hovered_cursor() const;

    // ── Handlers ────────────────────────────────────────────────────

    /// Register a click callback for elements matching a CSS-style
    /// selector. Phase 2D supports a minimal selector grammar:
    ///   "#id"   — element with matching id attribute
    ///   ".cls"  — element with matching class
    ///   "tag"   — element with matching tag name
    ///   "a,b"   — comma-separated list (any matches)
    /// Compound selectors (`div.card`) and combinators are Phase 3.
    void on_click(std::string_view selector, std::function<void()> cb);

    // ── Configuration ──────────────────────────────────────────────

    /// Background color drawn behind the document. Default catppuccin
    /// Base (#1e1e2e).
    void  set_clear_color(Color c);
    Color clear_color() const;

    // ── Escape hatches (advanced) ──────────────────────────────────

    Document&       document();
    const Document& document() const;
    Renderer&       renderer();
    const Renderer& renderer() const;

    /// Current document content size after the last layout pass.
    Size content_size() const;

private:
    std::unique_ptr<detail::UiImpl> impl_;
};

}  // namespace affineui
