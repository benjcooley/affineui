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
void Renderer::shutdown() { impl_->ready = false; }
void Renderer::render(Document&, int, int, float) {}

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

void Renderer::shutdown() {
    if (!impl_->ready) return;
    impl_->painter.reset();
    if (impl_->vg) {
        nvgDeleteSokol(impl_->vg);
        impl_->vg = nullptr;
    }
    impl_->ready = false;
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
