#ifndef AFFINEUI_C_API_H
#define AFFINEUI_C_API_H

// AffineUI C ABI — a flat extern "C" surface mirroring the C++ embedding
// API, for engines and language bindings (Rust / Zig / Odin / C). Pass the
// host's native graphics objects as opaque void* handles; AffineUI renders
// into render-target views you supply each frame and never presents.
//
// See affineui/embed.h for the full semantics (all-or-nothing ownership,
// the D3D11/GL state-clobber contract, per-backend handle types).

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to an affineui::Ui instance.
typedef struct affineui_ui affineui_ui;

// Concrete GPU backend. Must match the backend AffineUI was compiled for.
// (Named to avoid colliding with the AFFINEUI_BACKEND_* compile macros.)
typedef enum affineui_backend {
    AFFINEUI_GPU_D3D11 = 0,
    AFFINEUI_GPU_METAL = 1,
    AFFINEUI_GPU_GL    = 2,
    AFFINEUI_GPU_WGPU  = 3
} affineui_backend;

typedef enum affineui_pixel_format {
    AFFINEUI_FORMAT_DEFAULT       = 0,
    AFFINEUI_FORMAT_RGBA8         = 1,
    AFFINEUI_FORMAT_BGRA8         = 2,
    AFFINEUI_FORMAT_DEPTH         = 3,
    AFFINEUI_FORMAT_DEPTH_STENCIL = 4
} affineui_pixel_format;

// Host graphics objects, supplied once at init (all-or-nothing). Fill the
// fields for your backend; leave the rest null.
typedef struct affineui_gpu_context {
    affineui_backend backend;
    const void* d3d11_device;          // ID3D11Device*
    const void* d3d11_device_context;  // ID3D11DeviceContext*
    const void* metal_device;          // id<MTLDevice>
    const void* wgpu_device;           // WGPUDevice
    int color_format;                  // affineui_pixel_format
    int depth_format;                  // affineui_pixel_format
    int sample_count;                  // MSAA sample count (>=1)
} affineui_gpu_context;

typedef struct affineui_init_desc {
    const affineui_gpu_context* gpu;   // non-null + complete = embedded mode
    const char* default_font_family;   // may be null
    int         default_font_size;     // 0 = use AffineUI default
} affineui_init_desc;

// Per-frame render-target views. AffineUI opens a pass into these, draws,
// and ends+commits; the host presents. Fill the fields for your backend.
typedef struct affineui_frame_target {
    int   width;         // pixels
    int   height;        // pixels
    float dpi_scale;     // pixels per CSS point (1.0, 2.0, ...)
    int   sample_count;  // must match the target (>=1)
    int   clear;         // non-zero = clear to clear color first

    const void* d3d11_render_view;          // ID3D11RenderTargetView*
    const void* d3d11_resolve_view;         // ID3D11RenderTargetView* (optional)
    const void* d3d11_depth_stencil_view;   // ID3D11DepthStencilView*

    const void* metal_current_drawable;     // CAMetalDrawable
    const void* metal_depth_stencil_texture;// MTLTexture (optional)
    const void* metal_msaa_color_texture;   // MTLTexture (optional)

    const void* wgpu_render_view;           // WGPUTextureView
    const void* wgpu_resolve_view;          // WGPUTextureView (optional)
    const void* wgpu_depth_stencil_view;    // WGPUTextureView (optional)

    uint32_t    gl_framebuffer;             // GL FBO (0 = default)
} affineui_frame_target;

// ── Lifecycle ────────────────────────────────────────────────────────
affineui_ui* affineui_ui_create(void);
void         affineui_ui_destroy(affineui_ui* ui);

// Initialize for embedded mode against host graphics objects.
void affineui_ui_init(affineui_ui* ui, const affineui_init_desc* desc);

// ── Content ──────────────────────────────────────────────────────────
void affineui_ui_set_html(affineui_ui* ui, const char* html);
void affineui_ui_set_css(affineui_ui* ui, const char* css);
void affineui_ui_set_clear_color(affineui_ui* ui,
                                 uint8_t r, uint8_t g, uint8_t b, uint8_t a);

// ── Render (embedded) ────────────────────────────────────────────────
void affineui_ui_render(affineui_ui* ui, const affineui_frame_target* target);

// ── Misc ─────────────────────────────────────────────────────────────
const char* affineui_version(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // AFFINEUI_C_API_H
