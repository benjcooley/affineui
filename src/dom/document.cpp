// affineui::Document — Phase 2A.
//
// Parses HTML with lexbor, attaches stylesheets (user-agent + user +
// any embedded `<style>` blocks) through lexbor's cascade, then for
// each block-level element collects a `ResolvedStyle` (ComputedStyle
// + AnimatedStyle) via the StyleResolver. The Phase 1 `style_for(tag)`
// fallback is gone — real CSS now drives every visible attribute we
// expose this phase.
//
// Lifetime: the lxb_html_document_t and the resolver live for the
// lifetime of DocumentImpl. Element pointers in our Block list remain
// valid until the next set_html() (which replaces the document).
//
// What's intentionally still simple (Phase 2B-2E plan):
//   - One flat list of block-level elements; no nested boxes.
//   - Painter is invoked directly each frame (no DisplayList yet).
//   - Resolver is uncached. We pay the walk on every set_html, never
//     per frame.

#include "affineui/document.h"

#include "affineui/painter.h"
#include "affineui/themes.h"
#include "imm/imm_runtime.h"
#include "internal/animated_style.h"
#include "internal/computed_style.h"
#include "internal/element_id.h"
#include "internal/style_resolver.h"
#include "internal/style_store.h"
#include "layout/yoga_adapter.h"

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if !defined(AFFINEUI_STUB_BUILD)
#    include <lexbor/css/css.h>
#    include <lexbor/dom/dom.h>
#    include <lexbor/html/html.h>
#endif

namespace affineui {

namespace {

// A laid-out, paintable block. Style data lives in the Document's
// StyleStore (SoA); Block just carries the handle + the cheap, block-
// specific stuff (text content, computed bounds, tag for debugging).
//
// `parent_idx` is the index into Document::blocks of this block's
// containing block (or -1 for top-level). Blocks are appended in DFS
// order during collect_blocks(), so a parent always has a lower index
// than its children — that lets paint walk the vector linearly and
// hit parents before children (correct z-order for the box-bg-then-
// text emit pattern).
struct Block {
    detail::ElementId id{};        // StyleStore handle
    std::string       tag;
    std::string       elem_id;     // value of the `id` attribute (for "#x" selectors)
    std::vector<std::string> classes;  // tokenized `class` attribute
    std::string       text;
    int               parent_idx{-1};
    Rect              bounds{};
};

#if !defined(AFFINEUI_STUB_BUILD)
// :hover side-table entry. Populated by scan_hover_rules() at
// stylesheet-attach time. We keep only the simple-selector subset
// for MVP: a single tag/class/id identifier plus :hover. Descendant
// combinators land in Phase 4 (will replay through lxb_selectors_find
// against a state-aware match callback).
struct HoverRule {
    enum class Kind : std::uint8_t { Tag, Class, Id };
    Kind                                          kind;
    std::string                                   name;
    const lxb_css_rule_declaration_list_t*        decls;
};

// Per-element :hover bit lives inside StyleStore::state_bits(). We
// only claim bit 0 today; :active/:focus stake out bits 1/2 later.
constexpr std::uint8_t kHoverStateBit = 1u << 0;
#endif

}  // namespace

namespace detail {

struct DocumentImpl {
    std::string               html;
    std::string               user_stylesheet;
    ResourceLoader            resource_loader;
    Size                      content_size{0, 0};
    std::vector<Block>        blocks;
    bool                      paint_dirty{true};  // Phase 2C flips this

    // Interaction state. -1 = pointer is over no block (or off-window).
    // Updated by Document::dispatch; read by App to drive cursor +
    // :hover and click routing. `hovered_chain` is the ancestor chain
    // of `hovered_idx` (deepest → root). Recomputed on every
    // hover-changing event; diffed against the previous chain to
    // toggle the :hover state bit per affected element.
    int                       hovered_idx{-1};
    std::vector<int>          hovered_chain;
    Point                     last_mouse_pos{};

    // Immediate-mode runtime — lazily created on the first
    // set_imm_view() call. Holds state slots, click handlers, and the
    // view function across re-renders.
    std::unique_ptr<ImmRuntime> imm;

    // Per-element style + dirty bookkeeping. Lives across set_html()
    // calls; reset() inside set_html() recycles capacity.
    StyleStore                style_store;

#if !defined(AFFINEUI_STUB_BUILD)
    lxb_html_document_t*               doc{nullptr};
    std::vector<lxb_css_stylesheet_t*> sheets;
    // :hover overlay rules — populated by scan_hover_rules() during
    // attach. Pointers in `decls` reference rule data owned by the
    // document's CSS memory pool; valid for the document's lifetime.
    std::vector<HoverRule>             hover_rules;
    std::unique_ptr<StyleResolver>     resolver;
    ResolvedStyle                      root_style{};  // inheritance root
#endif

