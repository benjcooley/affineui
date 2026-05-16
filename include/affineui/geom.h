#pragma once

// Float geometry used by every stage downstream of layout. Public so
// custom rasterizers / hit-testers / inspectors can speak the same
// vocabulary as the engine.
//
// Layout is the *only* stage that can produce non-integer coordinates,
// and only because percentage and `em` resolution can. Once a box is
// laid out, its rect is fixed in float space. Subpixel positioning at
// paint time is allowed but optional.

#include "affineui/types.h"

#include <cmath>
#include <cstdint>

namespace affineui {

struct Vec2 {
    float x{0.0f};
    float y{0.0f};

    constexpr Vec2() = default;
    constexpr Vec2(float xx, float yy) : x(xx), y(yy) {}

    constexpr Vec2 operator+(Vec2 o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const noexcept { return {x * s, y * s}; }
};

struct SizeF {
    float w{0.0f};
    float h{0.0f};
};

/// Axis-aligned rect in (x, y, w, h) form. Stored that way because every
/// downstream consumer (compositor, hit-test, clip stack) wants origin +
/// extent rather than min/max — and storing extents directly avoids one
/// subtract per access in the hot loop.
struct RectF {
    float x{0.0f};
    float y{0.0f};
    float w{0.0f};
    float h{0.0f};

    constexpr float right()  const noexcept { return x + w; }
    constexpr float bottom() const noexcept { return y + h; }
    constexpr Vec2  origin() const noexcept { return {x, y}; }
    constexpr SizeF size()   const noexcept { return {w, h}; }
    constexpr bool  empty()  const noexcept { return w <= 0.0f || h <= 0.0f; }

    constexpr bool contains(Vec2 p) const noexcept {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }

    constexpr RectF inset(float dx, float dy) const noexcept {
        return {x + dx, y + dy, w - 2.0f * dx, h - 2.0f * dy};
    }

    constexpr RectF translate(Vec2 v) const noexcept {
        return {x + v.x, y + v.y, w, h};
    }

    constexpr bool intersects(RectF o) const noexcept {
        return !(o.x >= right() || o.right() <= x ||
                 o.y >= bottom() || o.bottom() <= y);
    }

    static constexpr RectF from_min_max(Vec2 mn, Vec2 mx) noexcept {
        return {mn.x, mn.y, mx.x - mn.x, mx.y - mn.y};
    }
};

/// Per-side amounts. Used for margin / padding / border widths.
struct EdgesF {
    float top{0.0f};
    float right{0.0f};
    float bottom{0.0f};
    float left{0.0f};

    constexpr float horizontal() const noexcept { return left + right; }
    constexpr float vertical()   const noexcept { return top + bottom; }

    constexpr RectF inset(RectF r) const noexcept {
        return {r.x + left, r.y + top,
                r.w - horizontal(), r.h - vertical()};
    }

    constexpr RectF outset(RectF r) const noexcept {
        return {r.x - left, r.y - top,
                r.w + horizontal(), r.h + vertical()};
    }

    static constexpr EdgesF uniform(float v) noexcept { return {v, v, v, v}; }
};

/// Per-corner radii for border-radius. Order matches CSS shorthand:
/// top-left, top-right, bottom-right, bottom-left.
struct CornerRadii {
    float tl{0.0f};
    float tr{0.0f};
    float br{0.0f};
    float bl{0.0f};

    constexpr bool is_zero() const noexcept {
        return tl == 0.0f && tr == 0.0f && br == 0.0f && bl == 0.0f;
    }
    constexpr bool is_uniform() const noexcept {
        return tl == tr && tr == br && br == bl;
    }
    static constexpr CornerRadii uniform(float v) noexcept { return {v, v, v, v}; }
};

/// 2D affine transform, row-major:
///   | a c tx |
///   | b d ty |
///   | 0 0  1 |
///
/// Stored as 6 floats (no third row). The natural shape for CSS
/// transforms — `translate`, `rotate`, `scale`, `skew`, `matrix()` —
/// and what the compositor uploads as a single uniform per layer.
struct Mat2x3 {
    float a{1.0f}, b{0.0f};
    float c{0.0f}, d{1.0f};
    float tx{0.0f}, ty{0.0f};

    static constexpr Mat2x3 identity() noexcept { return {}; }

    static constexpr Mat2x3 translate(float x, float y) noexcept {
        return {1.0f, 0.0f, 0.0f, 1.0f, x, y};
    }

    static constexpr Mat2x3 scale(float sx, float sy) noexcept {
        return {sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
    }

    static Mat2x3 rotate(float radians) noexcept {
        const float s = std::sin(radians);
        const float ct = std::cos(radians);
        return {ct, s, -s, ct, 0.0f, 0.0f};
    }

    constexpr bool is_identity() const noexcept {
        return a == 1.0f && b == 0.0f && c == 0.0f && d == 1.0f &&
               tx == 0.0f && ty == 0.0f;
    }

    constexpr Vec2 apply(Vec2 p) const noexcept {
        return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
    }

    constexpr Mat2x3 then(Mat2x3 o) const noexcept {
        return {
            o.a * a + o.c * b,
            o.b * a + o.d * b,
            o.a * c + o.c * d,
            o.b * c + o.d * d,
            o.a * tx + o.c * ty + o.tx,
            o.b * tx + o.d * ty + o.ty,
        };
    }
};

/// Lossless conversion to the public integer Rect (rounds to nearest).
inline Rect to_int(RectF r) noexcept {
    auto round = [](float v) { return static_cast<int>(v + (v >= 0 ? 0.5f : -0.5f)); };
    return {round(r.x), round(r.y), round(r.w), round(r.h)};
}

inline RectF to_float(Rect r) noexcept {
    return {static_cast<float>(r.x), static_cast<float>(r.y),
            static_cast<float>(r.w), static_cast<float>(r.h)};
}

}  // namespace affineui
