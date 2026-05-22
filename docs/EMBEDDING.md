# Embedding AffineUI in a host engine

AffineUI runs in one of two ownership modes:

| Mode | Who owns the GPU device + window + swapchain | Who presents | Entry points |
|---|---|---|---|
| **Standalone** | AffineUI (via sokol_app) | AffineUI | `affineui::App`, `affineui::run`, `sokol::wire` |
| **Embedded** | the host (your engine) | the host | `Ui::init(InitDesc)`, `Ui::render(FrameTarget)` |

Embedded mode is for a game engine that already has its own
`ID3D11Device` / `id<MTLDevice>` / GL context and its own frame loop, and
wants AffineUI to draw a UI layer **inside one of its render passes**
without taking over windowing or presentation.

It is **all-or-nothing**: either AffineUI creates every graphics object
(standalone), or the host provides *all* of them for its backend
(embedded). There is no partial handoff.

The public types live in [`affineui/embed.h`](../include/affineui/embed.h);
the matching C ABI is in [`affineui/c_api.h`](../include/affineui/c_api.h).

---

## The model

```
your engine frame:
  ├─ engine renders its 3D scene into a render target / the backbuffer
  ├─ affineui::Ui::render(FrameTarget{ those views })   ← UI drawn on top
  │     └─ internally: sg_begin_pass(views) → draw → sg_end_pass → sg_commit
  ├─ (engine rebinds its own pipeline state — see "GPU state" below)
  └─ engine presents / flips the swapchain
```

AffineUI brings up its **own** sokol_gfx instance on top of your device.
It never calls `Present`/`Flip` and never creates a window. You hand it:

* the device objects **once** at `init()` (a `GpuContext`), and
* the render-target views **every frame** at `render()` (a `FrameTarget`).

Handles are passed as opaque `void*`, so `embed.h` pulls in no
D3D11/Metal/GL/sokol headers.

---

## Lifecycle

```cpp
#include <affineui/affineui.h>

affineui::Ui ui;
ui.html(R"(<style>body{background:#1e1e2e;color:#eee}</style><h1>Hi</h1>)");

affineui::GpuContext gpu{};
gpu.backend              = affineui::Backend::d3d11;
gpu.d3d11.device         = my_d3d11_device;          // ID3D11Device*
gpu.d3d11.device_context = my_d3d11_immediate_ctx;   // ID3D11DeviceContext*
gpu.color_format         = affineui::PixelFormat::bgra8;          // your backbuffer format
gpu.depth_format         = affineui::PixelFormat::depth_stencil;  // needs a STENCIL component
gpu.sample_count         = 1;

affineui::InitDesc init{};
init.gpu = &gpu;            // present + complete = embedded mode
ui.init(init);             // brings up sokol_gfx + NanoVG on your device

// ... each frame ...
affineui::FrameTarget t{};
t.width  = backbuffer_width_px;
t.height = backbuffer_height_px;
t.dpi_scale = 1.0f;        // pixels per CSS point
t.clear = true;            // clear to Ui::clear_color first; false = draw over host content
t.d3d11.render_view        = my_backbuffer_rtv;   // ID3D11RenderTargetView*
t.d3d11.depth_stencil_view = my_depth_stencil_dsv;// ID3D11DepthStencilView* (D24S8)
ui.render(t);

my_swapchain->Present(1, 0);   // the host presents

// ... shutdown, while the device is still alive ...
ui.renderer().shutdown();      // tears down NanoVG + sokol_gfx (not your device)
```

A full, runnable host is in
[`examples/09_embed_d3d11`](../examples/09_embed_d3d11/main.cpp): a raw
Win32 + D3D11 program (no sokol_app) that owns the device, the swapchain,
and `Present`.

---

## GPU state contract

AffineUI renders inside its own sokol_gfx pass, which sets pipelines,
shaders, render targets, viewport/scissor, and (on D3D11) calls
`ID3D11DeviceContext::ClearState()` when it commits. **Treat your device /
context state as undefined after `Ui::render()` returns** and rebind
whatever you need before your next draw. This is the same contract Dear
ImGui's renderers use. AffineUI does not save or restore your state.