    ~DocumentImpl() {
#if !defined(AFFINEUI_STUB_BUILD)
        resolver.reset();
        // Stylesheets are owned by the document's CSS memory pool once
        // attached — destroying the document tears them down. Just drop
        // our tracking refs.
        sheets.clear();
        if (doc) lxb_html_document_destroy(doc);
#endif
    }
};

}  // namespace detail

namespace {

#if !defined(AFFINEUI_STUB_BUILD)

// ── DOM utilities ───────────────────────────────────────────────────

// CSS `white-space: normal` text content: collapse all whitespace
// runs (spaces, tabs, newlines, vertical-tabs, form-feeds, carriage-
// returns) to a single space character, and trim the result.
// Multi-line text in HTML source is *not* preserved as multi-line
// in the rendered output by default — the wrap engine breaks lines
// based on the *available width*, not the source line breaks.
//
// Phase 2D widening: honor `white-space: pre`/`pre-wrap` by reading
// the inherited white-space ComputedStyle and skipping the collapse
// for those modes.
std::string node_text(lxb_dom_node_t* node) {
    size_t len = 0;
    lxb_char_t* raw = lxb_dom_node_text_content(node, &len);
    if (!raw || len == 0) return {};

    const auto is_ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' ||
               c == '\r' || c == '\f' || c == '\v';
    };

    std::string out;
    out.reserve(len);
    bool prev_was_ws = true;  // leading whitespace is dropped
    for (std::size_t i = 0; i < len; ++i) {
        const auto c = static_cast<unsigned char>(raw[i]);
        if (is_ws(c)) {
            if (!prev_was_ws) out.push_back(' ');
            prev_was_ws = true;
        } else {
            out.push_back(static_cast<char>(c));
            prev_was_ws = false;
        }
    }
    // Trim a trailing space we may have appended just before EOF.
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string tag_name(lxb_dom_element_t* elem) {
    size_t len = 0;
    const lxb_char_t* name = lxb_dom_element_qualified_name(elem, &len);
    if (!name || len == 0) return {};
    return std::string(reinterpret_cast<const char*>(name), len);
}

// Pull the value of an attribute as a std::string, empty if absent.
std::string attr_string(lxb_dom_element_t* elem, std::string_view name) {
    size_t len = 0;
    const lxb_char_t* v = lxb_dom_element_get_attribute(
        elem,
        reinterpret_cast<const lxb_char_t*>(name.data()), name.size(),
        &len);
    if (!v || len == 0) return {};
    return std::string(reinterpret_cast<const char*>(v), len);
}

// Tokenize a class attribute on whitespace runs.
std::vector<std::string> split_classes(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) ++i;
        if (i >= s.size()) break;
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '\n') ++j;
        out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool is_block_tag(const std::string& tag) {
    return tag == "h1" || tag == "h2" || tag == "h3" ||
           tag == "h4" || tag == "h5" || tag == "h6" ||
           tag == "p"  || tag == "div" ||
           tag == "section" || tag == "article" || tag == "header" ||
           tag == "footer"  || tag == "main"    || tag == "nav" ||
           // Form-ish + a few common containers. We treat them as
           // block-level for layout — Phase 3 inline layout splits
           // these into their proper inline / inline-block flow.
           tag == "button" || tag == "input"  || tag == "label" ||
           tag == "form"   || tag == "ul"     || tag == "ol" ||
           tag == "li"     || tag == "a"      || tag == "span" ||
           tag == "img";
}

// ── Stylesheet extraction ──────────────────────────────────────────
//
// Walk the parsed DOM looking for <style> elements and collect their
// inner text. This is how `<style>body { color: red }</style>` in
// the page gets through to the cascade.

void collect_style_text(lxb_dom_node_t* node, std::string& out) {
    for (auto* c = lxb_dom_node_first_child(node); c;
         c = lxb_dom_node_next(c)) {
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            auto* el = lxb_dom_interface_element(c);
            if (tag_name(el) == "style") {
                size_t len = 0;
                if (auto* t = lxb_dom_node_text_content(c, &len); t && len) {
                    out.append(reinterpret_cast<const char*>(t), len);
                    out.push_back('\n');
                }
                continue;  // skip <style>'s descendants
            }
        }
        collect_style_text(c, out);
    }
}

