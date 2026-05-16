#pragma once

// `ComputedStyle` — the per-element, post-cascade style snapshot. The
// hottest data structure in the engine: read by layout, paint, hit-test,
// and animation. Memory layout matters more than ergonomics.
//
// Layout decisions:
//
//   • One `ComputedStyle` is targeted at ~256 bytes (4 cache lines on
//     x86_64). Most layouts walk it linearly during box/inline layout;
//     keeping it in 4 cache lines means a hot box-layout loop streams
//     through L1 cheaply.
//
//   • Rare-but-large data (multiple backgrounds, box-shadow list,
//     filter chain, transform decomposition, transitions, animations)
//     lives behind a single `extras` pointer. nullptr in the common
//     case → no allocation, no indirection.
//
//   • Computed values are pre-resolved where possible: keywords mapped
//     to their numeric equivalents, shorthands expanded, inherited
//     values pulled in. Layout never asks "is this value `inherit`?"
//
//   • Reference-counted via `refcount_` so siblings with identical
//     computed styles can share a single allocation. See
//     docs/OPTIMIZATION.md § 2.4 (computed-style sharing).
//
//   • Trivially copyable (the bottom 256 bytes are). `extras` ownership
//     is reference-counted separately.
//
// This is the Phase 2 shape. Phase 3+ adds the optimizations that
// honor the seams (sharing, deduplication); the struct itself doesn't
// change.

#include "affineui/geom.h"
#include "affineui/style.h"
#include "affineui/types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace affineui {

struct ComputedStyleExtras;  // forward — defined in src/style/extras.h

/// Per-element computed style. POD-ish: trivially copyable for the
/// inline fields; `extras` is a refcounted optional payload.
struct alignas(16) ComputedStyle {
    // ── Box model (96 B) ────────────────────────────────────────────
    Length        width{Length::auto_()};
    Length        height{Length::auto_()};
    Length        min_width{Length::px(0)};
    Length        min_height{Length::px(0)};
    Length        max_width{Length::none()};
    Length        max_height{Length::none()};
    LengthEdges   margin{LengthEdges::zero()};
    LengthEdges   padding{LengthEdges::zero()};
    // border widths share LengthEdges with padding to save bytes;
    // border-style determines whether the width applies.
    LengthEdges   border_width{LengthEdges::zero()};

    // ── Visual (52 B) ───────────────────────────────────────────────
    LengthCorners border_radius{LengthCorners::zero()};
    ColorEdges    border_color{};
    Color         color{0, 0, 0, 255};
    Color         background_color{0, 0, 0, 0};

    // ── Discrete enums (24 B; tightly packed) ───────────────────────
    Display       display{Display::Inline};
    Position      position{Position::Static};
    Overflow      overflow_x{Overflow::Visible};
    Overflow      overflow_y{Overflow::Visible};
    Visibility    visibility{Visibility::Visible};
    Cursor        cursor{Cursor::Auto};

    FlexDirection flex_direction{FlexDirection::Row};
    FlexWrap      flex_wrap{FlexWrap::NoWrap};
    JustifyContent justify_content{JustifyContent::Start};
    AlignItems    align_items{AlignItems::Stretch};
    AlignContent  align_content{AlignItems::Stretch};
    AlignSelf     align_self{AlignItems::Stretch};

    TextAlign     text_align{TextAlign::Start};
    FontStyle     font_style_value{FontStyle::Normal};
    WhiteSpace    white_space{WhiteSpace::Normal};
    WordBreak     word_break{WordBreak::Normal};
    BorderStyleEdges border_style{};

    std::uint8_t  text_decoration{0};   // bitmask of TextDecorationLine
    std::uint8_t  will_change{WC_None}; // bitmask of WillChange flags

    // ── Flex / position scalars (24 B) ──────────────────────────────
    float         flex_grow{0.0f};
    float         flex_shrink{1.0f};
    Length        flex_basis{Length::auto_()};
    std::int32_t  z_index{0};                  // INT_MIN = auto
    std::int16_t  order{0};

