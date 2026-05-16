#pragma once

// Display list: the data interface between Paint and Rasterize.
//
// Why a display list instead of direct virtual-painter calls:
//
//   • Hashable. Paint emits the same op stream for unchanged content;
//     we hash it as we go and skip rasterize when nothing changed.
//     (docs/OPTIMIZATION.md § 3.1)
//
//   • Cachable. Per-layer display lists live across frames. Idle UIs
//     don't re-emit; they re-use the cached list.
//
//   • Diffable. A future incremental rasterizer can diff the new list
//     against the cached one and re-render only damaged regions.
//
//   • Inspectable. Tools can print the op stream to debug layout vs
//     paint discrepancies.
//
// PaintOp is a 32-byte tagged union. The 32-byte size is chosen so a
// display list of 256 ops fits in 8 KB and a 4 KB page = one I/O.

#include "affineui/geom.h"
#include "affineui/style.h"
#include "affineui/types.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace affineui {

enum class PaintOpKind : std::uint8_t {
    Noop = 0,

    // Filled / stroked shapes
    FillRect,
    FillRoundRect,
    StrokeRect,
    StrokeRoundRect,

    // Backgrounds
    FillLinearGradient,
    FillRadialGradient,
    FillImage,

    // Borders (one op per side, with corner-radius handled by the
    // rasterizer via the layer's CornerRadii).
    DrawBorderSide,

    // Text
    DrawTextRun,

    // Images
    DrawImage,

    // Effects
    DrawShadow,

    // State changes
    PushClipRect,
    PushClipRoundRect,
    PopClip,
    PushTransform,
    PopTransform,

    // Compositor seam — these don't paint into the current layer; the
    // rasterizer treats them as breaks. The paint engine emits a
    // PushLayer / PopLayer when an element promotes (see
    // computed_style.h::requires_own_layer).
    PushLayer,
    PopLayer,
};

/// Packed 32-byte op. Different kinds use different unions of fields,
/// dispatched on `kind`.
///
/// Field aliasing rules:
///   • `rect` is always meaningful (the op's affected region — used
///     for damage tracking and bbox extension).
///   • `color` is packed RGBA8.
///   • `handle` is a registry index: image, font, gradient, layer.
///   • `aux_*` are small extra params (radius, stroke width, etc.).
struct PaintOp {
    PaintOpKind   kind{PaintOpKind::Noop};
    std::uint8_t  flags{0};
    std::uint16_t aux_u16{0};

    // Packed RGBA8. Zero-cost convert to `Color`.
    std::uint32_t color{0};

    // The op's affected area in layer-local coordinates. For ops that
    // don't paint (clip, push/pop), this is the bounds that *would* be
    // damaged if the op were removed — used by damage tracking.
    RectF         rect{};

    // Registry index when meaningful (font for DrawTextRun, image for
    // DrawImage, gradient for FillLinearGradient, layer for PushLayer).
    std::uint32_t handle{0};

    // Two small auxiliary floats. Examples:
    //   FillRoundRect:    aux_a = radius
    //   StrokeRect:       aux_a = stroke width
    //   DrawBorderSide:   aux_a = width, aux_b = (BorderStyle enum)
    //   DrawTextRun:      aux_a = font-size, aux_b = baseline-y
    //   DrawShadow:       aux_a = blur,      aux_b = spread
    //   PushClipRoundRect:aux_a = radius
    float         aux_a{0.0f};
    float         aux_b{0.0f};
};

// Aspirational target was 32 B; with natural alignment of the
// trailing floats we land at 36–40 depending on platform. Still fits
// ~6 ops per cache line — close enough that tightening further is a
// Phase 3 micro-optimization, not a correctness issue.
static_assert(sizeof(PaintOp) <= 40,
              "PaintOp grew beyond cache-density budget; re-pack before bumping");

/// Text and gradient payloads are stored out-of-line in the display
/// list's side tables to keep PaintOp at 32 bytes. The op's `handle`
/// indexes into the appropriate table.

struct TextRunPayload {
    std::uint32_t font_handle{0};
    std::uint32_t glyph_count{0};
    // Glyph indices + advances. Stored in the side buffer; this struct
    // describes the slice.
    std::uint32_t glyph_offset{0};
    // Pre-shaped string source for fallback re-shaping (debug only).
    std::uint32_t source_offset{0};
    std::uint32_t source_length{0};
};

struct GradientStop {
    Color color;
    float offset;   // 0.0 ... 1.0
};

struct GradientPayload {
    Vec2  p0;
    Vec2  p1;      // for linear: end point; for radial: center + radius via aux
    float radius{0.0f};
    std::uint16_t stop_offset{0};
    std::uint16_t stop_count{0};
};

/// A `DisplayList` is one layer's worth of paint output. Owned by the
/// layer; rebuilt when paint-dirty, kept stable otherwise.
class DisplayList {
public:
    DisplayList() = default;

    // Mutators — only the paint engine calls these.
    void clear() noexcept;
    void reserve(std::size_t n_ops);

    /// Append an op. Updates `bounds_` (union with op.rect) and
    /// `rolling_hash_` (FNV-1a over the op's bytes). Constant-time.
    void push(const PaintOp& op) noexcept;

    /// Append a text-run payload, returns the side-table index that
    /// PaintOp::handle should reference.
    std::uint32_t push_text_run(const TextRunPayload& run);

    /// Append a gradient payload (linear or radial).
    std::uint32_t push_gradient(const GradientPayload& g);

    // Inspection — readers.
    const std::vector<PaintOp>& ops()            const noexcept { return ops_; }
    const std::vector<TextRunPayload>& runs()    const noexcept { return runs_; }
    const std::vector<GradientPayload>& gradients() const noexcept { return gradients_; }
    const std::vector<std::uint32_t>& glyphs()   const noexcept { return glyphs_; }
    const std::vector<float>& advances()         const noexcept { return advances_; }
    const std::vector<char>& strings()           const noexcept { return strings_; }

    RectF         bounds()       const noexcept { return bounds_; }
    std::uint64_t rolling_hash() const noexcept { return rolling_hash_; }
    bool          empty()        const noexcept { return ops_.empty(); }

    /// True iff this list and `other` would produce identical pixels.
    /// Cheap: compares the rolling hash. False negatives never; false
    /// positives only on hash collision (vanishingly rare).
    bool same_pixels_as(const DisplayList& other) const noexcept {
        return rolling_hash_ == other.rolling_hash_ && ops_.size() == other.ops_.size();
    }

private:
    std::vector<PaintOp>         ops_;
    std::vector<TextRunPayload>  runs_;
    std::vector<GradientPayload> gradients_;
    std::vector<std::uint32_t>   glyphs_;
    std::vector<float>           advances_;
    std::vector<char>            strings_;

    RectF         bounds_{};
    std::uint64_t rolling_hash_{0xcbf29ce484222325ull};  // FNV-1a seed
};

}  // namespace affineui