// Parse + attach a single stylesheet string. Quietly tolerates parse
// failures so a malformed user stylesheet doesn't take the whole
// pipeline down.
// Walk one stylesheet's parsed rules and pull out the :hover rules
// we can apply via the overlay path. We accept only single-compound
// selectors of the form `tag:hover` / `.cls:hover` / `#id:hover`
// today; anything richer (descendant combinators, multiple class
// joins, functional pseudos) is silently skipped and lands in the
// Phase 4 polish pass.
void scan_hover_rules(lxb_css_stylesheet_t* sst,
                      std::vector<HoverRule>& out) {
    if (!sst || !sst->root) return;
    auto* rule_list = lxb_css_rule_list(sst->root);
    for (auto* r = rule_list->first; r != nullptr; r = r->next) {
        if (r->type != LXB_CSS_RULE_STYLE) continue;
        auto* style = lxb_css_rule_style(r);

        // selector is a comma-separated chain of compound chains.
        for (auto* sl = style->selector; sl != nullptr; sl = sl->next) {
            const lxb_css_selector_t* identifier = nullptr;
            bool has_hover = false;
            bool ok        = true;

            for (auto* sel = sl->first; sel != nullptr; sel = sel->next) {
                // Multi-compound (descendant/child/sibling combinator
                // anywhere past the first simple) — out of MVP scope.
                if (sel != sl->first &&
                    sel->combinator != LXB_CSS_SELECTOR_COMBINATOR_CLOSE) {
                    ok = false; break;
                }

                if (sel->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS) {
                    if (sel->u.pseudo.type ==
                        LXB_CSS_SELECTOR_PSEUDO_CLASS_HOVER) {
                        has_hover = true;
                    } else {
                        ok = false; break;  // some other pseudo — skip rule
                    }
                } else if (sel->type == LXB_CSS_SELECTOR_TYPE_ELEMENT ||
                           sel->type == LXB_CSS_SELECTOR_TYPE_CLASS   ||
                           sel->type == LXB_CSS_SELECTOR_TYPE_ID) {
                    // Two identifiers in one compound (e.g. `.a.b:hover`)
                    // — MVP intentionally drops it. Phase 4 widens.
                    if (identifier) { ok = false; break; }
                    identifier = sel;
                } else {
                    ok = false; break;
                }
            }

            if (!ok || !has_hover || !identifier) continue;

            HoverRule hr;
            switch (identifier->type) {
                case LXB_CSS_SELECTOR_TYPE_ELEMENT: hr.kind = HoverRule::Kind::Tag;   break;
                case LXB_CSS_SELECTOR_TYPE_CLASS:   hr.kind = HoverRule::Kind::Class; break;
                case LXB_CSS_SELECTOR_TYPE_ID:      hr.kind = HoverRule::Kind::Id;    break;
                default: continue;
            }
            hr.name.assign(
                reinterpret_cast<const char*>(identifier->name.data),
                identifier->name.length);
            hr.decls = style->declarations;
            out.push_back(std::move(hr));
        }
    }
}

void attach_stylesheet(detail::DocumentImpl& impl, std::string_view css) {
    if (css.empty()) return;
    // Parse via the document's own CSS parser (pre-wired with the
    // document's memory pool + selectors engine). Parsing through a
    // standalone parser allocates rules in a foreign pool that the
    // document's ev_destroy hook can't safely tear down.
    auto* sst = lxb_css_stylesheet_parse(
        impl.doc->css.parser,
        reinterpret_cast<const lxb_char_t*>(css.data()),
        css.size());
    if (!sst) return;
    if (lxb_html_document_stylesheet_attach(impl.doc, sst) == LXB_STATUS_OK) {
        impl.sheets.push_back(sst);
        scan_hover_rules(sst, impl.hover_rules);
    } else {
        lxb_css_stylesheet_destroy(sst, true);
    }
}

// Inline-style scan helper. Lexbor 2.4 doesn't expose every CSS
// property type (`border-radius`, `gap`, etc.), so for those we
// extract the value from the element's `style="..."` attribute via
// a stupid substring scan. Returns -1 when the property isn't
// present. Handles `<key>: <n><unit>?` and nothing more — no calc,
// no var, no shorthand expansion.
int scan_inline_px_property(lxb_dom_element_t* elem,
                            std::string_view key) {
    size_t len = 0;
    const lxb_char_t* attr = lxb_dom_element_get_attribute(
        elem,
        reinterpret_cast<const lxb_char_t*>("style"), 5,
        &len);
    if (!attr || len == 0) return -1;
    std::string_view s(reinterpret_cast<const char*>(attr), len);
    const auto pos = s.find(key);
    if (pos == std::string_view::npos) return -1;
    auto rest = s.substr(pos + key.size());
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
    if (rest.empty() || rest.front() != ':') return -1;
    rest.remove_prefix(1);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
    int  value = 0;
    bool any   = false;
    while (!rest.empty() && rest.front() >= '0' && rest.front() <= '9') {
        value = value * 10 + (rest.front() - '0');
        rest.remove_prefix(1);
        any = true;
    }
    return any ? value : -1;
}

// True if the inline style declares display:flex. Lexbor's cascade
// walk does pick up inline `style="display: flex"`, but this is a
// useful escape hatch for inline-only demos.
inline int parse_border_radius_inline(lxb_dom_element_t* elem) {
    return scan_inline_px_property(elem, "border-radius");
}
inline int parse_gap_inline(lxb_dom_element_t* elem) {
    return scan_inline_px_property(elem, "gap");
}

// Keyword-value variant of the inline-style scanner. Returns the
// identifier text after `<key>:` (e.g. "pointer" for `cursor:
// pointer`). Stops at the first ';' or end of attribute. Used for
// properties whose values are CSS keywords rather than lengths.
std::string scan_inline_keyword(lxb_dom_element_t* elem, std::string_view key) {
    size_t len = 0;
    const lxb_char_t* attr = lxb_dom_element_get_attribute(
        elem,
        reinterpret_cast<const lxb_char_t*>("style"), 5,
        &len);
    if (!attr || len == 0) return {};
    std::string_view s(reinterpret_cast<const char*>(attr), len);
    const auto pos = s.find(key);
    if (pos == std::string_view::npos) return {};
    auto rest = s.substr(pos + key.size());
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
    if (rest.empty() || rest.front() != ':') return {};
    rest.remove_prefix(1);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.remove_prefix(1);
    std::size_t end = 0;
    while (end < rest.size()
           && rest[end] != ';' && rest[end] != ' '
           && rest[end] != '\t' && rest[end] != '\n') {
        ++end;
    }
    return std::string(rest.substr(0, end));
}

