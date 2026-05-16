// Concrete Painter that draws via NanoVG.
//
// Lowers each affineui::Painter method onto nvg* calls. NanoVG handles
// the GL3 backend, the glyph atlas (via bundled fontstash), and
// stb_image-backed image loading.

#include "affineui/painter.h"
#include "internal/paint_internal.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if !defined(AFFINEUI_STUB_BUILD)
#    include "nanovg.h"
#endif

namespace affineui {

#if defined(AFFINEUI_STUB_BUILD)

// Stub Painter that satisfies the interface so other code links when
// no deps have been fetched yet.
class NullPainter final : public Painter {
public:
    void begin_frame(int, int, float) override {}
    void end_frame() override {}
    void fill_rect(const Rect&, Color) override {}
    void stroke_rect(const Rect&, Color, float) override {}
    void fill_rounded_rect(const Rect&, float, Color) override {}
    void stroke_rounded_rect(const Rect&, float, Color, float) override {}
    std::uint32_t resolve_font(std::string_view, int, int, bool) override { return 0; }
    int           measure_text(std::uint32_t, std::string_view) override { return 0; }
    TextMetrics   text_metrics(std::uint32_t) override { return {}; }
    void draw_text(std::uint32_t, const Point&, std::string_view, Color) override {}
    Size measure_text_box(std::uint32_t, std::string_view, float) override { return {}; }
    void draw_text_box(std::uint32_t, const Point&, std::string_view, Color, float) override {}
    std::uint32_t load_image(std::string_view) override { return 0; }
    Size          image_size(std::uint32_t) override { return {}; }
    void draw_image(std::uint32_t, const Rect&, const Rect&) override {}
    void push_clip(const Rect&) override {}
    void pop_clip() override {}
};

namespace detail {
std::unique_ptr<Painter> make_nanovg_painter(NVGcontext*) {
    return std::make_unique<NullPainter>();
}
std::uint32_t register_font_file(NVGcontext*, const char*, const char*) { return 0; }
std::string_view register_default_font(NVGcontext*) { return {}; }
}  // namespace detail

#else  // !AFFINEUI_STUB_BUILD

namespace {

inline NVGcolor to_nvg(Color c) {
    return nvgRGBA(c.r, c.g, c.b, c.a);
}

class NanoVGPainter final : public Painter {
public:
    explicit NanoVGPainter(NVGcontext* vg) : vg_(vg) {}

    void begin_frame(int width, int height, float dpi_scale) override {
        width_  = width;
        height_ = height;
        nvgBeginFrame(vg_, static_cast<float>(width), static_cast<float>(height), dpi_scale);
    }

    void end_frame() override { nvgEndFrame(vg_); }

    void fill_rect(const Rect& r, Color c) override {
        nvgBeginPath(vg_);
        nvgRect(vg_, static_cast<float>(r.x), static_cast<float>(r.y),
                static_cast<float>(r.w), static_cast<float>(r.h));
        nvgFillColor(vg_, to_nvg(c));
        nvgFill(vg_);
    }

    void stroke_rect(const Rect& r, Color c, float thickness) override {
        nvgBeginPath(vg_);
        nvgRect(vg_, static_cast<float>(r.x), static_cast<float>(r.y),
                static_cast<float>(r.w), static_cast<float>(r.h));
        nvgStrokeColor(vg_, to_nvg(c));
        nvgStrokeWidth(vg_, thickness);
        nvgStroke(vg_);
    }

    void fill_rounded_rect(const Rect& r, float radius, Color c) override {
        nvgBeginPath(vg_);
        nvgRoundedRect(vg_, static_cast<float>(r.x), static_cast<float>(r.y),
                       static_cast<float>(r.w), static_cast<float>(r.h), radius);
        nvgFillColor(vg_, to_nvg(c));
        nvgFill(vg_);
    }

