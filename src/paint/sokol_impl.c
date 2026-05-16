/*
 * Single TU that defines sokol's implementation symbols.
 *
 * Phase 1 only needs sokol_app — we use NanoVG with its native GL3
 * backend, talking directly to the OpenGL context that sokol_app
 * creates. sokol_gfx will land here in Phase 4 when a Metal/D3D11
 * painter ships.
 *
 * Backend (SOKOL_GLCORE / SOKOL_METAL / SOKOL_D3D11) comes in via
 * affineui_link_backend(); see cmake/BackendDetect.cmake.
 */
#if !defined(AFFINEUI_STUB_BUILD) && !defined(AFFINEUI_HOST_PROVIDES_SOKOL)

#    if defined(__APPLE__)
#        define _DARWIN_C_SOURCE
#    endif

#    define SOKOL_IMPL
#    include "sokol_log.h"
#    include "sokol_app.h"

#endif