// Map a `cursor` keyword onto our enum. Unknown values → Default.
detail::ComputedStyle::Cursor parse_cursor_keyword(std::string_view kw) {
    using C = detail::ComputedStyle::Cursor;
    if (kw == "pointer")     return C::Pointer;
    if (kw == "text")        return C::Text;
    if (kw == "crosshair")   return C::Crosshair;
    if (kw == "move")        return C::Move;
    if (kw == "not-allowed") return C::NotAllowed;
    if (kw == "ew-resize" || kw == "col-resize") return C::ResizeEW;
    if (kw == "ns-resize" || kw == "row-resize") return C::ResizeNS;
    return C::Default;
}

// Recursive DFS collector. Walks the DOM, creates one Block per
// block-level element, links it to its parent. Inline elements
// (e.g. <b>, <span>) do NOT become blocks; their text is absorbed
// into the containing leaf block via node_text().
//
// A block is treated as a *leaf* (gets text) when no descendant
// block-level element exists below it. Containers (a <div> wrapping
// other blocks) have empty text and just carry styling.
void collect_blocks(detail::DocumentImpl& impl,
                    lxb_dom_node_t* node,
                    const detail::ResolvedStyle& parent_style,
                    int parent_idx) {
    for (auto* child = lxb_dom_node_first_child(node); child;
         child = lxb_dom_node_next(child)) {
        if (child->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        auto* elem = lxb_dom_interface_element(child);
        std::string tag = tag_name(elem);

        if (tag == "head" || tag == "script" || tag == "style" ||
            tag == "meta" || tag == "link"   || tag == "title")
            continue;

        if (!is_block_tag(tag)) {
            // Inline content — its text is absorbed by the enclosing
            // leaf block's node_text() call. Don't recurse for now;
            // real inline layout is a Phase 2D follow-up.
            continue;
        }

        // Resolve this element's style under the parent's resolved
        // style (so inheritance flows correctly down the tree).
        const auto id = impl.style_store.acquire(elem);
        auto rs = impl.resolver->resolve(elem, parent_style);

        // :hover overlay — at collect time, the bit is preserved from
        // any previous hover state (state_bits survives reset/acquire).
        // dispatch() will re-resolve affected blocks when the hover
        // chain changes, so collect-time work is the steady-state path.
        if (impl.style_store.state_bits(id) & kHoverStateBit) {
            const auto match = [&](const HoverRule& rule) {
                switch (rule.kind) {
                    case HoverRule::Kind::Tag:   return rule.name == tag;
                    case HoverRule::Kind::Id:    return rule.name ==
                        attr_string(elem, "id");
                    case HoverRule::Kind::Class: {
                        const auto cls_attr = split_classes(
                            attr_string(elem, "class"));
                        return std::find(cls_attr.begin(), cls_attr.end(),
                                         rule.name) != cls_attr.end();
                    }
                }
                return false;
            };
            for (const auto& hr : impl.hover_rules) {
                if (match(hr)) impl.resolver->apply_decl_list(hr.decls, rs);
            }
        }

        impl.style_store.computed(id) = rs.computed;
        impl.style_store.animated(id) = rs.animated;

        // Inline scans for properties lexbor 2.4 doesn't expose via
        // the CSS module. Lives here next to where the cascade
        // results are committed.
        if (const int r = parse_border_radius_inline(elem); r >= 0) {
            impl.style_store.computed(id).border_radius_px =
                static_cast<std::int16_t>(r);
        }
        if (const int g = parse_gap_inline(elem); g >= 0) {
            impl.style_store.computed(id).row_gap    = static_cast<std::int16_t>(g);
            impl.style_store.computed(id).column_gap = static_cast<std::int16_t>(g);
        }
        if (auto kw = scan_inline_keyword(elem, "cursor"); !kw.empty()) {
            impl.style_store.computed(id).cursor = parse_cursor_keyword(kw);
        }
        impl.style_store.dirty(id) &=
            static_cast<std::uint8_t>(~detail::StyleStore::DirtyStyle);

        const int my_idx = static_cast<int>(impl.blocks.size());
        Block b;
        b.id         = id;
        b.tag        = std::move(tag);
        b.elem_id    = attr_string(elem, "id");
        b.classes    = split_classes(attr_string(elem, "class"));
        b.parent_idx = parent_idx;
        impl.blocks.push_back(std::move(b));

        // Recurse — children get my_idx as their parent. Track whether
        // any blocks were appended; if not, this block is a leaf and
        // gets the concatenated descendant text.
        const std::size_t before = impl.blocks.size();
        collect_blocks(impl, child, rs, my_idx);
        const bool is_leaf = (impl.blocks.size() == before);
        if (is_leaf) {
            impl.blocks[static_cast<std::size_t>(my_idx)].text = node_text(child);
        }
    }
}

#endif  // !AFFINEUI_STUB_BUILD

}  // namespace

