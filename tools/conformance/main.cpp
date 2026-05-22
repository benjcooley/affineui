// conformance_test — headless AffineUI side of the conformance harness.
//
// Loads a named test (--test <name> => <cases-dir>/<name>/{index.html,case.json}),
// replays the case's interaction script, and writes a PPM at each `snapshot`
// marker. The browser side (conformance/browser) loads the SAME case.json and
// replays it via Playwright; the runner (conformance/run.py) pixel-diffs each
// pair.
//
//   conformance_test --test <name> [--cases-dir DIR] [--out-dir DIR]
//                    [--width W] [--height H] [--dpi S]
//
// Step vocabulary (case.json "steps", an ordered list) is intentionally small +
// extensible — agents add new step types (named DOM interactions, keys, etc.)
// to this dispatch and the browser's as they go. Unknown step types are
// skipped, so a newer script degrades gracefully on an older tool. Starter set:
//   {"click":[x,y]} {"hover":[x,y]} {"wait_ms":N} {"snapshot":"name"}
// Coordinates are CSS pixels (== device px at dpi 1), matching the browser.

#include <affineui/affineui.h>

#include "json.h"

#include <windows.h>
#include <d3d11.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")

namespace {

struct Step {
    enum Kind { Click, Hover, Wait, Snapshot } kind;
    int x = 0, y = 0, ms = 0;
    std::string name;
};

struct Args {
    std::string test, cases_dir = "conformance/cases", out_dir = ".", html, script;
    int   width = 0, height = 0;     // 0 => take from case.json, else 1024/768
    float dpi   = 0.0f;             // 0 => take from case.json, else 1.0
    std::vector<Step> steps;
};

bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto sval = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if      (s == "--test")      a.test = sval();
        else if (s == "--cases-dir") a.cases_dir = sval();
        else if (s == "--out-dir")   a.out_dir = sval();
        else if (s == "--html")      a.html = sval();
        else if (s == "--script")    a.script = sval();
        else if (s == "--width")     a.width = std::atoi(sval().c_str());
        else if (s == "--height")    a.height = std::atoi(sval().c_str());
        else if (s == "--dpi")       a.dpi = (float)std::atof(sval().c_str());
        else { std::fprintf(stderr, "unknown option %s\n", s.c_str()); return false; }
    }
    if (a.test.empty() && a.html.empty()) {
        std::fprintf(stderr, "usage: conformance_test --test <name> [--cases-dir DIR] [--out-dir DIR] [--width W] [--height H] [--dpi S]\n");
        return false;
    }
    if (a.html.empty())   a.html   = a.cases_dir + "/" + a.test + "/index.html";
    if (a.script.empty()) a.script = a.cases_dir + "/" + a.test + "/case.json";
    if (a.test.empty())   a.test   = "test";
    return true;
}

std::string read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::string s((size_t)(n > 0 ? n : 0), '\0');
    if (n > 0) { size_t got = std::fread(&s[0], 1, (size_t)n, f); s.resize(got); }
    std::fclose(f);
    return s;
}