    void stroke_rounded_rect(const Rect& r, float radius, Color c, float w) override {
        nvgBeginPath(vg_);
        nvgRoundedRect(vg_, static_cast<float>(r.x), static_cast<float>(r.y),
                       static_cast<float>(r.w), static_cast<float>(r.h), radius);
        nvgStrokeColor(vg_, to_nvg(c));
        nvgStrokeWidth(vg_, w);
        nvgStroke(vg_);
    }

    std::uint32_t resolve_font(std::string_view family,
                               int               size_px,
                               int               weight,
                               bool /*italic*/) override {
        // NanoVG addresses fonts by face id + a per-frame size. We pack
        // (face_id, size) into a handle so draw_text/measure_text can
        // reconstitute it without another lookup.
        //
        // Weight selection: NanoVG has no "synthetic bold" — every
        // weight is its own face. The CSS spec says weight 500 falls
        // back to Regular when no Medium face is available, but in
        // practice systems ship Regular + Bold only, and the visual
        // result of using Regular for 500 is "too light." We promote
        // anything >= 500 to bold; slightly over-bolds true medium
        // text, but matches what readers expect from a browser that
        // *does* have a Medium variant installed. Phase 4 polish: load
        // an actual Medium face from .ttc collections (Helvetica Neue
        // on macOS, etc.) and use a three-tier ladder.
        const std::string family_str(family);
        const bool        prefer_bold = weight >= 500;

        int face = -1;
        if (prefer_bold) {
            face = nvgFindFont(vg_, (family_str + "-bold").c_str());
            if (face < 0 && bold_face_ >= 0) face = bold_face_;
        }
        if (face < 0) face = nvgFindFont(vg_, family_str.c_str());
        if (face < 0) face = default_face_;
        if (face < 0) return 0;
        const std::uint32_t handle = pack_handle(static_cast<std::uint16_t>(face),
                                                 static_cast<std::uint16_t>(size_px));
        return handle;
    }

    int measure_text(std::uint32_t handle, std::string_view text) override {
        if (handle == 0) return 0;
        apply_handle(handle);
        float bounds[4] = {0, 0, 0, 0};
        const float advance = nvgTextBounds(vg_, 0.0f, 0.0f,
                                            text.data(), text.data() + text.size(),
                                            bounds);
        return static_cast<int>(advance + 0.5f);
    }

    TextMetrics text_metrics(std::uint32_t handle) override {
        TextMetrics m{};
        if (handle == 0) return m;
        apply_handle(handle);
        // NanoVG returns descender as a *negative* value (pixels below
        // baseline). We expose it as positive — the spec contract for
        // TextMetrics is "magnitude in pixels."
        float asc = 0, desc = 0, lh = 0;
        nvgTextMetrics(vg_, &asc, &desc, &lh);
        m.ascender    = asc;
        m.descender   = desc < 0 ? -desc : desc;
        m.line_height = lh;
        return m;
    }

    void draw_text(std::uint32_t   handle,
                   const Point&    pos,
                   std::string_view text,
                   Color           color) override {
        if (handle == 0) return;
        apply_handle(handle);
        nvgFillColor(vg_, to_nvg(color));
        nvgText(vg_, static_cast<float>(pos.x), static_cast<float>(pos.y),
                text.data(), text.data() + text.size());
    }

    Size measure_text_box(std::uint32_t handle,
                          std::string_view text,
                          float max_width) override {
        if (handle == 0 || text.empty()) return {};
        apply_handle(handle);
        // nvgTextBoxBounds returns [xmin, ymin, xmax, ymax] for the
        // rendered text wrapped at `breakRowWidth`. The bounds are in
        // the same local coords as the (x,y) we passed (we pass 0,0).
        //
        // CEIL (not round-to-nearest) on the way out. Yoga sizes the
        // box to whatever we report. At paint time we pass that same
        // width as `breakRowWidth` to nvgTextBox; if measure rounded
        // DOWN by .5, the paint-side wrap would see "actual > width
        // by epsilon" and break the word onto a new line — invisible
        // wrap on what was supposed to be a single-line box.
        float bounds[4] = {0, 0, 0, 0};
        nvgTextBoxBounds(vg_, 0.0f, 0.0f, max_width,
                         text.data(), text.data() + text.size(), bounds);
        return Size{
            static_cast<int>(std::ceil(bounds[2] - bounds[0])),
            static_cast<int>(std::ceil(bounds[3] - bounds[1])),
        };
    }

