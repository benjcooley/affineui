#pragma once

// affineui::Renderer — the graphics-API-specific part of AffineUI.
//
// Owns:
//   • the NanoVG context (and underlying GL state)
//   • the root layer's FBO
//   • the cached display list across frames
//   • the compositor (textured-quad blit to default framebuffer)
//
// Does NOT own:
//   • the window (your windowing toolkit does — sokol_app, SDL,
//     glfw, whatever)
//   • the event loop
//   • the input source
//
// Lifecycle:
//   The first call to `render()` lazily initializes GPU resources
//   against the *currently bound* graphics context, so you can just
//   construct a Renderer and call render() from inside your render
//   pass without remembering an init step. Call `shutdown()` before
//   the GL context goes away (or rely on the destructor when the
//   process ends).
//
// Thread model:
//   Single-threaded. Construct, render, and destroy on the same
//   thread — same one that owns your graphics context.

#include "affineui/types.h"

#include <memory>

namespace affineui {

class Document;

namespace detail { struct RendererImpl; }

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) noexcept;
    Renderer& operator=(Renderer&&) noexcept;

    /// True once GPU resources are live. Becomes true after the first
    /// render() (or an explicit init_gl()) and false again after
    /// shutdown().
    bool ready() const noexcept;

    /// One-time GPU resource creation. Assumes a current GL3 context.
    /// You normally don't have to call this — `render()` does it on
    /// the first invocation. Useful if you want to pay the init cost
    /// at a known time (e.g. during a loading screen) rather than on
    /// the first frame.
    void init_gl();

    /// Release all GPU resources. Must be called while the graphics
    /// context that created them is still current — usually right
    /// before your windowing layer tears the context down.
    void shutdown();

    /// Render one frame of `doc` into the default framebuffer.
    ///   fb_w, fb_h  : framebuffer dimensions in pixels
    ///   dpi_scale   : pixels-per-point (1.0 standard, 2.0 Retina, etc.)
    ///
    /// Calls into the document's layout + paint pipeline as needed,
    /// rasterizes into an internal FBO, then blits the result to the
    /// default framebuffer with the configured clear color. No
    /// frame-loop control flow — call this once per your-game's-frame
    /// from inside your render pass.
    void render(Document& doc, int fb_w, int fb_h, float dpi_scale);

    /// Background color (RGBA) applied to the default framebuffer
    /// before the document quad is composited on top. Default is the
    /// catppuccin Base #1e1e2e.
    void set_clear_color(Color c);
    Color clear_color() const;

private:
    std::unique_ptr<detail::RendererImpl> impl_;
};

}  // namespace affineui
