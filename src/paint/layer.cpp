// Layer = one NanoVG framebuffer object. Allocated lazily; the
// content hash cached on the layer drives the idle-frame skip in
// the rasterize stage.

#include "internal/layer.h"

#if !defined(AFFINEUI_STUB_BUILD)

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
// nanovg_gl_utils.h has no extern "C" guards of its own. Wrap so its
// nvglu* symbols agree with the C-linked implementation in
// src/paint/nanovg_impl.c.
extern "C" {
#    include "nanovg_gl_utils.h"
}

namespace affineui::detail {

Layer::~Layer() { destroy(); }

Layer::Layer(Layer&& o) noexcept
    : last_content_hash(o.last_content_hash), fb_(o.fb_), w_(o.w_), h_(o.h_) {
    o.fb_ = nullptr;
    o.w_ = o.h_ = 0;
    o.last_content_hash = 0;
}

Layer& Layer::operator=(Layer&& o) noexcept {
    if (this != &o) {
        destroy();
        fb_  = o.fb_;
        w_   = o.w_;
        h_   = o.h_;
        last_content_hash = o.last_content_hash;
        o.fb_ = nullptr;
        o.w_  = o.h_ = 0;
        o.last_content_hash = 0;
    }
    return *this;
}

void Layer::destroy() {
    if (fb_) {
        nvgluDeleteFramebuffer(fb_);
        fb_ = nullptr;
    }
    w_ = h_ = 0;
    last_content_hash = 0;
}

void Layer::ensure_size(NVGcontext* vg, int width_px, int height_px) {
    if (width_px <= 0 || height_px <= 0) return;
    if (fb_ && w_ == width_px && h_ == height_px) return;
    if (fb_) {
        nvgluDeleteFramebuffer(fb_);
        fb_ = nullptr;
    }
    fb_ = nvgluCreateFramebuffer(vg, width_px, height_px,
                                 NVG_IMAGE_PREMULTIPLIED);
    w_ = width_px;
    h_ = height_px;
    // Force a re-rasterize next frame: any prior hash is meaningless
    // against a freshly allocated FBO.
    last_content_hash = 0;
}

unsigned Layer::gl_texture() const {
    if (!fb_) return 0;
    return static_cast<unsigned>(fb_->texture);
}

}  // namespace affineui::detail

#else  // AFFINEUI_STUB_BUILD

namespace affineui::detail {
Layer::~Layer() = default;
Layer::Layer(Layer&&) noexcept = default;
Layer& Layer::operator=(Layer&&) noexcept = default;
void Layer::destroy() {}
void Layer::ensure_size(NVGcontext*, int, int) {}
unsigned Layer::gl_texture() const { return 0; }
}  // namespace affineui::detail

#endif
