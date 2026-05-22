#pragma once

// affineui embedding API — drive AffineUI from a host (game engine) that
// already owns the core graphics-API objects and its own frame loop.
//
// Two ownership modes, all-or-nothing:
//
//   • Standalone — AffineUI creates and owns the device, swapchain, window,
//     and frame loop (see affineui::App / affineui::run). Leave
//     InitDesc::gpu null.
//
//   • Embedded   — the host owns the device and the present/flip. You hand
//     AffineUI a *complete* GpuContext once (Ui::init), then hand it the
//     render-target views for each frame (Ui::render(const FrameTarget&)).
//     AffineUI opens its own sokol_gfx pass into those views, draws, and
//     ends+commits — it never presents.
//
// "All-or-nothing" means exactly that: either you provide every field of
// GpuContext for your backend, or you provide no GpuContext at all.
// Partially-filled contexts are rejected at init.
//
// State contract (D3D11 / GL): AffineUI renders inside its own sokol_gfx
// pass, which clobbers pipeline / device-context state. Treat your GPU
// state as undefined after Ui::render(FrameTarget) returns and rebind what
// you need — the same contract Dear ImGui's renderers use.
//
// All graphics handles are opaque `void*` so this header pulls in no
// D3D11 / Metal / GL / sokol headers. Hand over the native objects:
//   D3D11  device          = ID3D11Device*
//          device_context  = ID3D11DeviceContext*
//          render_view     = ID3D11RenderTargetView*
//          depth_stencil_view = ID3D11DepthStencilView*   (optional)
//   Metal  device          = id<MTLDevice>
//          current_drawable= CAMetalDrawable               (per frame)
//   GL     (no handles; your GL context must be current on this thread)
//          framebuffer     = GL framebuffer object name    (0 = default)
//
// Threading: single-threaded, like the rest of AffineUI. Call init/render
// on the thread that owns the graphics context.

#include "affineui/types.h"

#include <cstdint>
#include <string>

namespace affineui {

/// Concrete GPU backend. Must match the backend AffineUI was compiled for
/// (AFFINEUI_BACKEND_* / SOKOL_*). Mismatches are rejected at init.
enum class Backend {
    d3d11,
    metal,
    gl,
    wgpu,
};

/// Texture formats for the host's color / depth-stencil targets. `default_`
/// lets AffineUI pick the platform-typical swapchain format (BGRA8 color,
/// DEPTH_STENCIL depth) — fine for the common case.
enum class PixelFormat {
    default_,
    rgba8,
    bgra8,
    depth,
    depth_stencil,
};

/// The host's core graphics objects, supplied once at init in embedded
/// mode. Fill the sub-struct for your backend completely. Handles are
/// borrowed: AffineUI never creates or destroys them.
struct GpuContext {
    Backend backend = Backend::d3d11;

    struct {
        const void* device         = nullptr;  // ID3D11Device*
        const void* device_context = nullptr;  // ID3D11DeviceContext*
    } d3d11;

    struct {
        const void* device = nullptr;          // id<MTLDevice>
    } metal;

    struct {
        const void* device = nullptr;          // WGPUDevice
    } wgpu;

    // gl: no handles — the host's GL context must be current on the
    // calling thread when init/render run.

    /// Formats AffineUI's pipelines should target. Must match the formats
    /// of the render targets you pass to render() each frame.
    PixelFormat color_format = PixelFormat::default_;
    PixelFormat depth_format = PixelFormat::default_;
    int         sample_count = 1;  // MSAA sample count of the targets
};

/// One-time initialization for embedded mode.
struct InitDesc {
    /// Host graphics objects. Non-null + complete → embedded mode (AffineUI
    /// uses the host's objects). Null → standalone (AffineUI owns the GPU).
    const GpuContext* gpu = nullptr;

    /// Optional resource loader for url-referenced assets (CSS, images).
    ResourceLoader resource_loader{};

    /// Default font + size used when CSS doesn't specify one.
    std::string default_font_family = "sans-serif";
    int         default_font_size   = 16;
};

/// The host's render-target views for the current frame. AffineUI brackets
/// a sokol_gfx pass around these, draws the UI, and ends+commits. The host
/// owns the views' lifetime and performs the present/flip afterwards.
struct FrameTarget {
    int   width     = 0;     // target width in pixels
    int   height    = 0;     // target height in pixels
    float dpi_scale = 1.0f;  // pixels per CSS point (1.0, 2.0 Retina, ...)
    int   sample_count = 1;  // must match the target's MSAA sample count

    /// Clear the target to the Ui's clear color before drawing. Set false
    /// to draw the UI over content the host already rendered into it.
    bool  clear = true;

    struct {
        const void* render_view        = nullptr;  // ID3D11RenderTargetView*
        const void* resolve_view       = nullptr;  // ID3D11RenderTargetView* (MSAA resolve, optional)
        const void* depth_stencil_view = nullptr;  // ID3D11DepthStencilView* (optional)
    } d3d11;

    struct {
        const void* current_drawable      = nullptr;  // CAMetalDrawable (per frame)
        const void* depth_stencil_texture = nullptr;  // MTLTexture (optional)
        const void* msaa_color_texture    = nullptr;  // MTLTexture (optional)
    } metal;

    struct {
        const void* render_view        = nullptr;  // WGPUTextureView
        const void* resolve_view       = nullptr;  // WGPUTextureView (optional)
        const void* depth_stencil_view = nullptr;  // WGPUTextureView (optional)
    } wgpu;

    struct {
        std::uint32_t framebuffer = 0;  // GL framebuffer object (0 = default)
    } gl;
};

}  // namespace affineui
