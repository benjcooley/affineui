// Phase-2A cascade: lexbor-backed StyleResolver.
//
// "Start small, but very very very smart."
//
// What this file does:
//   - Implements StyleResolver from internal/style_resolver.h.
//   - Walks lexbor's matched, cascade-ordered declarations per element
//     via lxb_html_element_style_walk.
//   - Routes each declaration into either ComputedStyle (layout-
//     affecting) or AnimatedStyle (paint/composite-only), preserving
//     the split that docs/DESIGN.md § "Real-time render architecture"
//     depends on.
//
// What this file deliberately does NOT do (yet):
//   - Cache ResolvedStyle per element. Phase 2E adds that.
//   - Handle named colors, rgb()/hsl(), em/rem/% lengths. Phase 3.
//   - Touch font_family or font_id. Font registry lands alongside.
//
// Adding a property = one switch arm and one small parser helper.

#include "internal/style_resolver.h"

#include <algorithm>
#include <memory>

#if !defined(AFFINEUI_STUB_BUILD)
#    include <lexbor/css/property.h>
#    include <lexbor/css/value.h>
#    include <lexbor/html/html.h>
#endif

namespace affineui::detail {

#if defined(AFFINEUI_STUB_BUILD)

namespace {
class NullResolver final : public StyleResolver {
public:
    ResolvedStyle resolve(lxb_dom_element_t*, const ResolvedStyle& parent) override {
        return parent;  // pure inherit, no overrides — fine for stub mode
    }
    void invalidate(lxb_dom_element_t*) override {}
    void clear() override {}
};
}  // namespace

std::unique_ptr<StyleResolver> make_lexbor_resolver(lxb_html_document_t*) {
    return std::make_unique<NullResolver>();
}

#else  // !AFFINEUI_STUB_BUILD

namespace {

// Pack an 8-bit RGBA quad in our canonical layout.
inline std::uint32_t make_rgba(std::uint8_t r, std::uint8_t g,
                               std::uint8_t b, std::uint8_t a) {
    return (std::uint32_t(r) << 24)
         | (std::uint32_t(g) << 16)
         | (std::uint32_t(b) <<  8)
         |  std::uint32_t(a);
}

// Hex colors only for Phase 2A. Named/rgb()/hsl() are one switch arm
// each and land alongside their tests.
bool parse_hex_color(const lxb_css_value_color_t* v, std::uint32_t& out) {
    if (!v || v->type != LXB_CSS_COLOR_HEX) return false;
    const auto& h = v->u.hex.rgba;
    const bool has_alpha =
        (v->u.hex.type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_4) ||
        (v->u.hex.type == LXB_CSS_PROPERTY_COLOR_HEX_TYPE_8);
    out = make_rgba(h.r, h.g, h.b, has_alpha ? h.a : 0xFF);
    return true;
}

// Resolve a `<length>` value to integer CSS pixels. Phase 2A handles
// LENGTH-typed values in `px`; em/rem/% follow when the font cascade
// and percent-resolution context are wired in (Phase 2A.1).
//
// CSS lets you write a unit-less `0` for any length value
// (`padding: 6px 0` is valid; the `0` parses as a NUMBER, not a
// LENGTH, but the spec treats it as zero pixels). Without this
// fallback, padding shorthands like `padding: 6px 0` silently lose
// the right/left sides and the mirror logic treats them as "unset"
// — copying the top value into all four sides. Visible bug: items
// got 6px of horizontal padding they didn't ask for.
bool parse_length_px(const lxb_css_value_length_percentage_type_t* v, int& out) {
    if (!v) return false;
    if (v->length.type == LXB_CSS_VALUE__LENGTH) {
        const auto& L = v->length.u.length;
        if (static_cast<int>(L.unit) == static_cast<int>(LXB_CSS_UNIT_PX)) {
            out = static_cast<int>(L.num + 0.5);
            return true;
        }
        return false;
    }
    if (v->length.type == LXB_CSS_VALUE__NUMBER) {
        out = 0;  // unit-less zero is the only valid NUMBER here
        return true;
    }
    return false;
}

bool parse_length_px(const lxb_css_value_length_type_t* v, int& out) {
    if (!v) return false;
    if (v->type == LXB_CSS_VALUE__LENGTH) {
        if (static_cast<int>(v->length.unit) != static_cast<int>(LXB_CSS_UNIT_PX)) return false;
        out = static_cast<int>(v->length.num + 0.5);
        return true;
    }
    if (v->type == LXB_CSS_VALUE__NUMBER) {
        // The lxb_css_value_length_type_t union doesn't expose the
        // number field directly here — but a NUMBER-typed length is
        // always the unit-less zero shorthand per CSS spec. Treat
        // as 0.
        out = 0;
        return true;
    }
    return false;
}

bool parse_length_px(const lxb_css_value_length_percentage_t* v, int& out) {
    if (!v) return false;
    if (v->type == LXB_CSS_VALUE__LENGTH) {
        if (static_cast<int>(v->u.length.unit) != static_cast<int>(LXB_CSS_UNIT_PX)) return false;
        out = static_cast<int>(v->u.length.num + 0.5);
        return true;
    }
    if (v->type == LXB_CSS_VALUE__NUMBER) {
        // Unit-less zero — lexbor reuses the length field's num for
        // the numeric value. CSS spec only allows unit-less zero for
        // length values; treat any value here as the zero-shorthand.
        out = 0;
        return true;
    }
    return false;
}

// Route one declaration into the right struct.
void apply_declaration(const lxb_css_rule_declaration_t* d, ResolvedStyle& s) {
    switch (d->type) {
        // ── Paint-only ─────────────────────────────────────────────
        case LXB_CSS_PROPERTY_COLOR: {
            const auto* v = static_cast<const lxb_css_property_color_t*>(d->u.user);
            std::uint32_t rgba;
            if (parse_hex_color(v, rgba)) s.animated.color_rgba = rgba;
            break;
        }
        case LXB_CSS_PROPERTY_BACKGROUND_COLOR: {
            const auto* v =
                static_cast<const lxb_css_property_background_color_t*>(d->u.user);
            std::uint32_t rgba;
            if (parse_hex_color(v, rgba)) s.animated.background_rgba = rgba;
            break;
        }
        // ── Borders ────────────────────────────────────────────────
        // The four `border-<side>` shorthands and the all-sides
        // `border` shorthand share `lxb_css_property_border_t`
        // (width + style + color). We treat all five as uniform for
        // Phase 2C — per-side variation lands when we split
        // AnimatedStyle / ComputedStyle to hold per-side colors and
        // styles.
        //
        // Gap: lexbor 2.4 does NOT expose `border-radius`. When that
        // changes (or we add our own parser hook), the radius cascade
        // entry lives here.
        case LXB_CSS_PROPERTY_BORDER:
        case LXB_CSS_PROPERTY_BORDER_TOP:
        case LXB_CSS_PROPERTY_BORDER_RIGHT:
        case LXB_CSS_PROPERTY_BORDER_BOTTOM:
        case LXB_CSS_PROPERTY_BORDER_LEFT: {
            const auto* v =
                static_cast<const lxb_css_property_border_t*>(d->u.user);
            // Width — lexbor's `width` is a length value, same type as
            // padding/margin lengths, so parse_length_px applies.
            int px = 0;
            if (parse_length_px(&v->width, px) && px >= 0) {
                const auto w16 = static_cast<std::int16_t>(px);
                if (d->type == LXB_CSS_PROPERTY_BORDER_TOP)    s.computed.border_top    = w16;
                else if (d->type == LXB_CSS_PROPERTY_BORDER_RIGHT)  s.computed.border_right  = w16;
                else if (d->type == LXB_CSS_PROPERTY_BORDER_BOTTOM) s.computed.border_bottom = w16;
                else if (d->type == LXB_CSS_PROPERTY_BORDER_LEFT)   s.computed.border_left   = w16;
                else {
                    s.computed.border_top = s.computed.border_right =
                        s.computed.border_bottom = s.computed.border_left = w16;
                }
            }
            // Style — `border_t::style` is the value-type enum directly
            // (not a nested struct), so compare against the
            // LXB_CSS_BORDER_* constants which alias to LXB_CSS_VALUE_*.
            using BS = ComputedStyle::BorderStyle;
            BS style = BS::None;
            switch (v->style) {
                case LXB_CSS_BORDER_SOLID:  style = BS::Solid;  break;
                case LXB_CSS_BORDER_DASHED: style = BS::Dashed; break;
                case LXB_CSS_BORDER_DOTTED: style = BS::Dotted; break;
                default:                    style = BS::None;   break;
            }
            s.computed.border_style = style;
            // Color — hex only, matching the rest of Phase 2A.
            std::uint32_t rgba;
            if (parse_hex_color(&v->color, rgba)) s.animated.border_rgba = rgba;
            break;
        }
        case LXB_CSS_PROPERTY_BORDER_TOP_COLOR:
        case LXB_CSS_PROPERTY_BORDER_RIGHT_COLOR:
        case LXB_CSS_PROPERTY_BORDER_BOTTOM_COLOR:
        case LXB_CSS_PROPERTY_BORDER_LEFT_COLOR: {
            const auto* v =
                static_cast<const lxb_css_value_color_t*>(d->u.user);
            std::uint32_t rgba;
            if (parse_hex_color(v, rgba)) s.animated.border_rgba = rgba;
            break;
        }
        // ── Box model: padding / margin ────────────────────────────
        // The `padding` and `margin` shorthands deliver a struct with
        // four sides; longhands deliver just the side's length-
        // percentage value. Two cascade arms each. All four parse
        // through the inner-direct overload of parse_length_px.
        case LXB_CSS_PROPERTY_PADDING: {
            // CSS shorthand expansion rules (which lexbor 2.4 does NOT
            // apply for us — it just sets the sides the author wrote
            // and leaves the rest typed as "unset"):
            //   1 value  →  T=R=B=L
            //   2 values →  T=B=v1, R=L=v2
            //   3 values →  T=v1, R=L=v2, B=v3
            //   4 values →  T=v1, R=v2, B=v3, L=v4
            const auto* v =
                static_cast<const lxb_css_property_padding_t*>(d->u.user);
            int t = 0, r = 0, bp = 0, l = 0;
            const bool ok_t = parse_length_px(&v->top,    t);
            const bool ok_r = parse_length_px(&v->right,  r);
            const bool ok_b = parse_length_px(&v->bottom, bp);
            const bool ok_l = parse_length_px(&v->left,   l);

            // Mirror per the shorthand rules: a side that lexbor left
            // unset inherits from its CSS-shorthand peer.
            const int T = ok_t ? t : 0;
            const int R = ok_r ? r : T;
            const int B = ok_b ? bp : T;
            const int L = ok_l ? l : R;
            if (T >= 0) s.computed.padding_top    = static_cast<std::int16_t>(T);
            if (R >= 0) s.computed.padding_right  = static_cast<std::int16_t>(R);
            if (B >= 0) s.computed.padding_bottom = static_cast<std::int16_t>(B);
            if (L >= 0) s.computed.padding_left   = static_cast<std::int16_t>(L);
            break;
        }
        case LXB_CSS_PROPERTY_PADDING_TOP: {
            const auto* v = static_cast<const lxb_css_property_padding_top_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px) && px >= 0)
                s.computed.padding_top = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_PADDING_RIGHT: {
            const auto* v = static_cast<const lxb_css_property_padding_right_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px) && px >= 0)
                s.computed.padding_right = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_PADDING_BOTTOM: {
            const auto* v = static_cast<const lxb_css_property_padding_bottom_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px) && px >= 0)
                s.computed.padding_bottom = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_PADDING_LEFT: {
            const auto* v = static_cast<const lxb_css_property_padding_left_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px) && px >= 0)
                s.computed.padding_left = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_MARGIN: {
            // Same shorthand-mirror dance as padding above. Lexbor 2.4
            // sets only the sides the author wrote; we expand to 4.
            const auto* v =
                static_cast<const lxb_css_property_margin_t*>(d->u.user);
            int t = 0, r = 0, bp = 0, l = 0;
            const bool ok_t = parse_length_px(&v->top,    t);
            const bool ok_r = parse_length_px(&v->right,  r);
            const bool ok_b = parse_length_px(&v->bottom, bp);
            const bool ok_l = parse_length_px(&v->left,   l);
            const int T = ok_t ? t : 0;
            const int R = ok_r ? r : T;
            const int B = ok_b ? bp : T;
            const int L = ok_l ? l : R;
            s.computed.margin_top    = static_cast<std::int16_t>(T);
            s.computed.margin_right  = static_cast<std::int16_t>(R);
            s.computed.margin_bottom = static_cast<std::int16_t>(B);
            s.computed.margin_left   = static_cast<std::int16_t>(L);
            break;
        }
        case LXB_CSS_PROPERTY_MARGIN_TOP: {
            const auto* v = static_cast<const lxb_css_property_margin_top_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px)) s.computed.margin_top = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_MARGIN_RIGHT: {
            const auto* v = static_cast<const lxb_css_property_margin_right_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px)) s.computed.margin_right = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_MARGIN_BOTTOM: {
            const auto* v = static_cast<const lxb_css_property_margin_bottom_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px)) s.computed.margin_bottom = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_MARGIN_LEFT: {
            const auto* v = static_cast<const lxb_css_property_margin_left_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px)) s.computed.margin_left = static_cast<std::int16_t>(px);
            break;
        }
        // ── Flex container properties ──────────────────────────────
        case LXB_CSS_PROPERTY_DISPLAY: {
            const auto* v =
                static_cast<const lxb_css_property_display_t*>(d->u.user);
            // Phase 2D: only the values our engine understands.
            // Unknown values fall through to "block" (the default).
            switch (v->a) {
                case LXB_CSS_DISPLAY_FLEX:
                    s.computed.display = ComputedStyle::Display::Flex; break;
                case LXB_CSS_DISPLAY_BLOCK:
                    s.computed.display = ComputedStyle::Display::Block; break;
                case LXB_CSS_DISPLAY_INLINE:
                    s.computed.display = ComputedStyle::Display::Inline; break;
                case LXB_CSS_DISPLAY_INLINE_BLOCK:
                    s.computed.display = ComputedStyle::Display::InlineBlock; break;
                case LXB_CSS_DISPLAY_NONE:
                    s.computed.display = ComputedStyle::Display::None; break;
                default: break;
            }
            break;
        }
        case LXB_CSS_PROPERTY_FLEX_DIRECTION: {
            const auto* v =
                static_cast<const lxb_css_property_flex_direction_t*>(d->u.user);
            using FD = ComputedStyle::FlexDirection;
            switch (v->type) {
                case LXB_CSS_FLEX_DIRECTION_ROW:            s.computed.flex_direction = FD::Row;           break;
                case LXB_CSS_FLEX_DIRECTION_ROW_REVERSE:    s.computed.flex_direction = FD::RowReverse;    break;
                case LXB_CSS_FLEX_DIRECTION_COLUMN:         s.computed.flex_direction = FD::Column;        break;
                case LXB_CSS_FLEX_DIRECTION_COLUMN_REVERSE: s.computed.flex_direction = FD::ColumnReverse; break;
                default: break;
            }
            break;
        }
        case LXB_CSS_PROPERTY_FLEX_WRAP: {
            const auto* v =
                static_cast<const lxb_css_property_flex_wrap_t*>(d->u.user);
            using FW = ComputedStyle::FlexWrap;
            switch (v->type) {
                case LXB_CSS_FLEX_WRAP_NOWRAP:       s.computed.flex_wrap = FW::NoWrap;      break;
                case LXB_CSS_FLEX_WRAP_WRAP:         s.computed.flex_wrap = FW::Wrap;        break;
                case LXB_CSS_FLEX_WRAP_WRAP_REVERSE: s.computed.flex_wrap = FW::WrapReverse; break;
                default: break;
            }
            break;
        }
        case LXB_CSS_PROPERTY_JUSTIFY_CONTENT: {
            const auto* v =
                static_cast<const lxb_css_property_justify_content_t*>(d->u.user);
            using JC = ComputedStyle::JustifyContent;
            switch (v->type) {
                case LXB_CSS_JUSTIFY_CONTENT_FLEX_START:    s.computed.justify_content = JC::Start;        break;
                case LXB_CSS_JUSTIFY_CONTENT_FLEX_END:      s.computed.justify_content = JC::End;          break;
                case LXB_CSS_JUSTIFY_CONTENT_CENTER:        s.computed.justify_content = JC::Center;       break;
                case LXB_CSS_JUSTIFY_CONTENT_SPACE_BETWEEN: s.computed.justify_content = JC::SpaceBetween; break;
                case LXB_CSS_JUSTIFY_CONTENT_SPACE_AROUND:  s.computed.justify_content = JC::SpaceAround;  break;
                default: break;
            }
            break;
        }
        case LXB_CSS_PROPERTY_ALIGN_ITEMS: {
            const auto* v =
                static_cast<const lxb_css_property_align_items_t*>(d->u.user);
            using AI = ComputedStyle::AlignItems;
            switch (v->type) {
                case LXB_CSS_ALIGN_ITEMS_STRETCH:    s.computed.align_items = AI::Stretch;  break;
                case LXB_CSS_ALIGN_ITEMS_FLEX_START: s.computed.align_items = AI::Start;    break;
                case LXB_CSS_ALIGN_ITEMS_FLEX_END:   s.computed.align_items = AI::End;      break;
                case LXB_CSS_ALIGN_ITEMS_CENTER:     s.computed.align_items = AI::Center;   break;
                case LXB_CSS_ALIGN_ITEMS_BASELINE:   s.computed.align_items = AI::Baseline; break;
                default: break;
            }
            break;
        }

        // ── Flex item properties ───────────────────────────────────
        case LXB_CSS_PROPERTY_FLEX_GROW: {
            const auto* v =
                static_cast<const lxb_css_value_number_type_t*>(d->u.user);
            const auto n = static_cast<int>(v->number.num + 0.5);
            s.computed.flex_grow = static_cast<std::uint8_t>(std::clamp(n, 0, 255));
            break;
        }
        case LXB_CSS_PROPERTY_FLEX_SHRINK: {
            const auto* v =
                static_cast<const lxb_css_value_number_type_t*>(d->u.user);
            const auto n = static_cast<int>(v->number.num + 0.5);
            s.computed.flex_shrink = static_cast<std::uint8_t>(std::clamp(n, 0, 255));
            break;
        }

        // ── Layout-affecting ───────────────────────────────────────
        case LXB_CSS_PROPERTY_FONT_SIZE: {
            const auto* v =
                static_cast<const lxb_css_property_font_size_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px) && px > 0)
                s.computed.font_size_px = static_cast<std::uint16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_LINE_HEIGHT: {
            const auto* v =
                static_cast<const lxb_css_property_line_height_t*>(d->u.user);
            switch (v->type) {
                case LXB_CSS_LINE_HEIGHT_NORMAL:
                    // CSS "normal" is font-dependent (~1.2 typical).
                    // Leave at 0 = unset; paint applies the default.
                    break;
                case LXB_CSS_LINE_HEIGHT__NUMBER: {
                    const auto m = v->u.number.num * 100.0;
                    s.computed.line_height_x100 =
                        static_cast<std::int16_t>(std::clamp(m, 0.0, 32760.0));
                    break;
                }
                case LXB_CSS_LINE_HEIGHT__PERCENTAGE: {
                    // 150% → multiplier 1.5 → x100 = 150.
                    const auto m = v->u.percentage.num;
                    s.computed.line_height_x100 =
                        static_cast<std::int16_t>(std::clamp(m, 0.0, 32760.0));
                    break;
                }
                case LXB_CSS_LINE_HEIGHT__LENGTH: {
                    // line-height: <px>. Stored as a negative
                    // sentinel so the paint side can tell "absolute"
                    // apart from "multiplier."
                    int px = 0;
                    // For length type, the value lives in v->u.length.
                    if (v->u.length.unit == LXB_CSS_UNIT__UNDEF
                        || v->u.length.unit == LXB_CSS_UNIT_PX) {
                        px = static_cast<int>(v->u.length.num + 0.5);
                    }
                    if (px > 0)
                        s.computed.line_height_x100 =
                            static_cast<std::int16_t>(-px);
                    break;
                }
                default: break;
            }
            break;
        }
        case LXB_CSS_PROPERTY_FONT_STYLE: {
            const auto* v =
                static_cast<const lxb_css_property_font_style_t*>(d->u.user);
            switch (v->type) {
                case LXB_CSS_FONT_STYLE_NORMAL:  s.computed.font_style = 0; break;
                case LXB_CSS_FONT_STYLE_ITALIC:  s.computed.font_style = 1; break;
                case LXB_CSS_FONT_STYLE_OBLIQUE: s.computed.font_style = 2; break;
                default: break;
            }
            s.computed.has.font_style = 1;
            break;
        }
        case LXB_CSS_PROPERTY_FONT_WEIGHT: {
            const auto* v =
                static_cast<const lxb_css_property_font_weight_t*>(d->u.user);
            int w = 0;
            switch (v->type) {
                case LXB_CSS_FONT_WEIGHT__NUMBER:
                    w = static_cast<int>(v->number.num + 0.5);
                    break;
                case LXB_CSS_FONT_WEIGHT_NORMAL:  w = 400; break;
                case LXB_CSS_FONT_WEIGHT_BOLD:    w = 700; break;
                case LXB_CSS_FONT_WEIGHT_BOLDER:  w = 700; break;  // approx
                case LXB_CSS_FONT_WEIGHT_LIGHTER: w = 300; break;  // approx
                default: break;
            }
            if (w > 0)
                s.computed.font_weight =
                    static_cast<std::uint16_t>(std::clamp(w, 1, 999));
            s.computed.has.font_weight = 1;
            break;
        }
        case LXB_CSS_PROPERTY_WIDTH: {
            const auto* v =
                static_cast<const lxb_css_property_width_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px) && px >= 0)
                s.computed.width = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_HEIGHT: {
            const auto* v =
                static_cast<const lxb_css_property_height_t*>(d->u.user);
            int px = 0;
            if (parse_length_px(v, px) && px >= 0)
                s.computed.height = static_cast<std::int16_t>(px);
            break;
        }
        case LXB_CSS_PROPERTY_OVERFLOW_Y: {
            const auto* v =
                static_cast<const lxb_css_property_overflow_y_t*>(d->u.user);
            using O = ComputedStyle::Overflow;
            switch (v->type) {
                case LXB_CSS_OVERFLOW_Y_VISIBLE: s.computed.overflow_y = O::Visible; break;
                case LXB_CSS_OVERFLOW_Y_HIDDEN:  s.computed.overflow_y = O::Hidden;  break;
                case LXB_CSS_OVERFLOW_Y_CLIP:    s.computed.overflow_y = O::Clip;    break;
                case LXB_CSS_OVERFLOW_Y_SCROLL:  s.computed.overflow_y = O::Scroll;  break;
                case LXB_CSS_OVERFLOW_Y_AUTO:    s.computed.overflow_y = O::Auto;    break;
                default: break;
            }
            break;
        }
        // Everything else lands when we have a test for it.
        default:
            break;
    }
}

