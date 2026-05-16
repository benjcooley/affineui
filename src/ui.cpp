// affineui::Ui — the "one type" facade. Composes Document + Renderer
// behind a friendlier API: lazy GPU init via Renderer, CSS-style
// click handler registration, file loading.
//
// Handler selector matching (minimal grammar — Phase 2D scope):
//   "#id"   → element's id attribute equals "id"
//   ".cls"  → element has "cls" in its class list
//   "tag"   → element's tag name equals "tag"
//   "a,b"   → any of a/b matches
//
// More expressive selectors (descendant combinators, attribute
// matches, pseudo-classes) ride on a future lexbor-selectors
// integration — the surface (`on_click(selector, cb)`) doesn't
// change.

#include "affineui/ui.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace affineui {

namespace {

bool matches_simple(std::string_view sel, const Document::HoverInfo& info) {
    if (sel.empty()) return false;
    if (sel.front() == '#') {
        return info.elem_id == sel.substr(1);
    }
    if (sel.front() == '.') {
        const auto cls = sel.substr(1);
        return std::find(info.classes.begin(), info.classes.end(), cls)
               != info.classes.end();
    }
    // Bare ident → tag name match.
    return info.tag == sel;
}

// Trim ASCII whitespace.
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                          s.front() == '\n' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t' ||
                          s.back()  == '\n' || s.back()  == '\r')) s.remove_suffix(1);
    return s;
}

bool matches_selector_list(std::string_view selector_list,
                           const Document::HoverInfo& info) {
    // Comma-separated simple selectors. Any match → true.
    std::size_t i = 0;
    while (i < selector_list.size()) {
        const auto comma = selector_list.find(',', i);
        const auto piece = trim(selector_list.substr(
            i, (comma == std::string_view::npos ? selector_list.size() : comma) - i));
        if (matches_simple(piece, info)) return true;
        if (comma == std::string_view::npos) break;
        i = comma + 1;
    }
    return false;
}

}  // namespace

namespace detail {

struct UiImpl {
    Document document;
    Renderer renderer;

    // Click handlers, in insertion order. A single click can fire
    // multiple handlers if multiple selectors match (mirrors DOM
    // event bubbling intuitively at the registration site).
    std::vector<std::pair<std::string, std::function<void()>>> click_handlers;
};

}  // namespace detail

Ui::Ui() : impl_(std::make_unique<detail::UiImpl>()) {}
Ui::~Ui() = default;
Ui::Ui(Ui&&) noexcept            = default;
Ui& Ui::operator=(Ui&&) noexcept = default;

// ── Content ─────────────────────────────────────────────────────────

void Ui::html(std::string_view source) {
    impl_->document.set_html(source);
}

void Ui::css(std::string_view source) {
    impl_->document.set_user_stylesheet(source);
}

bool Ui::load(std::string_view path) {
    std::ifstream f{std::string(path)};
    if (!f.good()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    html(ss.str());
    return true;
}

// ── Immediate mode ──────────────────────────────────────────────────

void Ui::mount(std::function<void()> view_fn) {
    impl_->document.set_imm_view(std::move(view_fn));
}

void Ui::invalidate() {
    impl_->document.invalidate_imm();
}

// ── Rendering ───────────────────────────────────────────────────────

void Ui::render(int fb_w, int fb_h, float dpi_scale) {
    // Run the imm view fn (if mounted + dirty) before paint. The
    // runtime mutates the DOM in place; we re-cascade + re-collect
    // inside tick_imm so layout/paint see the new tree.
    impl_->document.tick_imm();
    impl_->renderer.render(impl_->document, fb_w, fb_h, dpi_scale);
}

// ── Input ───────────────────────────────────────────────────────────

bool Ui::dispatch(const Event& e) {
    const auto result = impl_->document.dispatch(e);
    (void)result;  // currently we don't distinguish redraw vs consumed

    // Click routing: on MouseUp, check (1) user-registered selectors
    // via on_click, then (2) imm-mode handlers for elements stamped
    // with `aui-imm-{hash}` ids. The user-handler path runs first so
    // explicitly-bound handlers (e.g. retained-mode UI atop an imm-
    // mode island) override.
    if (e.type == EventType::MouseUp && e.button == MouseButton::Left) {
        const auto info = impl_->document.hovered_info();
        if (info.valid) {
            bool consumed = false;
            for (const auto& [selector, cb] : impl_->click_handlers) {
                if (matches_selector_list(selector, info)) {
                    cb();
                    consumed = true;
                }
            }
            if (consumed) return true;
            // imm-mode handler hit?
            if (impl_->document.invoke_imm_click(info.elem_id)) {
                return true;
            }
        }
    }
    return false;
}

int Ui::hovered_cursor() const {
    return impl_->document.hovered_cursor();
}

// ── Handlers ────────────────────────────────────────────────────────

void Ui::on_click(std::string_view selector, std::function<void()> cb) {
    impl_->click_handlers.emplace_back(std::string(selector), std::move(cb));
}

// ── Config ──────────────────────────────────────────────────────────

void  Ui::set_clear_color(Color c)  { impl_->renderer.set_clear_color(c); }
Color Ui::clear_color() const       { return impl_->renderer.clear_color(); }

// ── Access ──────────────────────────────────────────────────────────

Document&       Ui::document()       { return impl_->document; }
const Document& Ui::document() const { return impl_->document; }
Renderer&       Ui::renderer()       { return impl_->renderer; }
const Renderer& Ui::renderer() const { return impl_->renderer; }

Size Ui::content_size() const { return impl_->document.content_size(); }

}  // namespace affineui
