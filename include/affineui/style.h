#pragma once

// CSS property enums and the `Length` value-type. Public surface — the
// imm-mode API takes Lengths, the inspector tool needs these enums, and
// custom property animators read them.
//
// Memory-layout-sensitive. Every enum here is sized to fit alongside
// the others inside a 256-byte ComputedStyle. If you add a value, check
// the static_asserts in computed_style.h.

#include "affineui/geom.h"
#include "affineui/types.h"

#include <cstdint>

namespace affineui {

// ── Length ──────────────────────────────────────────────────────────
//
// 8 bytes: 4-byte value, 1-byte unit, 3 bytes padding (reserved for
// calc-handle index). The fixed size lets ComputedStyle store many
// Lengths inline without per-property heap allocation.
//
// `auto` and the keyword values (`thin`, `medium`, `thick` etc.) are
// represented as units with a zero numeric value — checking
// `length.is_auto()` is cheaper than checking a special sentinel float.

enum class LengthUnit : std::uint8_t {
    Px,         // CSS px (= 1.0 device px at scale 1.0)
    Em,         // relative to current element's font size
    Rem,        // relative to root element's font size
    Percent,    // relative to containing block (resolved at layout)
    Vw, Vh,     // viewport width / height percent
    Vmin, Vmax, // min/max(Vw, Vh)
    Auto,
    None,       // distinct from Auto: "explicitly absent"
    Calc,       // value is an index into Document's calc table
    Number,     // unitless (line-height, opacity)
};

struct Length {
    float       value{0.0f};
    LengthUnit  unit{LengthUnit::Auto};
    std::uint8_t flags{0};       // reserved
    std::uint16_t calc_id{0};    // valid when unit == Calc

    constexpr Length() = default;
    constexpr Length(float v, LengthUnit u) : value(v), unit(u) {}

    static constexpr Length px(float v)      noexcept { return {v, LengthUnit::Px}; }
    static constexpr Length em(float v)      noexcept { return {v, LengthUnit::Em}; }
    static constexpr Length rem(float v)     noexcept { return {v, LengthUnit::Rem}; }
    static constexpr Length percent(float v) noexcept { return {v, LengthUnit::Percent}; }
    static constexpr Length number(float v)  noexcept { return {v, LengthUnit::Number}; }
    static constexpr Length auto_()          noexcept { return {0.0f, LengthUnit::Auto}; }
    static constexpr Length none()           noexcept { return {0.0f, LengthUnit::None}; }

    constexpr bool is_auto()    const noexcept { return unit == LengthUnit::Auto; }
    constexpr bool is_none()    const noexcept { return unit == LengthUnit::None; }
    constexpr bool is_absolute() const noexcept { return unit == LengthUnit::Px; }
    constexpr bool is_percent() const noexcept { return unit == LengthUnit::Percent; }
};

static_assert(sizeof(Length) == 8, "Length must stay 8 bytes for ComputedStyle layout");

// ── Per-edge Lengths ────────────────────────────────────────────────
//
// 32 bytes. Used for margin / padding / border-width / inset.
struct LengthEdges {
    Length top, right, bottom, left;