Document::Document() : impl_{std::make_unique<detail::DocumentImpl>()} {}
Document::~Document() = default;

Document::Document(Document&&) noexcept            = default;
Document& Document::operator=(Document&&) noexcept = default;

void Document::set_html(std::string_view html) {
    impl_->html.assign(html);
    impl_->blocks.clear();
    impl_->style_store.reset();
    impl_->paint_dirty = true;
    impl_->content_size = Size{0, 0};

#if !defined(AFFINEUI_STUB_BUILD)
    // Tear down the previous document; its CSS pool owns the
    // attached stylesheets, so destroying doc tears them down too.
    impl_->resolver.reset();
    impl_->sheets.clear();
    impl_->hover_rules.clear();
    impl_->hovered_chain.clear();
    if (impl_->doc) {
        lxb_html_document_destroy(impl_->doc);
        impl_->doc = nullptr;
    }

    impl_->doc = lxb_html_document_create();
    if (!impl_->doc) return;

    // Initialise CSS subsystems on the document BEFORE parsing HTML so
    // any <style>/style="..." inline declarations are kept.
    if (lxb_html_document_css_init(impl_->doc) != LXB_STATUS_OK) {
        return;
    }

    if (lxb_html_document_parse(
            impl_->doc,
            reinterpret_cast<const lxb_char_t*>(impl_->html.data()),
            impl_->html.size()) != LXB_STATUS_OK) {
        return;
    }

    // Cascade order (lower → higher specificity, ties to last):
    //   1. User-agent baseline
    //   2. Author <style> blocks from the page
    //   3. User stylesheet (App-supplied, often a theme override)
    attach_stylesheet(*impl_, theme::ua_default());

    std::string author_css;
    collect_style_text(lxb_dom_interface_node(impl_->doc), author_css);
    attach_stylesheet(*impl_, author_css);

    attach_stylesheet(*impl_, impl_->user_stylesheet);

    // Resolver runs against the now fully-cascade-attached document.
    impl_->resolver = detail::make_lexbor_resolver(impl_->doc);

    // Establish a root inheritance baseline. Reasonable initial values
    // for the implicit document root — anything not overridden by CSS
    // gets these. AnimatedStyle's foreground defaults to near-white
    // (#dcdce6) so unstyled docs are readable on the dark clear color.
    impl_->root_style                       = detail::ResolvedStyle{};
    impl_->root_style.animated.color_rgba   = 0xDCDCE6FFu;
    impl_->root_style.computed.font_size_px = 16;
    impl_->root_style.computed.font_weight  = 400;

    // Use <body>'s resolved style as the parent for all blocks so
    // body-level CSS (e.g. `body { color: ... }`) inherits down.
    auto* body = lxb_html_document_body_element(impl_->doc);
    detail::ResolvedStyle body_style = impl_->root_style;
    if (body) {
        body_style = impl_->resolver->resolve(
            lxb_dom_interface_element(lxb_dom_interface_node(body)),
            impl_->root_style);
    }
    collect_blocks(*impl_,
                   body ? lxb_dom_interface_node(body)
                        : lxb_dom_interface_node(impl_->doc),
                   body_style,
                   /*parent_idx=*/-1);
#endif
}

void Document::set_user_stylesheet(std::string_view css) {
    impl_->user_stylesheet.assign(css);
    // Re-cascade on next set_html. Live mutation of the attached
    // stylesheet without a full re-parse is a Phase 2E hot-reload
    // refinement.
    impl_->paint_dirty = true;
}

void Document::reload_stylesheets() {
    if (!impl_->html.empty()) set_html(impl_->html);
}