// Load the test script (case.json): fills width/height/dpi (when unset) and the
// step list. Returns false only on a malformed file (missing is fine).
bool load_case(Args& a) {
    const std::string text = read_file(a.script);
    if (text.empty()) return true;  // no case.json => static default snapshot
    cjson::Value root;
    if (!cjson::parse(text, root) || root.type != cjson::Value::Obj) {
        std::fprintf(stderr, "warning: malformed %s; ignoring\n", a.script.c_str());
        return true;
    }
    if (a.width  == 0)   if (auto* v = root.find("width"))  a.width  = (int)v->as_num(0);
    if (a.height == 0)   if (auto* v = root.find("height")) a.height = (int)v->as_num(0);
    if (a.dpi    == 0.f) if (auto* v = root.find("dpi"))    a.dpi    = (float)v->as_num(0);

    if (const cjson::Value* steps = root.find("steps"); steps && steps->type == cjson::Value::Arr) {
        for (const cjson::Value& s : *steps->arr) {
            if (const cjson::Value* c = s.find("click"))    { a.steps.push_back({Step::Click, c->at_int(0), c->at_int(1)}); }
            else if (const cjson::Value* h = s.find("hover")) { a.steps.push_back({Step::Hover, h->at_int(0), h->at_int(1)}); }
            else if (const cjson::Value* w = s.find("wait_ms")) { Step st{Step::Wait}; st.ms = (int)w->as_num(); a.steps.push_back(st); }
            else if (const cjson::Value* n = s.find("snapshot")) { Step st{Step::Snapshot}; st.name = n->as_str(); a.steps.push_back(st); }
            // else: unknown step type — skip (agents add new types to both drivers).
        }
    }
    return true;
}

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool write_ppm(const std::string& path, const uint8_t* rgb, int w, int h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::fwrite(rgb, 1, (size_t)w * h * 3, f);
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) return 2;
    if (!load_case(args)) return 2;
    if (args.width  <= 0) args.width  = 1024;
    if (args.height <= 0) args.height = 768;
    if (args.dpi    <= 0) args.dpi    = 1.0f;
    const int W = args.width, H = args.height;

    // ── Headless D3D11 device + offscreen targets ────────────────────
    ID3D11Device* dev = nullptr; ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                   nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                               nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    if (FAILED(hr)) { std::fprintf(stderr, "D3D11CreateDevice failed (0x%08lx)\n", (unsigned long)hr); return 1; }

    D3D11_TEXTURE2D_DESC cd{};
    cd.Width = W; cd.Height = H; cd.MipLevels = 1; cd.ArraySize = 1;
    cd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; cd.SampleDesc.Count = 1;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ID3D11Texture2D* color = nullptr; ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(dev->CreateTexture2D(&cd, nullptr, &color)) ||
        FAILED(dev->CreateRenderTargetView(color, nullptr, &rtv))) {
        std::fprintf(stderr, "create color target failed\n"); return 1; }

    D3D11_TEXTURE2D_DESC dd = cd;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depth = nullptr; ID3D11DepthStencilView* dsv = nullptr;
    if (FAILED(dev->CreateTexture2D(&dd, nullptr, &depth)) ||
        FAILED(dev->CreateDepthStencilView(depth, nullptr, &dsv))) {
        std::fprintf(stderr, "create depth target failed\n"); return 1; }

    D3D11_TEXTURE2D_DESC sd = cd;
    sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) {
        std::fprintf(stderr, "create staging failed\n"); return 1; }

    // ── AffineUI on our headless device ──────────────────────────────
    affineui::Ui ui;
    if (!ui.load(args.html)) { std::fprintf(stderr, "failed to load %s\n", args.html.c_str()); return 1; }

    affineui::GpuContext gpu{};
    gpu.backend = affineui::Backend::d3d11;
    gpu.d3d11.device = dev; gpu.d3d11.device_context = ctx;
    gpu.color_format = affineui::PixelFormat::bgra8;
    gpu.depth_format = affineui::PixelFormat::depth_stencil;
    affineui::InitDesc init{}; init.gpu = &gpu;
    ui.init(init);

    affineui::FrameTarget target{};
    target.width = W; target.height = H; target.dpi_scale = args.dpi; target.clear = true;
    target.d3d11.render_view = rtv; target.d3d11.depth_stencil_view = dsv;

    ui.render(target);  // warm-up (layout + font atlas upload)

    auto capture = [&](const std::string& snap) -> bool {
        ui.render(target);
        ctx->CopyResource(staging, color);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
            std::fprintf(stderr, "map staging failed\n"); return false; }
        std::vector<uint8_t> rgb((size_t)W * H * 3);
        for (int y = 0; y < H; ++y) {
            const uint8_t* row = (const uint8_t*)map.pData + (size_t)y * map.RowPitch;
            uint8_t* dst = rgb.data() + (size_t)y * W * 3;
            for (int x = 0; x < W; ++x) { const uint8_t* px = row + x * 4;
                dst[x*3+0] = px[2]; dst[x*3+1] = px[1]; dst[x*3+2] = px[0]; }
        }
        ctx->Unmap(staging, 0);
        const std::string path = args.out_dir + "/" + args.test + ".affineui." + snap + ".ppm";
        const bool ok = write_ppm(path, rgb.data(), W, H);
        if (ok) std::fprintf(stderr, "wrote %s\n", path.c_str());
        return ok;
    };
    auto click = [&](int x, int y) {
        affineui::Event e; e.pos = {x, y}; e.button = affineui::MouseButton::Left;
        e.type = affineui::EventType::MouseMove; ui.dispatch(e);
        e.type = affineui::EventType::MouseDown; ui.dispatch(e);
        e.type = affineui::EventType::MouseUp;   ui.dispatch(e);
    };
    auto hover = [&](int x, int y) {
        affineui::Event e; e.pos = {x, y}; e.type = affineui::EventType::MouseMove; ui.dispatch(e);
    };

    bool ok = true, took_any = false;
    for (const auto& st : args.steps) {
        switch (st.kind) {
            case Step::Click:    click(st.x, st.y); break;
            case Step::Hover:    hover(st.x, st.y); break;
            case Step::Wait:     break;  // no host clock here; animations are a TODO
            case Step::Snapshot: ok = capture(st.name) && ok; took_any = true; break;
        }
    }
    if (!took_any) ok = capture("default") && ok;

    ui.renderer().shutdown();
    release(staging); release(dsv); release(depth); release(rtv); release(color);
    release(ctx); release(dev);
    return ok ? 0 : 1;
}
