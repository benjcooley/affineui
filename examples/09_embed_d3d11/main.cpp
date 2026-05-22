// 09_embed_d3d11 — embedding AffineUI in a host that owns the GPU.
//
// This is NOT a sokol_app program. It creates its own Win32 window, its own
// ID3D11Device + swapchain, and drives its own present/flip — exactly like a
// game engine would. AffineUI is handed:
//   • the device + immediate context ONCE (Ui::init with a GpuContext), and
//   • the backbuffer + depth-stencil views EACH FRAME (Ui::render(FrameTarget)).
//
// AffineUI opens its own sokol_gfx pass into those views, draws the UI, and
// ends+commits. It never presents — this host does. After Ui::render the
// D3D11 device-context state is considered clobbered (ImGui-style contract);
// a real engine would rebind its own pipeline state before its next draw.

#include <affineui/affineui.h>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace {

ID3D11Device*           g_device   = nullptr;
ID3D11DeviceContext*    g_context  = nullptr;
IDXGISwapChain*         g_swap     = nullptr;
ID3D11RenderTargetView* g_rtv      = nullptr;
ID3D11DepthStencilView* g_dsv      = nullptr;
int  g_width  = 1024;
int  g_height = 768;
bool g_quit   = false;

void release_targets() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_dsv) { g_dsv->Release(); g_dsv = nullptr; }
}

// (Re)create the backbuffer RTV and a matching D24S8 depth-stencil view.
// NanoVG (AffineUI's rasterizer) uses the stencil buffer, so a depth-stencil
// with a stencil component is required for correct fills.
bool create_targets() {
    release_targets();

    ID3D11Texture2D* backbuffer = nullptr;
    if (FAILED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                 reinterpret_cast<void**>(&backbuffer)))) {
        return false;
    }
    HRESULT hr = g_device->CreateRenderTargetView(backbuffer, nullptr, &g_rtv);
    backbuffer->Release();
    if (FAILED(hr)) return false;

    D3D11_TEXTURE2D_DESC dd{};
    dd.Width      = static_cast<UINT>(g_width);
    dd.Height     = static_cast<UINT>(g_height);
    dd.MipLevels  = 1;
    dd.ArraySize  = 1;
    dd.Format     = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage      = D3D11_USAGE_DEFAULT;
    dd.BindFlags  = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depth = nullptr;
    if (FAILED(g_device->CreateTexture2D(&dd, nullptr, &depth))) return false;
    hr = g_device->CreateDepthStencilView(depth, nullptr, &g_dsv);
    depth->Release();
    return SUCCEEDED(hr);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (g_swap && wp != SIZE_MINIMIZED) {
                g_width  = LOWORD(lp);
                g_height = HIWORD(lp);
                release_targets();
                g_swap->ResizeBuffers(0, static_cast<UINT>(g_width),
                                      static_cast<UINT>(g_height),
                                      DXGI_FORMAT_UNKNOWN, 0);
                create_targets();
            }
            return 0;
        case WM_CLOSE:
        case WM_DESTROY:
            g_quit = true;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

}  // namespace

int main() {
    // 1:1 backbuffer-to-window pixels (no DWM DPI virtualization).
    SetProcessDPIAware();

    // ── Host-owned window ────────────────────────────────────────────
    HINSTANCE inst = GetModuleHandle(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"AffineUIEmbedD3D11";
    RegisterClassExW(&wc);

    RECT r{0, 0, g_width, g_height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"AffineUI — embedded (raw D3D11 host)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);

    RECT cr{};
    GetClientRect(hwnd, &cr);
    g_width  = cr.right  - cr.left;
    g_height = cr.bottom - cr.top;

    // ── Host-owned D3D11 device + swapchain ──────────────────────────
    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount       = 2;                            // flip model wants >= 2
    scd.BufferDesc.Width  = static_cast<UINT>(g_width);
    scd.BufferDesc.Height = static_cast<UINT>(g_height);
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // BGRA8 (sokol default)
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow      = hwnd;
    scd.SampleDesc.Count  = 1;
    scd.Windowed          = TRUE;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // modern; required on Win10+ DWM

    UINT flags = 0;
#ifndef NDEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        &scd, &g_swap, &g_device, nullptr, &g_context);
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        // Retry without the debug layer (not installed on all machines).
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            nullptr, 0, D3D11_SDK_VERSION,
            &scd, &g_swap, &g_device, nullptr, &g_context);
    }
    if (FAILED(hr)) {
        MessageBoxW(hwnd, L"D3D11CreateDeviceAndSwapChain failed", L"error", MB_OK);
        return 1;
    }
    if (!create_targets()) return 1;

    // ── Hand the device to AffineUI (once) ───────────────────────────
    affineui::Ui ui;
    ui.html(R"(
        <style>
          html, body { height: 100%; margin: 0; }
          body { font-family: sans-serif; padding: 24px;
                 background: #1e1e2e; color: #cdd6f4; }
          h1   { color: #f38ba8; }
          .card{ background:#313244; border-radius:8px; padding:16px; margin-top:12px; }
        </style>
        <h1>Embedded in a raw D3D11 host</h1>
        <div class="card">
          <p>AffineUI got the host's ID3D11Device + context at init,</p>
          <p>and the backbuffer + depth-stencil views each frame.</p>
          <p>The host owns the swapchain and calls Present().</p>
        </div>
    )");
    ui.set_clear_color(affineui::Color{30, 30, 46, 255});

    affineui::GpuContext gpu{};
    gpu.backend              = affineui::Backend::d3d11;
    gpu.d3d11.device         = g_device;
    gpu.d3d11.device_context = g_context;
    gpu.color_format         = affineui::PixelFormat::bgra8;
    gpu.depth_format         = affineui::PixelFormat::depth_stencil;
    gpu.sample_count         = 1;

    affineui::InitDesc init{};
    init.gpu = &gpu;
    ui.init(init);

    ShowWindow(hwnd, SW_SHOW);

    // ── Host frame loop (host owns the flip) ─────────────────────────
    while (!g_quit) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (g_quit) break;

        affineui::FrameTarget target{};
        target.width                  = g_width;
        target.height                 = g_height;
        target.dpi_scale              = 1.0f;
        target.sample_count           = 1;
        target.clear                  = true;
        target.d3d11.render_view        = g_rtv;
        target.d3d11.depth_stencil_view = g_dsv;

        ui.render(target);     // AffineUI: sg_begin_pass(views) ... end + commit
        g_swap->Present(1, 0); // host presents
    }

    ui.renderer().shutdown();  // tear down sokol_gfx while the device is alive
    release_targets();
    if (g_swap)    g_swap->Release();
    if (g_context) g_context->Release();
    if (g_device)  g_device->Release();
    return 0;
}