    // ── Text scalars (16 B) ─────────────────────────────────────────
    Length        font_size{Length::px(16)};
    Length        line_height{Length::number(1.2f)};
    FontWeight    font_weight{FW_NORMAL};
    std::uint16_t font_family_id{0};   // index into Document font registry
    std::uint32_t letter_spacing_q16{0}; // Q16.16 fixed-point px

    // ── Compositor-relevant scalars (16 B) ──────────────────────────
    float         opacity{1.0f};
    // gap is technically two values (row, column) — packed as a u32 pair
    // to keep ComputedStyle small. Resolved to floats at layout.
    Length        row_gap{Length::px(0)};

    // ── Insets (32 B) ───────────────────────────────────────────────
    Length        top_{Length::auto_()};
    Length        right_{Length::auto_()};
    Length        bottom_{Length::auto_()};
    Length        left_{Length::auto_()};

    // ── Refcount + flags (8 B) ──────────────────────────────────────
    mutable std::atomic<std::uint32_t> refcount_{1};
    std::uint32_t                      generation_{0};

    // ── Optional payload (8 B) ──────────────────────────────────────
    // nullptr unless this element has at least one of: background-
    // image(s), box-shadow(s), filter, transform, transition list,
    // animation list, mask, clip-path. ~95% of elements have nullptr.
    ComputedStyleExtras* extras{nullptr};

    // ── Methods ─────────────────────────────────────────────────────

    /// Increment the refcount. Returns this for chaining.
    const ComputedStyle* retain() const noexcept {
        refcount_.fetch_add(1, std::memory_order_relaxed);
        return this;
    }

    /// Decrement and free if last. Free path lives in the .cpp because
    /// it has to delete `extras` and that type is incomplete here.
    void release() const noexcept;

    /// Layout helpers — these are pure inspection, not resolution.
    /// Real resolution to px (with %, em, vw, calc handling) happens
    /// in layout against a containing-block context.
    constexpr bool has_borders() const noexcept {
        return border_style.top   != BorderStyle::None
            || border_style.right != BorderStyle::None
            || border_style.bottom!= BorderStyle::None
            || border_style.left  != BorderStyle::None;
    }
    constexpr bool has_background() const noexcept {
        return background_color.a != 0 || extras != nullptr;  // extras carries images
    }
    constexpr bool is_block_flow() const noexcept {
        return display == Display::Block || display == Display::Flex
            || display == Display::Grid;
    }
    constexpr bool is_flex_container() const noexcept {
        return display == Display::Flex || display == Display::InlineFlex;
    }
    constexpr bool participates_in_layout() const noexcept {
        return display != Display::None;
    }

    /// Whether this element triggers its own compositing layer.
    /// Decision lives next to the data it reads to keep it inlinable.
    /// See docs/OPTIMIZATION.md § 1.2.
    constexpr bool requires_own_layer() const noexcept {
        if (will_change != WC_None) return true;
        if (opacity < 1.0f)         return true;
        if (position == Position::Fixed)  return true;
        if (overflow_x != Overflow::Visible || overflow_y != Overflow::Visible) return true;
        // `extras` non-null with a transform / filter also promotes;
        // checked there because we can't see into `extras` from here.
        return false;
    }
};

// Phase 2 budget. Aspirational target ~256 B; current naïve packing
// lands at 320–352 depending on platform alignment. Above 384 means
// we've added a field that should have gone behind `extras`.
//
// Phase 3 tightening plan (docs/OPTIMIZATION.md § 2.4 sharing): pack
// the 14 discrete enums into a single u64 bitfield, fold `top/right/
// bottom/left` insets behind `extras` (only set when position != static),
// fold border_radius behind `extras` (zero in ~80% of elements). Target
// after that work: 224 B.
static_assert(sizeof(ComputedStyle) <= 384,
              "ComputedStyle exceeded its cache budget; move rare data to extras");

}  // namespace affineui