// lxb_html_element_style_walk callback. Lexbor's pre-matched store
// keeps the highest-specificity rule per property as the AVL node's
// "primary" value, with lower-specificity matches chained behind as
// "weak" entries. The walk visits PRIMARY first then WEAK, so if we
// applied every declaration with last-write-wins we'd let the lower-
// specificity rule overwrite the higher one — exactly backwards from
// CSS cascade semantics. Solution: ignore the weak chain entirely.
// The primary entry already encodes the cascade winner for this
// property on this element.
struct WalkCtx { ResolvedStyle* out; };

lxb_status_t walk_callback(lxb_html_element_t* /*element*/,
                           const lxb_css_rule_declaration_t* declr,
                           void* ctx,
                           lxb_css_selector_specificity_t /*spec*/,
                           bool is_weak) {
    if (is_weak) return LXB_STATUS_OK;
    apply_declaration(declr, *static_cast<WalkCtx*>(ctx)->out);
    return LXB_STATUS_OK;
}

class LexborResolver final : public StyleResolver {
public:
    // doc_ is kept (but unused for now) so Phase 2E can attach
    // per-document caching/invalidation hooks without changing the
    // construction shape.
    explicit LexborResolver(lxb_html_document_t* doc) : doc_(doc) { (void)doc_; }

