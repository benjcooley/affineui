#pragma once

#include "affineui/types.h"

#include <cstdint>

namespace affineui::detail {

/// Properties that only affect paint (not layout). Changing one of
/// these never triggers layout. They change frequently — animations,
/// hover transitions, per-frame state updates.
///
/// Target: <= 32 bytes (half a cache line). One AnimatedStyle per
/// element lives in a tight array; the animation engine writes here
/// directly and the cascade is not invoked.
///
/// See docs/DESIGN.md § "Real-time render architecture" for the
/// rationale.
struct AnimatedStyle {
    // ── Colors (12 bytes) ─────────────────────────────────────────
    // Packed as RGBA8 (one u32 each) for compact storage and trivial
    // GPU-uniform handoff. Color (the type) unpacks at use-time.
    std::uint32_t color_rgba           {0xFFFFFFFFu};  // foreground
    std::uint32_t background_rgba      {0x00000000u};
    std::uint32_t border_rgba          {0x00000000u};  // uniform border color

    // ── Transform (20 bytes) ──────────────────────────────────────
    // 2D affine: translate + scale + rotation. The full 3x2 matrix
    // can be reconstructed for composite-shader handoff.
    float tx       {0.0f};
    float ty       {0.0f};
    float scale_x  {1.0f};
    float scale_y  {1.0f};
    float rotation {0.0f};  // radians

    // ── Compositor (4 bytes) ──────────────────────────────────────
    float opacity{1.0f};

    // Total: 12 + 20 + 4 = 36 bytes. The original 32-byte aspiration
    // is gone now that borders carry a color; per-side border colors
    // would push us to 48. Still well inside one cache line.
};

static_assert(sizeof(AnimatedStyle) <= 48,
              "AnimatedStyle should stay inside one cache line");
static_assert(std::is_trivially_copyable_v<AnimatedStyle>,
              "AnimatedStyle must be trivially copyable");

inline std::uint32_t pack_rgba(Color c) noexcept {
    return (std::uint32_t(c.r) << 24)
         | (std::uint32_t(c.g) << 16)
         | (std::uint32_t(c.b) <<  8)
         |  std::uint32_t(c.a);
}

inline Color unpack_rgba(std::uint32_t v) noexcept {
    return Color{
        static_cast<std::uint8_t>((v >> 24) & 0xFF),
        static_cast<std::uint8_t>((v >> 16) & 0xFF),
        static_cast<std::uint8_t>((v >>  8) & 0xFF),
        static_cast<std::uint8_t>( v        & 0xFF),
    };
}

}  // namespace affineui::detail
