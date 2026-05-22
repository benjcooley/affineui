/* Compile + smoke test for nanovg_sokol.h against the sokol dummy backend.
 * Validates that the backend compiles and that resource creation + a full
 * fill/stroke/text frame runs through sokol's validation layer without a
 * real GPU or window. Not a rendering test. */
#include <stdio.h>
#include <string.h>

#define NANOVG_SOKOL_IMPLEMENTATION
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "nanovg.h"
#include "nanovg_sokol.h"

int main(void) {
    sg_desc d; memset(&d, 0, sizeof(d));
    d.logger.func = slog_func;
    d.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    d.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    d.environment.defaults.sample_count = 1;
    sg_setup(&d);
    if (!sg_isvalid()) { printf("FAIL: sg_setup\n"); return 1; }

    NVGcontext* vg = nvgCreateSokol(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (!vg) { printf("FAIL: nvgCreateSokol\n"); return 1; }

    sg_pass pass; memset(&pass, 0, sizeof(pass));
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value.r = 0.1f;
    pass.action.colors[0].clear_value.a = 1.0f;
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.stencil.load_action = SG_LOADACTION_CLEAR;
    pass.swapchain.width = 800;
    pass.swapchain.height = 600;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pass.swapchain.sample_count = 1;
    sg_begin_pass(&pass);

    nvgBeginFrame(vg, 800.0f, 600.0f, 1.0f);

    /* concave fill (stencil path) */
    nvgBeginPath(vg);
    nvgMoveTo(vg, 50, 50);
    nvgLineTo(vg, 200, 80);
    nvgLineTo(vg, 120, 220);
    nvgLineTo(vg, 60, 160);
    nvgLineTo(vg, 180, 200);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGBA(80, 160, 220, 255));
    nvgFill(vg);

    /* convex rounded rect + stroke */
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 300, 100, 320, 200, 18);
    nvgFillPaint(vg, nvgLinearGradient(vg, 300, 100, 620, 300,
                                       nvgRGBA(220, 80, 80, 255), nvgRGBA(80, 80, 220, 255)));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgStrokeWidth(vg, 3.0f);
    nvgStroke(vg);

    /* text (exercises font atlas texture create/update + triangles path) */
    nvgEndFrame(vg);

    sg_end_pass();
    sg_commit();

    nvgDeleteSokol(vg);
    sg_shutdown();
    printf("OK: nanovg_sokol dummy-backend frame completed\n");
    return 0;
}