void Document::layout(int viewport_width, Painter* measurer) {
    // Layout delegates to Yoga via src/layout/yoga_adapter. Text
    // leaves get a Yoga measure callback that calls nvgTextBoxBounds
    // — Yoga asks "given width W, what height?" and we return the
    // *actually rendered* wrapped bbox. No metric heuristics; the
    // top/bottom padding ends up symmetric for free because the
    // content area matches what the painter will draw into.
    constexpr int kPagePadding = 24;

    std::vector<detail::BlockLayoutInput> inputs;
    inputs.reserve(impl_->blocks.size());
    for (auto& b : impl_->blocks) {
        const auto& cs = impl_->style_store.computed(b.id);
        detail::BlockLayoutInput in{};
        in.style          = &cs;
        in.parent_idx     = b.parent_idx;
        in.intrinsic_w_px = 0;  // let parent stretch on cross axis

        // Container blocks (no direct text — wrap child blocks) leave
        // intrinsic_h at 0 so Yoga sizes them from their children's
        // resolved heights + their own padding/border.
        if (b.text.empty()) {
            in.intrinsic_h_px = 0;
            inputs.push_back(in);
            continue;
        }

        // Text-bearing leaf. Hand it the text + a font handle; the
        // adapter wires a Yoga measure callback that runs the live
        // painter's nvgTextBoxBounds per constraint width.
        if (measurer != nullptr) {
            in.font = measurer->resolve_font(
                "sans-serif", cs.font_size_px, cs.font_weight, /*italic=*/false);
            in.text = b.text;
            // Leave intrinsic_h_px = 0 — the measure callback supplies
            // the height instead.
        } else {
            in.intrinsic_h_px = cs.font_size_px;
        }
        inputs.push_back(in);
    }

    std::vector<Rect> out(impl_->blocks.size());
    // Yoga's root has no per-block padding of its own. We bake the
    // page gutter in by shrinking the viewport handed to Yoga and
    // shifting frames back out below. Cleaner: a real root node with
    // padding set; Phase 2D once we have a real Box tree.
    const int inner_w = viewport_width - 2 * kPagePadding;
    detail::layout_blocks_with_yoga(inner_w, inputs, out, measurer);

    int max_bottom = 0;
    for (std::size_t i = 0; i < impl_->blocks.size(); ++i) {
        out[i].x += kPagePadding;
        out[i].y += kPagePadding;
        impl_->blocks[i].bounds = out[i];
        const int bottom = out[i].y + out[i].h;
        if (bottom > max_bottom) max_bottom = bottom;
    }
    impl_->content_size = Size{viewport_width, max_bottom + kPagePadding};
}

void Document::draw(Painter& painter) {
    // Document::draw paints through *any* Painter — could be the real
    // NanoVGPainter, could be a DisplayListBuilder that records into
    // a DisplayList. The App layer decides which.
    //
    // This is the "paint" stage of the five-stage pipeline. It walks
    // the box tree, fetches per-element ResolvedStyle from the
    // StyleStore, and emits Painter calls. No GL calls happen here
    // directly — that's the rasterize stage's job.
    for (const auto& b : impl_->blocks) {
        const auto& cs = impl_->style_store.computed(b.id);
        const auto& an = impl_->style_store.animated(b.id);

        const float radius = static_cast<float>(cs.border_radius_px);
        const bool has_bg = (an.background_rgba & 0xFFu) != 0;
        const bool has_border =
            cs.border_style != detail::ComputedStyle::BorderStyle::None
            && (cs.border_top > 0 || cs.border_right > 0
                || cs.border_bottom > 0 || cs.border_left > 0)
            && (an.border_rgba & 0xFFu) != 0;

        // Background fill — common case is transparent (skip op
        // entirely to keep idle display lists short and their hash
        // stable). Uses the rounded variant when border-radius is
        // non-zero so the background matches the border curve.
        if (has_bg) {
            const Color bg = detail::unpack_rgba(an.background_rgba);
            if (radius > 0.0f) {
                painter.fill_rounded_rect(b.bounds, radius, bg);
            } else {
                painter.fill_rect(b.bounds, bg);
            }
        }

        // Uniform border stroke. NanoVG centers strokes on the path
        // edge — to align the outer edge with `bounds`, inset the
        // stroke rect by half the thickness.
        if (has_border) {
            // Phase 2C handles uniform borders only: pick the max
            // declared side width as the stroke thickness.
            int wmax = cs.border_top;
            if (cs.border_right  > wmax) wmax = cs.border_right;
            if (cs.border_bottom > wmax) wmax = cs.border_bottom;
            if (cs.border_left   > wmax) wmax = cs.border_left;
            const float thickness = static_cast<float>(wmax);
            const int   inset     = wmax / 2;
            const Rect  stroke_r{
                b.bounds.x + inset, b.bounds.y + inset,
                b.bounds.w - 2 * inset, b.bounds.h - 2 * inset,
            };
            const Color bc = detail::unpack_rgba(an.border_rgba);
            if (radius > 0.0f) {
                painter.stroke_rounded_rect(stroke_r, radius, bc, thickness);
            } else {
                painter.stroke_rect(stroke_r, bc, thickness);
            }
        }

        if (b.text.empty()) continue;

        const auto font = painter.resolve_font(
            "sans-serif",
            cs.font_size_px,
            cs.font_weight,
            /*italic=*/false);

        // Yoga returns the border-box; CSS text lives inside the
        // content-box, which is border-box minus border-width minus
        // padding on each side. We pass the content-box width as the
        // wrap bound — nvgTextBox breaks lines at exactly that width,
        // matching what nvgTextBoxBounds reported during layout.
        const int text_x = b.bounds.x + cs.border_left + cs.padding_left;
        const int text_y = b.bounds.y + cs.border_top  + cs.padding_top;
        const float content_w = static_cast<float>(
            b.bounds.w - cs.border_left - cs.border_right
                       - cs.padding_left - cs.padding_right);
        painter.draw_text_box(font, Point{text_x, text_y}, b.text,
                              detail::unpack_rgba(an.color_rgba),
                              content_w);
    }
}