Conversely, AffineUI assumes nothing about the state you leave behind — it
fully sets up everything it needs each frame.

---

## Requirements & gotchas

* **Backend must match the build.** `GpuContext::backend` must equal the
  backend AffineUI was compiled for (`AFFINEUI_BACKEND_*`). On Windows the
  default build is D3D11. A mismatch is rejected at `init()` (the renderer
  logs and stays uninitialized).
* **Provide a depth-stencil with a stencil component.** NanoVG (AffineUI's
  rasterizer) uses the stencil buffer for fills, so the target needs a
  depth-stencil view with stencil bits (e.g. `DXGI_FORMAT_D24_UNORM_S8_UINT`).
  Without one, fills render incorrectly.
* **Formats must match.** `GpuContext::color_format` / `depth_format` must
  match the views you pass each frame. `PixelFormat::default_` resolves to
  BGRA8 color + DEPTH_STENCIL depth.
* **D3D11 swapchains:** use the flip model (`DXGI_SWAP_EFFECT_FLIP_DISCARD`,
  `BufferCount >= 2`) — the legacy bitblt model does not composite reliably
  on modern Windows. Re-bind/clear per frame; the example shows the pattern.
* **Default document background is white** (like a browser). Set
  `body { background: ... }` if you want the page itself opaque; otherwise
  only `FrameTarget::clear` + `Ui::set_clear_color` paint the backdrop, and
  the body draws over it.
* **Single-threaded.** Call `init` / `render` / `shutdown` on the thread
  that owns the graphics context.
* **One sokol_gfx instance.** Embedded mode calls `sg_setup` itself, so the
  host must not also be using sokol_gfx. (A host that *does* use sokol can
  build with `-DAFFINEUI_HOST_PROVIDES_SOKOL=ON` instead.)

---

## C ABI

The same flow in C ([`affineui/c_api.h`](../include/affineui/c_api.h)):

```c
affineui_ui* ui = affineui_ui_create();
affineui_ui_set_html(ui, "<h1>Hi</h1>");

affineui_gpu_context gpu = {0};
gpu.backend              = AFFINEUI_GPU_D3D11;
gpu.d3d11_device         = my_device;
gpu.d3d11_device_context = my_context;
gpu.color_format         = AFFINEUI_FORMAT_BGRA8;
gpu.depth_format         = AFFINEUI_FORMAT_DEPTH_STENCIL;
gpu.sample_count         = 1;

affineui_init_desc init = {0};
init.gpu = &gpu;
affineui_ui_init(ui, &init);

affineui_frame_target t = {0};
t.width = w; t.height = h; t.dpi_scale = 1.0f; t.sample_count = 1; t.clear = 1;
t.d3d11_render_view        = my_rtv;
t.d3d11_depth_stencil_view = my_dsv;
affineui_ui_render(ui, &t);     /* then the host presents */

affineui_ui_destroy(ui);
```

> Note: the C enum is `AFFINEUI_GPU_D3D11` (not `AFFINEUI_BACKEND_D3D11`,
> which is a build-time macro).

---

## Backend handles

| Backend | `GpuContext` (init) | `FrameTarget` (per frame) |
|---|---|---|
| **D3D11** | `d3d11.device` (ID3D11Device*), `d3d11.device_context` (ID3D11DeviceContext*) | `d3d11.render_view` (ID3D11RenderTargetView*), `d3d11.depth_stencil_view` (ID3D11DepthStencilView*), `d3d11.resolve_view` (MSAA, optional) |
| **Metal** | `metal.device` (id<MTLDevice>) | `metal.current_drawable` (CAMetalDrawable), `metal.depth_stencil_texture`, `metal.msaa_color_texture` |
| **GL** | none — GL context current on the calling thread | `gl.framebuffer` (FBO name; 0 = default) |
| **WGPU** | `wgpu.device` (WGPUDevice) | `wgpu.render_view`, `wgpu.resolve_view`, `wgpu.depth_stencil_view` (WGPUTextureView) |

The structs carry every backend; only the one matching the build is read.
D3D11 is verified today; Metal/GL/WGPU share the same code path and wiring.
