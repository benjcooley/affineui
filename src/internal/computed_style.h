#pragma once

#include "affineui/types.h"

#include <cstdint>

namespace affineui::detail {

/// Properties that affect layout. Changing one of these triggers a
/// layout re-run. They change rarely (DOM mutation, stylesheet
/// attach) — never per-frame. See docs/DESIGN.md § "Real-time render
/// architecture" for why these are split from AnimatedStyle.
///
/// Target: <= 64 bytes (one cache line). All fields are fixed-size
/// integer types; strings (font-family) resolve to a `font_id` at
/// cascade time so we can keep the struct trivially copyable and
/// pointer-free.
struct ComputedStyle {
    // ── Box model (16 bytes) ──────────────────────────────────────
    std::int16_t margin_top    {0};
    std::int16_t margin_right  {0};
    std::int16_t margin_bottom {0};
    std::int16_t margin_left   {0};
    std::int16_t padding_top   {0};
    std::int16_t padding_right {0};
    std::int16_t padding_bottom{0};
    std::int16_t padding_left  {0};

    // ── Border widths (8 bytes) ───────────────────────────────────
    std::int16_t border_top    {0};
    std::int16_t border_right  {0};
    std::int16_t border_bottom {0};
    std::int16_t border_left   {0};

    // ── Border style + radius (4 bytes) ───────────────────────────
    // Uniform border-style for Phase 2C — per-side styles land when
    // the cascade splits them out. Radius is also uniform (single
    // value applied to all four corners); per-corner CSS
    // (`border-top-left-radius` etc.) is Phase 2D.
    //
    // `border_radius_px` strictly speaking doesn't affect layout —
    // it only changes rasterization — so it could live in
    // AnimatedStyle. Parking it here for now to keep border data
    // grouped; will migrate if border-radius animations become a
    // hot path. See docs/OPTIMIZATION.md § 1.1.
    enum class BorderStyle : std::uint8_t {
        None = 0, Solid = 1, Dashed = 2, Dotted = 3,
    };
    BorderStyle  border_style    {BorderStyle::None};
    std::uint8_t pad_border_     {0};
    std::int16_t border_radius_px{0};

    // ── Layout sizing (10 bytes) ──────────────────────────────────
    std::int16_t width     {-1};  // -1 = auto
    std::int16_t height    {-1};
    std::int16_t min_width {0};
    std::int16_t max_width {-1};
    std::int16_t min_height{0};

    // ── Text layout (6 bytes) ─────────────────────────────────────
    std::uint16_t font_size_px{16};  // resolved px
    std::uint16_t font_weight {400};  // 100..900
    // Line-height as multiplier x 100. 0 = unset (paint uses NVG
    // default of 1.0 today; eventually we'll match browsers' 1.2
    // baseline). Length-typed `line-height: 24px` lands as a
    // negative value: -24 means "absolute 24 px," distinguished
    // from any positive multiplier.
    std::int16_t  line_height_x100{0};

    // ── Display + flex + position (4 bytes) ───────────────────────
    enum class Display : std::uint8_t {
        Block, Inline, InlineBlock, Flex, None,
    };
    enum class Position : std::uint8_t {
        Static, Relative, Absolute, Fixed,
    };
    enum class FlexDirection : std::uint8_t {
        Row, Column, RowReverse, ColumnReverse,
    };

    Display       display       {Display::Block};
    Position      position      {Position::Static};
    FlexDirection flex_direction{FlexDirection::Row};
    std::uint8_t  font_style    {0};  // 0=normal, 1=italic, 2=oblique

    // Cursor enum — drives sapp_set_mouse_cursor when this element is
    // under the pointer. Default = the OS default arrow. Pointer = the
    // hand cursor used for clickable things.
    enum class Cursor : std::uint8_t {
        Default = 0, Pointer, Text, Crosshair, Move,
        NotAllowed, ResizeEW, ResizeNS,
    };
    Cursor cursor{Cursor::Default};

    // CSS `overflow-y`. Determines whether children of this block can
    // be scrolled when content exceeds the box. Visible is the CSS
    // default (children paint outside the parent's bounds, no scroll).
    // Auto + Scroll both make the block scrollable; we don't yet
    // distinguish them visually (no scrollbar when not needed).
    enum class Overflow : std::uint8_t {
        Visible = 0, Hidden, Clip, Scroll, Auto,
    };
    Overflow      overflow_y{Overflow::Visible};
    std::uint16_t pad_cursor2_{0};

    // ── Flex container + item properties (12 bytes) ───────────────
    // CSS flex enums collapsed to our minimum-needed sets. Each maps
    // directly onto a Yoga YGAlign/YGJustify/YGWrap enum in the
    // adapter — keep the values in sync if you reorder.
    enum class JustifyContent : std::uint8_t {
        Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly,
    };
    enum class AlignItems : std::uint8_t {
        Stretch, Start, End, Center, Baseline,
    };
    enum class FlexWrap : std::uint8_t {
        NoWrap, Wrap, WrapReverse,
    };

    JustifyContent justify_content{JustifyContent::Start};
    AlignItems     align_items    {AlignItems::Stretch};
    FlexWrap       flex_wrap      {FlexWrap::NoWrap};
    std::uint8_t   pad_flex_      {0};

    // Gaps. CSS allows length or %; we currently support px integers.
    std::int16_t row_gap   {0};
    std::int16_t column_gap{0};

    // Flex item properties. flex_grow/shrink are byte-sized integers —
    // typical CSS values are 0 or 1, occasionally 2-3. Sub-integer
    // factors round to nearest int. Phase 2D could widen to a real
    // float when fractional factors land in a demo.
    std::uint8_t  flex_grow  {0};
    std::uint8_t  flex_shrink{1};
    std::int16_t  flex_basis {-1};  // -1 = auto, else px

    // ── Font face id (1 byte) ─────────────────────────────────────
    // Resolved at cascade time from font-family. 0 = default (sans).
    // 256 distinct faces per document is plenty.
    std::uint8_t  font_id      {0};
    std::uint8_t  z_index_high {0};  // top 8 bits of i16 z-index
    std::int16_t  z_index_low  {0};  // bottom 16 bits (i16 for sort)

    // ── Inheritance presence bitset (4 bytes, plenty of room) ─────
    // Only inherited properties care about presence; non-inherited
    // properties default to their CSS initial value and never need
    // a "use parent's" path.
    struct InheritedHas {
        std::uint32_t color       : 1 {};
        std::uint32_t font_family : 1 {};
        std::uint32_t font_size   : 1 {};
        std::uint32_t font_weight : 1 {};
        std::uint32_t font_style  : 1 {};
    } has{};

    // Roughly ~66 bytes after flex was added. The original 64-byte
    // "fits in one x86_64 cache line" target survives only as a soft
    // goal — modern Apple Silicon has 128-byte lines anyway, and the
    // hot loops that read this struct read just a few fields per
    // node, not the whole thing. Relaxing the assert to a budget that
    // tells us when we've grown problematically large.
};

static_assert(sizeof(ComputedStyle) <= 80,
              "ComputedStyle exceeded its size budget — re-pack before bumping further");
static_assert(std::is_trivially_copyable_v<ComputedStyle>,
              "ComputedStyle must be trivially copyable");

}  // namespace affineui::detail