    void draw_text_box(std::uint32_t handle,
                       const Point&  pos,
                       std::string_view text,
                       Color         color,
                       float         max_width) override {
        if (handle == 0 || text.empty()) return;
        apply_handle(handle);
        nvgFillColor(vg_, to_nvg(color));
        nvgTextBox(vg_,
                   static_cast<float>(pos.x), static_cast<float>(pos.y),
                   max_width,
                   text.data(), text.data() + text.size());
    }

    std::uint32_t load_image(std::string_view url) override {
        const std::string key(url);
        if (auto it = image_cache_.find(key); it != image_cache_.end())
            return static_cast<std::uint32_t>(it->second);
        const int img = nvgCreateImage(vg_, key.c_str(), 0);
        if (img <= 0) return 0;
        image_cache_[key] = img;
        return static_cast<std::uint32_t>(img);
    }

    Size image_size(std::uint32_t image) override {
        if (image == 0) return {};
        int w = 0, h = 0;
        nvgImageSize(vg_, static_cast<int>(image), &w, &h);
        return {w, h};
    }

    void draw_image(std::uint32_t image, const Rect& dst, const Rect& /*src*/) override {
        if (image == 0) return;
        NVGpaint p = nvgImagePattern(vg_,
                                     static_cast<float>(dst.x), static_cast<float>(dst.y),
                                     static_cast<float>(dst.w), static_cast<float>(dst.h),
                                     0.0f, static_cast<int>(image), 1.0f);
        nvgBeginPath(vg_);
        nvgRect(vg_, static_cast<float>(dst.x), static_cast<float>(dst.y),
                static_cast<float>(dst.w), static_cast<float>(dst.h));
        nvgFillPaint(vg_, p);
        nvgFill(vg_);
    }

    void push_clip(const Rect& r) override {
        nvgSave(vg_);
        nvgScissor(vg_, static_cast<float>(r.x), static_cast<float>(r.y),
                   static_cast<float>(r.w), static_cast<float>(r.h));
    }

    void pop_clip() override { nvgRestore(vg_); }

