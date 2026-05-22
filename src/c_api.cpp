// Thin extern "C" surface mirroring the C++ API.
//
// Lets bindings from Rust / Zig / Odin / Python ctypes drive AffineUI
// without a C++ ABI dance. Functions are named affineui_<verb_object>;
// opaque handle types are `affineui_<type>*`.

#include "affineui/affineui.h"
#include "affineui/c_api.h"
#include "affineui/embed.h"
#include "affineui/ui.h"

#include <string_view>

namespace {

affineui::Backend to_cpp_backend(affineui_backend b) {
    switch (b) {
        case AFFINEUI_GPU_METAL: return affineui::Backend::metal;
        case AFFINEUI_GPU_GL:    return affineui::Backend::gl;
        case AFFINEUI_GPU_WGPU:  return affineui::Backend::wgpu;
        case AFFINEUI_GPU_D3D11:
        default:                 return affineui::Backend::d3d11;
    }
}

affineui::PixelFormat to_cpp_format(int f) {
    switch (f) {
        case AFFINEUI_FORMAT_RGBA8:         return affineui::PixelFormat::rgba8;
        case AFFINEUI_FORMAT_BGRA8:         return affineui::PixelFormat::bgra8;
        case AFFINEUI_FORMAT_DEPTH:         return affineui::PixelFormat::depth;
        case AFFINEUI_FORMAT_DEPTH_STENCIL: return affineui::PixelFormat::depth_stencil;
        case AFFINEUI_FORMAT_DEFAULT:
        default:                            return affineui::PixelFormat::default_;
    }
}

}  // namespace

extern "C" {

const char* affineui_version() {
    return AFFINEUI_VERSION_STRING;
}

affineui_ui* affineui_ui_create(void) {
    return reinterpret_cast<affineui_ui*>(new affineui::Ui());
}

void affineui_ui_destroy(affineui_ui* ui) {
    delete reinterpret_cast<affineui::Ui*>(ui);
}

void affineui_ui_init(affineui_ui* ui, const affineui_init_desc* desc) {
    if (!ui || !desc) return;
    auto& cpp = *reinterpret_cast<affineui::Ui*>(ui);

    affineui::GpuContext gpu{};
    affineui::InitDesc init{};
    if (desc->gpu) {
        const auto& g = *desc->gpu;
        gpu.backend                = to_cpp_backend(g.backend);
        gpu.d3d11.device           = g.d3d11_device;
        gpu.d3d11.device_context   = g.d3d11_device_context;
        gpu.metal.device           = g.metal_device;
        gpu.wgpu.device            = g.wgpu_device;
        gpu.color_format           = to_cpp_format(g.color_format);
        gpu.depth_format           = to_cpp_format(g.depth_format);
        gpu.sample_count           = g.sample_count > 0 ? g.sample_count : 1;
        init.gpu = &gpu;
    }
    if (desc->default_font_family) init.default_font_family = desc->default_font_family;
    if (desc->default_font_size > 0) init.default_font_size = desc->default_font_size;

    cpp.init(init);  // gpu points at a local; init() consumes it synchronously
}

void affineui_ui_set_html(affineui_ui* ui, const char* html) {
    if (!ui || !html) return;
    reinterpret_cast<affineui::Ui*>(ui)->html(std::string_view{html});
}

void affineui_ui_set_css(affineui_ui* ui, const char* css) {
    if (!ui || !css) return;
    reinterpret_cast<affineui::Ui*>(ui)->css(std::string_view{css});
}

void affineui_ui_set_clear_color(affineui_ui* ui,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!ui) return;
    reinterpret_cast<affineui::Ui*>(ui)->set_clear_color(affineui::Color{r, g, b, a});
}

void affineui_ui_render(affineui_ui* ui, const affineui_frame_target* t) {
    if (!ui || !t) return;
    affineui::FrameTarget target{};
    target.width        = t->width;
    target.height       = t->height;
    target.dpi_scale    = t->dpi_scale > 0.0f ? t->dpi_scale : 1.0f;
    target.sample_count = t->sample_count > 0 ? t->sample_count : 1;
    target.clear        = t->clear != 0;
    target.d3d11.render_view        = t->d3d11_render_view;
    target.d3d11.resolve_view       = t->d3d11_resolve_view;
    target.d3d11.depth_stencil_view = t->d3d11_depth_stencil_view;
    target.metal.current_drawable      = t->metal_current_drawable;
    target.metal.depth_stencil_texture = t->metal_depth_stencil_texture;
    target.metal.msaa_color_texture    = t->metal_msaa_color_texture;
    target.wgpu.render_view        = t->wgpu_render_view;
    target.wgpu.resolve_view       = t->wgpu_resolve_view;
    target.wgpu.depth_stencil_view = t->wgpu_depth_stencil_view;
    target.gl.framebuffer          = t->gl_framebuffer;
    reinterpret_cast<affineui::Ui*>(ui)->render(target);
}

int affineui_run_html(const char* html) {
    if (!html) return 1;
    return ::affineui::run(std::string_view{html});
}

}  // extern "C"
