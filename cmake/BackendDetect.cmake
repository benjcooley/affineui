# Resolve AFFINEUI_BACKEND ("auto" or an explicit name) into a concrete
# backend name, and expose helpers to wire compile defines / link libs.

function(affineui_detect_backend out requested)
    set(_chosen "${requested}")
    if(_chosen STREQUAL "auto")
        # NanoVG now renders through sokol_gfx (affineui_nanovg's
        # nanovg_sokol backend), so we pick the native backend per
        # platform. The hand-written shaders in nanovg_sokol.h currently
        # cover HLSL (D3D11) and GLSL (GL); Metal/MSL is a TODO, so macOS
        # stays on GL for now.
        if(WIN32)
            set(_chosen "d3d11")
        else()
            set(_chosen "gl")
        endif()
    endif()
    set(_valid metal d3d11 gl wgpu)
    if(NOT _chosen IN_LIST _valid)
        message(FATAL_ERROR
            "AFFINEUI_BACKEND='${requested}' is not one of: ${_valid}")
    endif()
    set(${out} ${_chosen} PARENT_SCOPE)
endfunction()

function(affineui_link_backend target backend)
    if(backend STREQUAL "metal")
        target_compile_definitions(${target} PUBLIC SOKOL_METAL AFFINEUI_BACKEND_METAL)
    elseif(backend STREQUAL "d3d11")
        target_compile_definitions(${target} PUBLIC SOKOL_D3D11 AFFINEUI_BACKEND_D3D11)
    elseif(backend STREQUAL "gl")
        target_compile_definitions(${target} PUBLIC SOKOL_GLCORE AFFINEUI_BACKEND_GL)
    elseif(backend STREQUAL "wgpu")
        target_compile_definitions(${target} PUBLIC SOKOL_WGPU AFFINEUI_BACKEND_WGPU)
    endif()
    # We call sapp_run() from our own main(); without this, sokol_app's
    # platform layer (esp. macOS NSApplication) expects a sokol_main()
    # entry point that we don't provide.
    target_compile_definitions(${target} PUBLIC SOKOL_NO_ENTRY)
endfunction()
