// affineui::Renderer — graphics side of the engine, now driving NanoVG
// through sokol_gfx (see affineui_nanovg/src/nanovg_sokol.h). No raw GL.
//
// The caller (App / the windowing adapter) owns the sokol_gfx frame: it
// calls sg_begin_pass(swapchain) before render() and sg_end_pass()/
// sg_commit() after. render() records the document's paint into a
// display list and replays it through the NanoVG painter, which emits
// sokol_gfx draw calls into the active pass.
//
// Layer/FBO caching + a compositor pass (the old raw-GL composite.cpp /
// layer.cpp) will be reintroduced on top of sokol_gfx render targets in
// a later milestone; for now the document paints directly to the
// swapchain every frame.

#include "affineui/renderer.h"

#include "affineui/document.h"
#include "affineui/painter.h"

#include <cstdio>
#include <memory>
#include <utility>

#if !defined(AFFINEUI_STUB_BUILD)
#    include "internal/display_list_painter.h"
#    include "internal/paint_internal.h"
#    include "sokol_gfx.h"
#    include "sokol_log.h"
#    include "nanovg.h"
#    include "nanovg_sokol.h"
#endif

namespace affineui {

namespace detail {

struct RendererImpl {
    Color clear_color{30, 30, 46, 255};
    bool  ready{false};

#if !defined(AFFINEUI_STUB_BUILD)
    NVGcontext*              vg{nullptr};
    std::unique_ptr<Painter> painter;
    int                      last_w{-1};
    int                      last_h{-1};
    float                    last_dpi{1.0f};
    bool                     first_frame{true};
    bool                     owns_sg{false};  // we called sg_setup (embedded)
    sg_pixel_format          sw_color{SG_PIXELFORMAT_BGRA8};         // embedded swapchain formats
    sg_pixel_format          sw_depth{SG_PIXELFORMAT_DEPTH_STENCIL};
#endif
};

}  // namespace detail

Renderer::Renderer() : impl_(std::make_unique<detail::RendererImpl>()) {}
Renderer::~Renderer() { shutdown(); }
Renderer::Renderer(Renderer&&) noexcept            = default;
Renderer& Renderer::operator=(Renderer&&) noexcept = default;

bool Renderer::ready() const noexcept { return impl_->ready; }

void Renderer::set_clear_color(Color c) { impl_->clear_color = c; }
Color Renderer::clear_color() const     { return impl_->clear_color; }

#if defined(AFFINEUI_STUB_BUILD)

void Renderer::init_gl() {}
void Renderer::init_embedded(const GpuContext&) {}
void Renderer::shutdown() { impl_->ready = false; }
void Renderer::render(Document&, int, int, float) {}
void Renderer::render_to(Document&, const FrameTarget&) {}

#else  // real sokol_gfx path

void Renderer::init_gl() {
    if (impl_->ready) return;

    // Requires sg_setup() to have run and a sokol_gfx render pass to be
    // available; the caller (App::cb_init) guarantees this.
    impl_->vg = nvgCreateSokol(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!impl_->vg) {
        std::fprintf(stderr, "[affineui] nvgCreateSokol failed\n");
        return;
    }
    // Best-effort default sans-serif. If none of the platform paths
    // exist, text simply won't render — by design we never crash.
    detail::register_default_font(impl_->vg);
    impl_->painter = detail::make_nanovg_painter(impl_->vg);
    impl_->ready = true;
}

namespace {

// Neutral PixelFormat → sokol_gfx. NanoVG's fill/stroke uses the stencil
// buffer, so a depth target without stencil renders incorrectly; the
// default depth maps to DEPTH_STENCIL deliberately.
sg_pixel_format to_sg_format(PixelFormat f, bool is_depth) {
    switch (f) {
        case PixelFormat::rgba8:         return SG_PIXELFORMAT_RGBA8;
        case PixelFormat::bgra8:         return SG_PIXELFORMAT_BGRA8;
        case PixelFormat::depth:         return SG_PIXELFORMAT_DEPTH;
        case PixelFormat::depth_stencil: return SG_PIXELFORMAT_DEPTH_STENCIL;
        case PixelFormat::default_:
        default:
            return is_depth ? SG_PIXELFORMAT_DEPTH_STENCIL : SG_PIXELFORMAT_BGRA8;
    }
}

// The backend AffineUI was compiled against (compile-time selected).
constexpr Backend compiled_backend() {
#if defined(AFFINEUI_BACKEND_D3D11)
    return Backend::d3d11;
#elif defined(AFFINEUI_BACKEND_METAL)
    return Backend::metal;
#elif defined(AFFINEUI_BACKEND_WGPU)
    return Backend::wgpu;
#else
    return Backend::gl;
#endif
}

}  // namespace

void Renderer::init_embedded(const GpuContext& gpu) {
    if (impl_->ready) return;

    if (gpu.backend != compiled_backend()) {
        std::fprintf(stderr,
            "[affineui] init_embedded: GpuContext backend does not match the "
            "backend AffineUI was compiled for; rebuild with the matching "
            "AFFINEUI_BACKEND.\n");
        return;
    }

    sg_environment env{};
    env.defaults.color_format = to_sg_format(gpu.color_format, false);
    env.defaults.depth_format = to_sg_format(gpu.depth_format, true);
    env.defaults.sample_count = gpu.sample_count > 0 ? gpu.sample_count : 1;
    impl_->sw_color = env.defaults.color_format;
    impl_->sw_depth = env.defaults.depth_format;
    // Only the active backend's handles are read by sokol_gfx; copying all
    // is harmless and keeps this branch backend-agnostic.
    env.metal.device         = gpu.metal.device;
    env.d3d11.device         = gpu.d3d11.device;
    env.d3d11.device_context = gpu.d3d11.device_context;
    env.wgpu.device          = gpu.wgpu.device;

    sg_desc d{};
    d.environment = env;
    d.logger.func = slog_func;  // sokol prints validation errors / panics
    sg_setup(&d);
    if (!sg_isvalid()) {
        std::fprintf(stderr, "[affineui] init_embedded: sg_setup failed\n");
        return;
    }
    impl_->owns_sg = true;

    // NanoVG + painter on top of the now-live sokol_gfx context.
    init_gl();
}

void Renderer::render_to(Document& doc, const FrameTarget& t) {
    if (!impl_->ready) {
        // init_embedded() must have run first; without sg_setup we cannot
        // open a pass. Bail rather than crash.
        std::fprintf(stderr,
            "[affineui] render_to: not initialized; call Ui::init() with a "
            "GpuContext before render(FrameTarget).\n");
        return;
    }

    sg_swapchain sc{};
    sc.width        = t.width;
    sc.height       = t.height;
    sc.sample_count = t.sample_count > 0 ? t.sample_count : 1;
    sc.color_format = impl_->sw_color;   // match sglue_swapchain(): set explicitly
    sc.depth_format = impl_->sw_depth;
    sc.metal.current_drawable      = t.metal.current_drawable;
    sc.metal.depth_stencil_texture = t.metal.depth_stencil_texture;
    sc.metal.msaa_color_texture    = t.metal.msaa_color_texture;
    sc.d3d11.render_view           = t.d3d11.render_view;
    sc.d3d11.resolve_view          = t.d3d11.resolve_view;
    sc.d3d11.depth_stencil_view    = t.d3d11.depth_stencil_view;
    sc.wgpu.render_view            = t.wgpu.render_view;
    sc.wgpu.resolve_view           = t.wgpu.resolve_view;
    sc.wgpu.depth_stencil_view     = t.wgpu.depth_stencil_view;
    sc.gl.framebuffer              = t.gl.framebuffer;

    sg_pass pass{};
    if (t.clear) {
        const Color c = impl_->clear_color;
        pass.action.colors[0].load_action  = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value.r = c.r / 255.0f;
        pass.action.colors[0].clear_value.g = c.g / 255.0f;
        pass.action.colors[0].clear_value.b = c.b / 255.0f;
        pass.action.colors[0].clear_value.a = c.a / 255.0f;
    } else {
        pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
    }
    pass.action.depth.load_action   = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value   = 1.0f;
    pass.action.stencil.load_action = SG_LOADACTION_CLEAR;
    pass.action.stencil.clear_value = 0;
    pass.swapchain = sc;

    sg_begin_pass(&pass);
    render(doc, t.width, t.height, t.dpi_scale);
    sg_end_pass();
    sg_commit();
}

void Renderer::shutdown() {
    if (!impl_->ready && !impl_->owns_sg) return;
    impl_->painter.reset();
    if (impl_->vg) {
        nvgDeleteSokol(impl_->vg);
        impl_->vg = nullptr;
    }
    impl_->ready = false;
    if (impl_->owns_sg) {
        sg_shutdown();
        impl_->owns_sg = false;
    }
}

void Renderer::render(Document& doc, int fb_w, int fb_h, float dpi_scale) {
    if (!impl_->ready) {
        init_gl();
        if (!impl_->ready) return;
    }

    // CSS layout works in points; the framebuffer is in pixels.
    const int pt_w = static_cast<int>(static_cast<float>(fb_w) / dpi_scale + 0.5f);
    const int pt_h = static_cast<int>(static_cast<float>(fb_h) / dpi_scale + 0.5f);

    const bool viewport_changed =
        impl_->first_frame || fb_w != impl_->last_w || fb_h != impl_->last_h;
    const bool layout_dirty = doc.content_size().width != pt_w;
    if (viewport_changed || layout_dirty) {
        doc.layout(pt_w, pt_h, impl_->painter.get());
        impl_->last_w      = fb_w;
        impl_->last_h      = fb_h;
        impl_->last_dpi    = dpi_scale;
        impl_->first_frame = false;
    }

    // Record paint into a DisplayList (CPU only; no GPU calls)…
    detail::DisplayListBuilder builder(impl_->painter.get());
    builder.begin_frame(pt_w, pt_h, dpi_scale);
    doc.draw(builder);
    builder.end_frame();

    // …then replay through the NanoVG painter, which emits sokol_gfx
    // draws into the render pass the caller opened. The clear is handled
    // by that pass's load action (App builds it from clear_color).
    impl_->painter->begin_frame(pt_w, pt_h, dpi_scale);
    detail::replay(builder.list(), *impl_->painter);
    impl_->painter->end_frame();
}

#endif  // AFFINEUI_STUB_BUILD

}  // namespace affineui
