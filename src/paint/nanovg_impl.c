/*
 * NanoVG GL3 backend implementation TU.
 *
 * nanovg.c (the core) is added to the target_sources list in
 * cmake/Dependencies.cmake. This TU compiles the GL backend that
 * goes with it.
 *
 * Phase 1 ships GL3 on every platform; Metal and D3D11 backends land
 * later alongside the corresponding sokol_gfx wiring.
 */
#if !defined(AFFINEUI_STUB_BUILD) && !defined(AFFINEUI_HOST_PROVIDES_NANOVG)

#    if defined(_WIN32)
#        define WIN32_LEAN_AND_MEAN
#        define NOMINMAX
#        include <windows.h>
#    endif

/* Pick the right GL header per platform — NanoVG's GL backend needs
 * GL types/entrypoints available before nanovg_gl.h. sokol_app's
 * GLCORE backend will have created a compatible context by the time
 * this code runs.
 */
#    if defined(__APPLE__)
#        define GL_SILENCE_DEPRECATION
#        include <OpenGL/gl3.h>
#    elif defined(_WIN32)
#        include <GL/gl.h>
#    else
#        define GL_GLEXT_PROTOTYPES
#        include <GL/gl.h>
#        include <GL/glext.h>
#    endif

/* nanovg_gl.h references NVGcontext from nanovg.h — include it first. */
#    include "nanovg.h"

#    define NANOVG_GL3_IMPLEMENTATION
#    include "nanovg_gl.h"

/* nvgluCreateFramebuffer / nvgluBindFramebuffer / nvgluDeleteFramebuffer
 * implementations land here too — gated on NANOVG_GL_IMPLEMENTATION,
 * which the macro above sets. Layer (src/paint/layer.cpp) and the
 * app frame loop (src/app/app.cpp) need these symbols for the Phase
 * 2C rasterize-into-FBO step.
 */
#    include "nanovg_gl_utils.h"

#endif /* !STUB && !HOST_PROVIDES_NANOVG */