namespace {

// True iff (x, y) is inside `r` (half-open: right/bottom edges are
// excluded so adjacent rects don't both match).
inline bool rect_contains(const Rect& r, int x, int y) noexcept {
    return x >= r.x && x < r.x + r.w
        && y >= r.y && y < r.y + r.h;
}

// Deepest block whose border-box contains (x, y), or -1 if none.
// Inputs are in DFS order — parents come before children — so the
// *last* block in the list that contains the point is the deepest
// match. One linear scan suffices.
int hit_test_blocks(const std::vector<Block>& blocks, int x, int y) {
    int hit = -1;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (rect_contains(blocks[i].bounds, x, y)) {
            hit = static_cast<int>(i);
        }
    }
    return hit;
}

}  // namespace

namespace {
#if !defined(AFFINEUI_STUB_BUILD)
// Build the ancestor chain (deepest → root) for the block at `idx`.
// Walks parent_idx, which collect_blocks set up. Empty when idx == -1.
std::vector<int> build_hover_chain(const std::vector<Block>& blocks, int idx) {
    std::vector<int> chain;
    while (idx >= 0) {
        chain.push_back(idx);
        idx = blocks[static_cast<std::size_t>(idx)].parent_idx;
    }
    return chain;
}

// Look up the resolved style of `block_idx`'s parent. Returns the
// document's root style when there is no parent (top-level body).
detail::ResolvedStyle parent_resolved(const detail::DocumentImpl& impl,
                                      int block_idx) {
    const int p = impl.blocks[static_cast<std::size_t>(block_idx)].parent_idx;
    if (p < 0) return impl.root_style;
    const auto pid = impl.blocks[static_cast<std::size_t>(p)].id;
    detail::ResolvedStyle rs;
    rs.computed = impl.style_store.computed(pid);
    rs.animated = impl.style_store.animated(pid);
    return rs;
}

// Re-resolve one block's style (paint-only properties), applying the
// :hover overlay if the bit is set. We don't touch layout: :hover
// affecting layout would require relayout-on-hover, which lands with
// the broader restyle queue in Phase 4. Bounds in the block stay put.
void restyle_block(detail::DocumentImpl& impl, int idx) {
    auto& block = impl.blocks[static_cast<std::size_t>(idx)];
    auto* elem  = impl.style_store.element_of(block.id);
    if (!elem) return;
    auto parent = parent_resolved(impl, idx);
    auto rs     = impl.resolver->resolve(elem, parent);
    if (impl.style_store.state_bits(block.id) & kHoverStateBit) {
        for (const auto& hr : impl.hover_rules) {
            const bool match = (hr.kind == HoverRule::Kind::Tag    && hr.name == block.tag)
                            || (hr.kind == HoverRule::Kind::Id     && hr.name == block.elem_id)
                            || (hr.kind == HoverRule::Kind::Class  &&
                                std::find(block.classes.begin(), block.classes.end(),
                                          hr.name) != block.classes.end());
            if (match) impl.resolver->apply_decl_list(hr.decls, rs);
        }
    }
    impl.style_store.computed(block.id) = rs.computed;
    impl.style_store.animated(block.id) = rs.animated;
    impl.paint_dirty = true;
}

// Update the hover chain in response to a pointer move. Returns true
// when the chain changed (and therefore a repaint is required).
bool refresh_hover_chain(detail::DocumentImpl& impl) {
    auto new_chain = build_hover_chain(impl.blocks, impl.hovered_idx);
    if (new_chain == impl.hovered_chain) return false;

    // Diff: anything in old\new is leaving (clear bit + restyle), any
    // in new\old is entering (set bit + restyle). Small chains, O(n*m)
    // is fine. Sorting + set_diff is the upgrade if chains ever grow.
    const auto in = [](int x, const std::vector<int>& v) {
        return std::find(v.begin(), v.end(), x) != v.end();
    };
    for (int old_idx : impl.hovered_chain) {
        if (in(old_idx, new_chain)) continue;
        const auto id = impl.blocks[static_cast<std::size_t>(old_idx)].id;
        impl.style_store.state_bits(id) &=
            static_cast<std::uint8_t>(~kHoverStateBit);
        restyle_block(impl, old_idx);
    }
    for (int new_idx : new_chain) {
        if (in(new_idx, impl.hovered_chain)) continue;
        const auto id = impl.blocks[static_cast<std::size_t>(new_idx)].id;
        impl.style_store.state_bits(id) |= kHoverStateBit;
        restyle_block(impl, new_idx);
    }
    impl.hovered_chain = std::move(new_chain);
    return true;
}
#else  // stub build — no DOM, no hover bookkeeping
bool refresh_hover_chain(detail::DocumentImpl&) { return false; }
#endif
}  // namespace

