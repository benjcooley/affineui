// affineui::Renderer — graphics-only side of the engine. Lifted out
// of App so the same code path serves any windowing layer (sokol_app,
// SDL, glfw, custom). What used to be `App::cb_frame`'s body now
// lives in `Renderer::render()`; everything sokol-specific stays in
// App / the sokol adapter.

#include "affineui/renderer.h"

#include "affineui/document.h"
#include "affineui/painter.h"

#include <memory>
#include <utility>

#if !defined(AFFINEUI_STUB_BUILD)
#    include "internal/compositor.h"
#    include "internal/display_list_painter.h"
#    include "internal/layer.h"
#    include "internal/paint_internal.h"

// Platform GL headers (NanoVG GL3 needs GLuint et al.).
#    if defined(__APPLE__)
#        define GL_SILENCE_DEPRECATION
#        include <OpenGL/gl3.h>
#    elif defined(_WIN32)
#        define WIN32_LEAN_AND_MEAN
#        include <windows.h>
#        include <GL/gl.h>
#    else
#        define GL_GLEXT_PROTOTYPES
#        include <GL/gl.h>
#        include <GL/glext.h>
#    endif

#    include "nanovg.h"
#    define NANOVG_GL3 1
#    include "nanovg_gl.h"
extern "C" {
#    include "nanovg_gl_utils.h"
}
#endif  // !AFFINEUI_STUB_BUILD

namespace affineui {

namespace detail {

struct RendererImpl {
    Color clear_color{30, 30, 46, 255};
    bool  ready{false};

#if !defined(AFFINEUI_STUB_BUILD)
    NVGcontext*              vg{nullptr};
    std::unique_ptr<Painter> painter;
    detail::DisplayList      cached_list;
    detail::Layer            root_layer;
    detail::Compositor       compositor;
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

#else  // real GL3 path

void Renderer::init_gl() {
    if (impl_->ready) return;

    impl_->vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!impl_->vg) {
        std::fprintf(stderr, "[affineui] nvgCreateGL3 failed\n");
        return;
    }
    // Best-effort default sans-serif. If none of the platform paths
    // exist, text simply won't render — by design we never crash.
    detail::register_default_font(impl_->vg);
    impl_->painter = detail::make_nanovg_painter(impl_->vg);

    if (!impl_->compositor.init()) {
        std::fprintf(stderr, "[affineui] compositor init failed\n");
        return;
    }
    impl_->ready = true;
}

void Renderer::shutdown() {
    if (!impl_->ready) return;
    impl_->compositor.shutdown();
    impl_->root_layer.destroy();
    impl_->painter.reset();
    if (impl_->vg) {
        nvgDeleteGL3(impl_->vg);
        impl_->vg = nullptr;
    }
    impl_->ready = false;
}

void Renderer::render(Document& doc, int fb_w, int fb_h, float dpi_scale) {
    // Lazy GPU init — works because the caller's render pass is
    // active by the time we get here, meaning a GL context is
    // current. The cost is paid on the first frame only.
    if (!impl_->ready) {
        init_gl();
        if (!impl_->ready) return;
    }

    // CSS layout works in points; everything GL-side wants pixels.
    const int   pt_w = static_cast<int>(static_cast<float>(fb_w) / dpi_scale + 0.5f);
    const int   pt_h = static_cast<int>(static_cast<float>(fb_h) / dpi_scale + 0.5f);

    const bool viewport_changed =
        impl_->first_frame || fb_w != impl_->last_w
                           || fb_h != impl_->last_h;
    const bool layout_dirty = doc.content_size().width != pt_w;
    if (viewport_changed || layout_dirty) {
        doc.layout(pt_w, pt_h, impl_->painter.get());
        impl_->last_w     = fb_w;
        impl_->last_h     = fb_h;
        impl_->last_dpi   = dpi_scale;
        impl_->first_frame = false;
    }
    (void)pt_h;

    // ── Paint: record into a DisplayList (cheap; no GPU calls) ─────
    detail::DisplayListBuilder builder(impl_->painter.get());
    builder.begin_frame(pt_w, pt_h, dpi_scale);
    doc.draw(builder);
    builder.end_frame();
    auto& dl = builder.list();

    // ── Rasterize: only when the DL hash changed ───────────────────
    impl_->root_layer.ensure_size(impl_->vg, fb_w, fb_h);
    const bool needs_rasterize =
        viewport_changed
        || dl.content_hash != impl_->root_layer.last_content_hash;

    if (needs_rasterize) {
        nvgluBindFramebuffer(impl_->root_layer.fb());
        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        impl_->painter->begin_frame(pt_w, pt_h, dpi_scale);
        detail::replay(dl, *impl_->painter);
        impl_->painter->end_frame();
        nvgluBindFramebuffer(nullptr);
        impl_->root_layer.last_content_hash = dl.content_hash;
    }

    // ── Composite: every frame, cheap. One textured quad. ──────────
    const Color c = impl_->clear_color;
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    impl_->compositor.blit_fullscreen(impl_->root_layer.gl_texture(),
                                      /*opacity=*/1.0f,
                                      /*flip_y=*/false);
}

#endif  // AFFINEUI_STUB_BUILD

}  // namespace affineui