    void set_default_face(int face) { default_face_ = face; }
    void set_bold_face   (int face) { bold_face_    = face; }

private:
    static constexpr std::uint32_t pack_handle(std::uint16_t face, std::uint16_t size) {
        return (static_cast<std::uint32_t>(face) << 16) | static_cast<std::uint32_t>(size);
    }
    void apply_handle(std::uint32_t handle) {
        const int face = static_cast<int>((handle >> 16) & 0xFFFF);
        const int size = static_cast<int>(handle & 0xFFFF);
        nvgFontFaceId(vg_, face);
        nvgFontSize(vg_, static_cast<float>(size));
        // Layout treats Y as the top edge of the text box. NanoVG's
        // default is BASELINE — flip so callers get the intuitive
        // meaning.
        nvgTextAlign(vg_, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    }

    NVGcontext*                                  vg_{nullptr};
    int                                          width_{0};
    int                                          height_{0};
    int                                          default_face_{-1};
    int                                          bold_face_{-1};
    std::unordered_map<std::string, int>         image_cache_;
};

}  // namespace

namespace detail {

std::unique_ptr<Painter> make_nanovg_painter(NVGcontext* vg) {
    auto painter = std::make_unique<NanoVGPainter>(vg);
    // If a default font has been registered with the context, surface
    // it so handles without an explicit family fall back to it.
    int face = nvgFindFont(vg, "sans");
    if (face < 0) face = nvgFindFont(vg, "sans-serif");
    if (face >= 0) painter->set_default_face(face);
    // Same for the bold variant, if register_default_font installed it.
    int bold = nvgFindFont(vg, "sans-bold");
    if (bold < 0) bold = nvgFindFont(vg, "sans-serif-bold");
    if (bold >= 0) painter->set_bold_face(bold);
    return painter;
}

std::uint32_t register_font_file(NVGcontext* vg, const char* family, const char* ttf_path) {
    const int id = nvgCreateFont(vg, family, ttf_path);
    return id >= 0 ? static_cast<std::uint32_t>(id) : 0u;
}

std::string_view register_default_font(NVGcontext* vg) {
    // A font-file candidate. `index` > 0 selects a specific face from
    // a .ttc/.otc font collection (macOS Helvetica.ttc bundles
    // Regular + Bold + Light + obliques as faces 0..5; HelveticaNeue
    // is the same shape). Index 0 == default load via nvgCreateFont.
    struct FontCandidate { const char* path; int index; };

    static constexpr FontCandidate kCandidates[] = {
#if defined(__APPLE__)
        // SFNS is the modern macOS UI font — what `-apple-system`
        // resolves to in a browser. Stroke weight and metrics
        // visibly differ from Helvetica, so prefer it when present.
        {"/System/Library/Fonts/SFNS.ttf",                       0},
        {"/System/Library/Fonts/SFCompact.ttf",                  0},
        {"/System/Library/Fonts/HelveticaNeue.ttc",              0},
        {"/System/Library/Fonts/Helvetica.ttc",                  0},
        {"/System/Library/Fonts/Supplemental/Arial.ttf",         0},
        {"/Library/Fonts/Arial.ttf",                             0},
#elif defined(__linux__)
        {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",       0},
        {"/usr/share/fonts/TTF/DejaVuSans.ttf",                   0},
        {"/usr/share/fonts/liberation/LiberationSans-Regular.ttf", 0},
        {"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", 0},
        {"/usr/share/fonts/noto/NotoSans-Regular.ttf",            0},
#elif defined(_WIN32)
        {"C:/Windows/Fonts/segoeui.ttf",                          0},
        {"C:/Windows/Fonts/arial.ttf",                            0},
        {"C:/Windows/Fonts/calibri.ttf",                          0},
#endif
    };
    static constexpr FontCandidate kBoldCandidates[] = {
#if defined(__APPLE__)
        // HelveticaNeue.ttc face index 1 is the Bold face on macOS;
        // same for Helvetica.ttc. Try them before the standalone TTFs
        // so the bold matches the regular family.
        {"/System/Library/Fonts/HelveticaNeue.ttc",              1},
        {"/System/Library/Fonts/Helvetica.ttc",                  1},
        {"/System/Library/Fonts/Supplemental/Arial Bold.ttf",    0},
        {"/Library/Fonts/Arial Bold.ttf",                        0},
#elif defined(__linux__)
        {"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",  0},
        {"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",              0},
        {"/usr/share/fonts/liberation/LiberationSans-Bold.ttf",   0},
        {"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", 0},
        {"/usr/share/fonts/noto/NotoSans-Bold.ttf",               0},
#elif defined(_WIN32)
        {"C:/Windows/Fonts/segoeuib.ttf",                         0},
        {"C:/Windows/Fonts/arialbd.ttf",                          0},
        {"C:/Windows/Fonts/calibrib.ttf",                         0},
#endif
    };
    auto load_one = [&](const char* name, FontCandidate c) {
        return (c.index == 0)
            ? nvgCreateFont       (vg, name, c.path)
            : nvgCreateFontAtIndex(vg, name, c.path, c.index);
    };
    std::string_view registered_regular;
    for (FontCandidate c : kCandidates) {
        if (load_one("sans", c) >= 0) {
            load_one("sans-serif", c);
            registered_regular = "sans";
            break;
        }
    }
    // Bold is best-effort. If no bold face is on the system, the
    // weight cascade falls back to the regular face for >= 500.
    for (FontCandidate c : kBoldCandidates) {
        if (load_one("sans-bold", c) >= 0) {
            load_one("sans-serif-bold", c);
            break;
        }
    }
    return registered_regular;
}

}  // namespace detail

#endif  // AFFINEUI_STUB_BUILD

}  // namespace affineui