    ResolvedStyle resolve(lxb_dom_element_t* element,
                          const ResolvedStyle& parent) override {
        // Start from the parent's resolved style so inherited
        // properties pre-arrive. The cascade then overrides whatever
        // the matching rules specified for this element.
        //
        // Non-inherited properties (background, margins, paddings)
        // are reset to CSS initial values before the walk — we mask
        // them out of the parent here.
        ResolvedStyle s = parent;
        // Reset non-inherited fields to their initial values.
        s.animated.background_rgba = 0x00000000u;  // transparent
        s.animated.border_rgba     = 0x00000000u;  // transparent
        s.animated.tx = s.animated.ty = 0.0f;
        s.animated.scale_x = s.animated.scale_y = 1.0f;
        s.animated.rotation = 0.0f;
        s.animated.opacity  = 1.0f;
        // ComputedStyle: margins, padding, borders, width/height,
        // display reset to defaults (struct's brace-init values).
        s.computed.margin_top = s.computed.margin_right =
            s.computed.margin_bottom = s.computed.margin_left = 0;
        s.computed.padding_top = s.computed.padding_right =
            s.computed.padding_bottom = s.computed.padding_left = 0;
        s.computed.border_top = s.computed.border_right =
            s.computed.border_bottom = s.computed.border_left = 0;
        s.computed.border_style              = ComputedStyle::BorderStyle::None;
        s.computed.border_radius_top_left_px  = 0;
        s.computed.border_radius_top_right_px = 0;
        s.computed.border_radius_bot_right_px = 0;
        s.computed.border_radius_bot_left_px  = 0;
        // Flex (non-inherited; reset to CSS initial values).
        s.computed.flex_direction   = ComputedStyle::FlexDirection::Row;
        s.computed.flex_wrap        = ComputedStyle::FlexWrap::NoWrap;
        s.computed.justify_content  = ComputedStyle::JustifyContent::Start;
        s.computed.align_items      = ComputedStyle::AlignItems::Stretch;
        s.computed.row_gap          = 0;
        s.computed.column_gap       = 0;
        s.computed.flex_grow        = 0;
        s.computed.flex_shrink      = 1;
        s.computed.flex_basis       = -1;
        // Cursor is technically *inherited* in CSS (an element with
        // no cursor inherits its parent's). But cascade's `s = parent`
        // already does that for us — explicit reset would break the
        // inheritance. Note for the next person looking at this list.
        // (No reset.)
        s.computed.width = s.computed.height = -1;
        s.computed.display = ComputedStyle::Display::Block;
        s.computed.position = ComputedStyle::Position::Static;

        if (!element) return s;

        WalkCtx ctx{&s};
        auto* html_el =
            lxb_html_interface_element(lxb_dom_interface_node(element));
        // with_weak=false: only walk the cascade winner per property.
        // The walk_callback also guards against weak entries for
        // belt-and-braces in case lexbor's contract ever shifts.
        lxb_html_element_style_walk(html_el, walk_callback, &ctx,
                                    /*with_weak=*/false);
        return s;
    }

    void apply_decl_list(const lxb_css_rule_declaration_list_t* list,
                         ResolvedStyle& out) override {
        if (!list) return;
        // Each declaration in the list is an lxb_css_rule_t-derived
        // node; iterate via the base ->next pointer (the list's storage
        // is the same intrusive chain lexbor uses for any rule list).
        for (auto* node = list->first; node != nullptr; node = node->next) {
            apply_declaration(
                reinterpret_cast<const lxb_css_rule_declaration_t*>(node), out);
        }
    }

    void invalidate(lxb_dom_element_t*) override {
        // Phase 2A is uncached — nothing to invalidate.
    }
    void clear() override {}

private:
    lxb_html_document_t* doc_;
};

}  // namespace

std::unique_ptr<StyleResolver> make_lexbor_resolver(lxb_html_document_t* doc) {
    return std::make_unique<LexborResolver>(doc);
}

#endif  // AFFINEUI_STUB_BUILD

}  // namespace affineui::detail
