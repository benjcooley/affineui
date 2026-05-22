//
// NanoVG demo on sokol_gfx — visual test for the nanovg_sokol backend.
//
// Renders NanoVG's standard demo screen (example/demo.c) through the
// affineui_nanovg sokol_gfx backend (nanovg_sokol.h). This exercises the
// full feature surface — paths, linear/radial/box gradients, images,
// text + icons, scissor/clip, blurs, shadows, widgets — independent of
// any host engine.
//
// Run it from a directory whose ../example/ holds the demo assets
// (fonts + images), e.g. a build dir inside the fork.
//
// Keys: ESC quit, SPACE toggle "blow up" (zoom), P toggle premult bg.
//
#include <stdio.h>
#include <math.h>

#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_log.h"
#include "sokol_time.h"

#include "nanovg.h"
#define NANOVG_SOKOL_IMPLEMENTATION
#include "nanovg_sokol.h"

#include "demo.h"

static struct {
    NVGcontext* vg;
    DemoData    data;
    uint64_t    start;
    int         loaded;
    int         blowup;
    int         premult;
} g;

static void init(void) {
    sg_desc d = {0};
    d.environment = sglue_environment();
    d.logger.func = slog_func;
    sg_setup(&d);
    stm_setup();

    g.vg = nvgCreateSokol(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!g.vg) { fprintf(stderr, "nvgCreateSokol failed\n"); return; }
    if (loadDemoData(g.vg, &g.data) == -1) {
        fprintf(stderr, "loadDemoData failed — run from a dir whose ../example/ has the fonts+images\n");
    } else {
        g.loaded = 1;
    }
    g.start = stm_now();
}

static void frame(void) {
    const int   fbw = sapp_width();
    const int   fbh = sapp_height();
    const float px  = sapp_dpi_scale() > 0.0f ? sapp_dpi_scale() : 1.0f;
    const int   winW = (int)(fbw / px);
    const int   winH = (int)(fbh / px);
    const double t = stm_sec(stm_diff(stm_now(), g.start));

    sg_pass pass = {0};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    if (g.premult) {
        pass.action.colors[0].clear_value = (sg_color){0.0f, 0.0f, 0.0f, 0.0f};
    } else {
        pass.action.colors[0].clear_value = (sg_color){0.3f, 0.3f, 0.32f, 1.0f};
    }
    pass.action.depth.load_action   = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value   = 1.0f;
    pass.action.stencil.load_action = SG_LOADACTION_CLEAR;
    pass.action.stencil.clear_value = 0;
    pass.swapchain = sglue_swapchain();

    sg_begin_pass(&pass);
    if (g.vg && g.loaded) {
        nvgBeginFrame(g.vg, (float)winW, (float)winH, px);
        renderDemo(g.vg, (float)winW * 0.5f, (float)winH * 0.5f,
                   (float)winW, (float)winH, (float)t, g.blowup, &g.data);
        nvgEndFrame(g.vg);
    }
    sg_end_pass();
    sg_commit();
}

static void cleanup(void) {
    if (g.vg) {
        if (g.loaded) freeDemoData(g.vg, &g.data);
        nvgDeleteSokol(g.vg);
    }
    sg_shutdown();
}

static void event(const sapp_event* e) {
    if (e->type != SAPP_EVENTTYPE_KEY_DOWN) return;
    switch (e->key_code) {
        case SAPP_KEYCODE_ESCAPE: sapp_request_quit(); break;
        case SAPP_KEYCODE_SPACE:  g.blowup = !g.blowup; break;
        case SAPP_KEYCODE_P:      g.premult = !g.premult; break;
        default: break;
    }
}

int main(void) {
    sapp_desc d = {0};
    d.init_cb     = init;
    d.frame_cb    = frame;
    d.cleanup_cb  = cleanup;
    d.event_cb    = event;
    d.width       = 1000;
    d.height      = 600;
    d.window_title = "NanoVG demo (sokol_gfx)";
    d.logger.func = slog_func;
    sapp_run(&d);
    return 0;
}