    static constexpr LengthEdges zero() noexcept {
        return {Length::px(0), Length::px(0), Length::px(0), Length::px(0)};
    }
    static constexpr LengthEdges auto_() noexcept {
        return {Length::auto_(), Length::auto_(), Length::auto_(), Length::auto_()};
    }
};

static_assert(sizeof(LengthEdges) == 32);

// ── Per-corner Lengths (border-radius) ──────────────────────────────
struct LengthCorners {
    Length tl, tr, br, bl;
    static constexpr LengthCorners zero() noexcept {
        return {Length::px(0), Length::px(0), Length::px(0), Length::px(0)};
    }
};

static_assert(sizeof(LengthCorners) == 32);

// ── Display modes ───────────────────────────────────────────────────
//
// The full CSS `display` shorthand is bigger than what we support. We
// model only the inner/outer combinations that v1 needs. Unsupported
// values (`table-cell`, `grid`, `list-item`) parse to `Block` with a
// warning rather than fail loudly.

enum class Display : std::uint8_t {
    None,
    Block,
    Inline,
    InlineBlock,
    Flex,
    InlineFlex,
    // Phase 3:
    Grid,
    InlineGrid,
};

enum class Position : std::uint8_t {
    Static,
    Relative,
    Absolute,
    Fixed,
    Sticky,
};

enum class Overflow : std::uint8_t {
    Visible,
    Hidden,
    Scroll,
    Auto,
    Clip,
};

enum class Visibility : std::uint8_t { Visible, Hidden, Collapse };

// ── Flex ────────────────────────────────────────────────────────────

enum class FlexDirection : std::uint8_t { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap      : std::uint8_t { NoWrap, Wrap, WrapReverse };

enum class JustifyContent : std::uint8_t {
    Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly, Stretch,
};

enum class AlignItems : std::uint8_t {
    Stretch, Start, End, Center, Baseline,
};

// AlignContent and AlignSelf reuse AlignItems values; semantic-different
// alias.
using AlignContent = AlignItems;
using AlignSelf    = AlignItems;

// ── Borders ─────────────────────────────────────────────────────────

enum class BorderStyle : std::uint8_t {
    None, Hidden, Dotted, Dashed, Solid, Double,
    Groove, Ridge, Inset, Outset,
};

struct BorderStyleEdges {
    BorderStyle top{BorderStyle::None};
    BorderStyle right{BorderStyle::None};
    BorderStyle bottom{BorderStyle::None};
    BorderStyle left{BorderStyle::None};
};

static_assert(sizeof(BorderStyleEdges) == 4);

struct ColorEdges {
    Color top, right, bottom, left;
};

static_assert(sizeof(ColorEdges) == 16);

// ── Text ────────────────────────────────────────────────────────────

enum class TextAlign : std::uint8_t {
    Start, End, Left, Right, Center, Justify,
};

enum class TextDecorationLine : std::uint8_t {
    None       = 0,
    Underline  = 1 << 0,
    Overline   = 1 << 1,
    LineThrough = 1 << 2,
};
constexpr TextDecorationLine operator|(TextDecorationLine a, TextDecorationLine b) noexcept {
    return static_cast<TextDecorationLine>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

enum class FontStyle  : std::uint8_t { Normal, Italic, Oblique };
enum class WhiteSpace : std::uint8_t { Normal, NoWrap, Pre, PreWrap, PreLine, BreakSpaces };
enum class WordBreak  : std::uint8_t { Normal, BreakAll, KeepAll };

// Font weight is a 16-bit number (100..900) per the spec, even though
// most fonts only have 4–5 distinct cuts. We keep it as a value rather
// than an enum so animation between weights is just a lerp.
using FontWeight = std::uint16_t;
constexpr FontWeight FW_THIN       = 100;
constexpr FontWeight FW_LIGHT      = 300;
constexpr FontWeight FW_NORMAL     = 400;
constexpr FontWeight FW_MEDIUM     = 500;
constexpr FontWeight FW_SEMI_BOLD  = 600;
constexpr FontWeight FW_BOLD       = 700;
constexpr FontWeight FW_BLACK      = 900;

// ── Cursor ──────────────────────────────────────────────────────────

enum class Cursor : std::uint8_t {
    Auto, Default, Pointer, Text, Wait, Crosshair, Move,
    NotAllowed, EwResize, NsResize, NeswResize, NwseResize, Grab, Grabbing,
};

// ── will-change flags ───────────────────────────────────────────────
//
// Bitset: which composite-only properties the embedder hinted will
// animate. Used to opt into early layer promotion. See
// docs/OPTIMIZATION.md § 1.2.

enum WillChange : std::uint8_t {
    WC_None      = 0,
    WC_Transform = 1 << 0,
    WC_Opacity   = 1 << 1,
    WC_Filter    = 1 << 2,
    WC_Scroll    = 1 << 3,
};

// ── Property identifiers ────────────────────────────────────────────
//
// One enum value per longhand we support. Used by:
//   • the cascade pass when emitting per-property "winner" tuples
//   • the animation system to identify which property is being lerped
//   • the inspector tool for human-readable property names
//
// Order is not stable — never persist these.

enum class PropertyId : std::uint16_t {
    Unknown = 0,

    // Box model
    Width, Height, MinWidth, MinHeight, MaxWidth, MaxHeight,
    MarginTop, MarginRight, MarginBottom, MarginLeft,
    PaddingTop, PaddingRight, PaddingBottom, PaddingLeft,
    BorderTopWidth, BorderRightWidth, BorderBottomWidth, BorderLeftWidth,
    BorderTopStyle, BorderRightStyle, BorderBottomStyle, BorderLeftStyle,
    BorderTopColor, BorderRightColor, BorderBottomColor, BorderLeftColor,
    BorderTopLeftRadius, BorderTopRightRadius,
    BorderBottomLeftRadius, BorderBottomRightRadius,
    BoxSizing,

    // Display / position
    Display_, Position_, Top, Right, Bottom, Left, ZIndex,
    Overflow_, OverflowX, OverflowY, Visibility_,

    // Flex
    FlexDirection_, FlexWrap_, FlexGrow, FlexShrink, FlexBasis,
    JustifyContent_, AlignItems_, AlignContent_, AlignSelf_, Gap,
    RowGap, ColumnGap, Order_,

    // Visual
    Color_, BackgroundColor, BackgroundImage, BackgroundSize,
    BackgroundPosition, BackgroundRepeat,
    Opacity, Filter, BoxShadow,

    // Text
    FontFamily, FontSize, FontWeight_, FontStyle_, LineHeight,
    TextAlign_, TextDecoration, WhiteSpace_, WordBreak_, LetterSpacing,

    // Transform / animation
    Transform_, TransformOrigin, Transition, Animation,
    WillChange_,

    // Interaction
    Cursor_, PointerEvents,

    // Custom property marker — actual key lives in DOM/CSS string table
    CustomPropertyMark,

    _Count,
};

}  // namespace affineui