DispatchResult Document::dispatch(const Event& ev) {
    DispatchResult result{};
    switch (ev.type) {
        case EventType::MouseMove: {
            impl_->last_mouse_pos = ev.pos;
            const int new_hover = hit_test_blocks(impl_->blocks, ev.pos.x, ev.pos.y);
            if (new_hover != impl_->hovered_idx) {
                impl_->hovered_idx      = new_hover;
                result.redraw_requested = true;
            }
            // Refresh :hover chain even when hovered_idx didn't change —
            // mouse may have moved within the same leaf block (no-op
            // here) or the tree may have churned underneath us (rare,
            // but cheap to verify).
            if (refresh_hover_chain(*impl_)) {
                result.redraw_requested = true;
            }
            break;
        }
        case EventType::MouseDown:
        case EventType::MouseUp:
            // Routing to click handlers lands in the next step. For
            // now: just refresh hover under the click point.
            impl_->last_mouse_pos = ev.pos;
            impl_->hovered_idx    = hit_test_blocks(impl_->blocks, ev.pos.x, ev.pos.y);
            if (refresh_hover_chain(*impl_)) {
                result.redraw_requested = true;
            }
            break;
        default:
            break;
    }
    return result;
}

namespace {
// Walk from the hovered block up the parent chain, returning the
// nearest non-default cursor. CSS-correct: a child without its own
// cursor inherits from its parent. The cascade already does this for
// ComputedStyle::cursor, but the *root* element with no inline
// cursor returns Default — so this walk is mostly belt-and-braces.
detail::ComputedStyle::Cursor effective_cursor(
        const std::vector<Block>& blocks,
        const detail::StyleStore& styles,
        int idx) {
    using C = detail::ComputedStyle::Cursor;
    while (idx >= 0) {
        const auto c = styles.computed(blocks[static_cast<std::size_t>(idx)].id).cursor;
        if (c != C::Default) return c;
        idx = blocks[static_cast<std::size_t>(idx)].parent_idx;
    }
    return C::Default;
}
}  // namespace

/// Cursor the OS should display right now (under the last mouse pos).
/// Lives on the public Document surface so App can poll it once per
/// frame without taking a Painter-style dependency.
int Document::hovered_cursor() const {
    return static_cast<int>(
        effective_cursor(impl_->blocks, impl_->style_store, impl_->hovered_idx));
}

Document::HoverInfo Document::hovered_info() const {
    HoverInfo info{};
    const int idx = impl_->hovered_idx;
    if (idx < 0 || idx >= static_cast<int>(impl_->blocks.size())) return info;
    const auto& b = impl_->blocks[static_cast<std::size_t>(idx)];
    info.valid   = true;
    info.tag     = b.tag;
    info.elem_id = b.elem_id;
    info.classes = b.classes;
    return info;
}

void Document::set_resource_loader(ResourceLoader loader) {
    impl_->resource_loader = std::move(loader);
}

Size Document::content_size() const { return impl_->content_size; }

// ── Immediate mode ──────────────────────────────────────────────────

void Document::set_imm_view(std::function<void()> view_fn) {
    if (!impl_->imm) impl_->imm = std::make_unique<detail::ImmRuntime>();
#if !defined(AFFINEUI_STUB_BUILD)
    if (!impl_->doc) {
        // No DOM yet — establish a minimal empty document so the
        // runtime has a body to mutate. set_html("") goes through the
        // normal parse path and ends with an empty <body>.
        set_html("");
    }
    impl_->imm->bind(this, impl_->doc);
#endif
    impl_->imm->set_view_fn(std::move(view_fn));
}

bool Document::imm_dirty() const {
    return impl_->imm && impl_->imm->dirty() && impl_->imm->has_view_fn();
}

void Document::invalidate_imm() {
    if (impl_->imm) impl_->imm->mark_dirty();
}

void Document::tick_imm() {
    if (!impl_->imm || !impl_->imm->has_view_fn()) return;
    if (!impl_->imm->dirty()) return;

#if !defined(AFFINEUI_STUB_BUILD)
    // 1. Run the view fn — it mutates lexbor's DOM directly via the
    //    runtime, replacing the body's children.
    impl_->imm->run_view_fn();

    // 2. Re-cascade + re-collect. This is the same tail as set_html
    //    after parsing — minus the stylesheet re-attach (those are
    //    still bound to impl_->doc from the original set_html).
    impl_->blocks.clear();
    impl_->style_store.reset();

    detail::ResolvedStyle root_style{};
    root_style.animated.color_rgba   = 0xDCDCE6FFu;
    root_style.computed.font_size_px = 16;
    root_style.computed.font_weight  = 400;

    auto* body = lxb_html_document_body_element(impl_->doc);
    detail::ResolvedStyle body_style = root_style;
    if (body && impl_->resolver) {
        body_style = impl_->resolver->resolve(
            lxb_dom_interface_element(lxb_dom_interface_node(body)),
            root_style);
    }
    collect_blocks(*impl_,
                   body ? lxb_dom_interface_node(body)
                        : lxb_dom_interface_node(impl_->doc),
                   body_style,
                   /*parent_idx=*/-1);

    // 3. Stale-state cleanup. Block indices have churned; hovered_idx
    //    points into the old vector. Reset; next MouseMove restores
    //    a correct hover state.
    impl_->hovered_idx = -1;
    impl_->content_size = Size{0, 0};   // forces relayout in Renderer
#endif
}

bool Document::invoke_imm_click(std::string_view elem_id) {
    return impl_->imm && impl_->imm->invoke_click(elem_id);
}

}  // namespace affineui
