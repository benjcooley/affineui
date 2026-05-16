#pragma once

#include "affineui/types.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace affineui {

class App;
class Painter;

namespace detail {
struct DocumentImpl;
}

/// A parsed HTML document with its associated CSS, layout, and event state.
/// Owned by an App, but can also be used headless for layout / testing.
class Document {
public:
    Document();
    ~Document();

    Document(const Document&)            = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&&) noexcept;
    Document& operator=(Document&&) noexcept;

    /// Parse and replace the document body. Previous DOM is discarded.
    void set_html(std::string_view html);

    /// Inject (or replace) the user stylesheet. Applied on top of the
    /// document's own <style> blocks and <link> imports.
    void set_user_stylesheet(std::string_view css);

    /// Reapply stylesheets without re-parsing the DOM. Cheap; intended
    /// for hot-reload workflows.
    void reload_stylesheets();

    /// Trigger a layout pass against the given viewport width. Called
    /// automatically on resize and after `set_html`; exposed for tests.
    ///
    /// `measurer` is consulted for per-font text metrics (used to size
    /// text-bearing leaves precisely). Pass nullptr for tests or
    /// headless cases — layout falls back to a conservative `font_size`
    /// estimate that may produce vertically-asymmetric padding.
    void layout(int viewport_width, Painter* measurer = nullptr);

    /// Paint the current document into `painter`. `painter` is whatever
    /// the embedder supplies — typically an internal NanoVG-backed one.
    void draw(Painter& painter);

    /// Route an OS / app event through litehtml. Returns whether a
    /// redraw and/or imm-view re-evaluation is needed.
    DispatchResult dispatch(const Event& ev);

    /// Embedder-supplied resource loader for `<img src=...>`,
    /// `<link rel=stylesheet href=...>`, etc.
    void set_resource_loader(ResourceLoader loader);

    /// Current document size after the last layout pass.
    Size content_size() const;

    /// Identity of the currently-hovered element, for click routing.
    /// `valid` is false when nothing is hovered. `tag` / `elem_id` /
    /// `classes` are populated from the element's HTML attributes —
    /// enough for the minimal selector grammar in Ui::on_click.
    struct HoverInfo {
        bool                     valid{false};
        std::string              tag;
        std::string              elem_id;
        std::vector<std::string> classes;
    };
    HoverInfo hovered_info() const;

    // ── Immediate-mode view (Phase 2D — "clear and rebuild") ────────

    /// Install an imm-mode view function. The function will be called
    /// when the document is dirty (state mutation or explicit
    /// invalidate_imm); it should produce the desired UI via the
    /// affineui::imm API.
    void set_imm_view(std::function<void()> view_fn);

    /// True when the imm view should be re-run before the next paint.
    /// Set by state mutations + the explicit `imm::invalidate()` call.
    bool imm_dirty() const;

    /// Mark the imm view dirty. Next tick_imm() will re-run the view
    /// function.
    void invalidate_imm();

    /// If dirty, clear the document body, run the view function, then
    /// re-cascade + re-collect blocks so paint sees the new DOM.
    /// No-op when there is no view function or nothing is dirty.
    void tick_imm();

    /// Invoke an imm-mode click handler matching `elem_id` (the form
    /// `aui-imm-{hash}` auto-assigned during view-fn rebuilds), if one
    /// is registered. Returns true on hit. Ui::dispatch consults this
    /// after its own selector-based handlers when MouseUp lands on
    /// an element with such an id.
    bool invoke_imm_click(std::string_view elem_id);

    /// Cursor enum that the OS should display under the current
    /// hovered element. Returned as `int` to avoid exporting the
    /// internal `detail::ComputedStyle::Cursor` enum through the
    /// public header. Map onto your platform's cursor API on the
    /// host side (App does this for sokol_app).
    ///   0 = default, 1 = pointer, 2 = text, 3 = crosshair, 4 = move,
    ///   5 = not-allowed, 6 = ew-resize, 7 = ns-resize
    int hovered_cursor() const;

private:
    std::unique_ptr<detail::DocumentImpl> impl_;
    friend class App;
};

}  // namespace affineui
