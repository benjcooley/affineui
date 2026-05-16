// Cascade tests — exercises the lexbor-backed StyleResolver directly,
// without involving the window or paint subsystems. These are the
// proof that Phase 2A's "real CSS works" claim is true.

#include <doctest/doctest.h>

#if !defined(AFFINEUI_STUB_BUILD)

#    include "internal/animated_style.h"
#    include "internal/computed_style.h"
#    include "internal/style_resolver.h"

#    include <lexbor/css/css.h>
#    include <lexbor/dom/dom.h>
#    include <lexbor/html/html.h>

#    include <cstring>
#    include <memory>
#    include <string>

namespace {

// Tiny RAII wrapper so test bodies stay readable.
struct CssEnv {
    lxb_html_document_t* doc{nullptr};
    lxb_css_parser_t*    parser{nullptr};
    std::vector<lxb_css_stylesheet_t*> sheets;
    std::unique_ptr<affineui::detail::StyleResolver> resolver;

    explicit CssEnv(std::string_view html) {
        doc = lxb_html_document_create();
        REQUIRE(doc != nullptr);
        REQUIRE(lxb_html_document_css_init(doc) == LXB_STATUS_OK);
        REQUIRE(lxb_html_document_parse(
            doc,
            reinterpret_cast<const lxb_char_t*>(html.data()),
            html.size()) == LXB_STATUS_OK);
        parser = lxb_css_parser_create();
        REQUIRE(lxb_css_parser_init(parser, nullptr) == LXB_STATUS_OK);
    }

    ~CssEnv() {
        resolver.reset();
        for (auto* s : sheets) lxb_css_stylesheet_destroy(s, true);
        if (parser) lxb_css_parser_destroy(parser, true);
        if (doc) lxb_html_document_destroy(doc);
    }

    void attach(std::string_view css) {
        auto* sst = lxb_css_stylesheet_parse(
            parser,
            reinterpret_cast<const lxb_char_t*>(css.data()),
            css.size());
        REQUIRE(sst != nullptr);
        REQUIRE(lxb_html_document_stylesheet_attach(doc, sst) == LXB_STATUS_OK);
        sheets.push_back(sst);
    }

    void build_resolver() {
        resolver = affineui::detail::make_lexbor_resolver(doc);
        REQUIRE(resolver != nullptr);
    }

    // Find the first element with the given tag. Linear scan — fine
    // for these tiny test fixtures.
    lxb_dom_element_t* find(const char* tag) const {
        auto find_in = [&](auto&& self, lxb_dom_node_t* node) -> lxb_dom_element_t* {
            for (auto* c = lxb_dom_node_first_child(node); c;
                 c = lxb_dom_node_next(c)) {
                if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    auto* el = lxb_dom_interface_element(c);
                    size_t len = 0;
                    const auto* name = lxb_dom_element_qualified_name(el, &len);
                    if (name && len == std::strlen(tag) &&
                        std::memcmp(name, tag, len) == 0) {
                        return el;
                    }
                    if (auto* nested = self(self, c)) return nested;
                }
            }
            return nullptr;
        };
        return find_in(find_in, lxb_dom_interface_node(doc));
    }
};

// Helper to construct an RGBA color literal (matches AnimatedStyle's
// packed layout).
constexpr std::uint32_t rgba(std::uint8_t r, std::uint8_t g,
                             std::uint8_t b, std::uint8_t a = 0xFF) {
    return (std::uint32_t(r) << 24) | (std::uint32_t(g) << 16) |
           (std::uint32_t(b) <<  8) |  std::uint32_t(a);
}

}  // namespace

TEST_CASE("a hex color on a type selector reaches the resolved style") {
    CssEnv env("<h1>hi</h1>");
    env.attach("h1 { color: #f38ba8; }");
    env.build_resolver();

    auto* h1 = env.find("h1");
    REQUIRE(h1 != nullptr);

    const affineui::detail::ResolvedStyle parent{};
    const auto rs = env.resolver->resolve(h1, parent);
    CHECK(rs.animated.color_rgba == rgba(0xF3, 0x8B, 0xA8));
}

TEST_CASE("color inherits from parent through the resolver call chain") {
    CssEnv env("<body><h1>hi</h1></body>");
    env.attach("body { color: #cdd6f4; }");
    env.build_resolver();

    auto* body = env.find("body");
    auto* h1   = env.find("h1");
    REQUIRE(body != nullptr);
    REQUIRE(h1   != nullptr);

    const affineui::detail::ResolvedStyle root{};
    const auto body_rs = env.resolver->resolve(body, root);
    const auto h1_rs   = env.resolver->resolve(h1,  body_rs);

    // body's color won
    CHECK(body_rs.animated.color_rgba == rgba(0xCD, 0xD6, 0xF4));
    // h1 has no color of its own → must inherit body's.
    CHECK(h1_rs.animated.color_rgba   == rgba(0xCD, 0xD6, 0xF4));
}

TEST_CASE("a more-specific rule overrides a less-specific one") {
    CssEnv env("<body><h1>hi</h1></body>");
    env.attach("body { color: #cdd6f4; }"
               "h1   { color: #f38ba8; }");
    env.build_resolver();

    auto* body = env.find("body");
    auto* h1   = env.find("h1");
    REQUIRE(h1 != nullptr);

    const affineui::detail::ResolvedStyle root{};
    const auto body_rs = env.resolver->resolve(body, root);
    const auto h1_rs   = env.resolver->resolve(h1,  body_rs);

    CHECK(h1_rs.animated.color_rgba == rgba(0xF3, 0x8B, 0xA8));
}

TEST_CASE("font-size in px lands in ComputedStyle, not AnimatedStyle") {
    CssEnv env("<h1>hi</h1>");
    env.attach("h1 { font-size: 42px; }");
    env.build_resolver();

    auto* h1 = env.find("h1");
    REQUIRE(h1 != nullptr);

    const affineui::detail::ResolvedStyle parent{};
    const auto rs = env.resolver->resolve(h1, parent);
    CHECK(rs.computed.font_size_px == 42);
}

#endif  // !AFFINEUI_STUB_BUILD
