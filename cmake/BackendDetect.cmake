# Resolve AFFINEUI_BACKEND ("auto" or an explicit name) into a concrete
# backend name, and expose helpers to wire compile defines / link libs.

function(affineui_detect_backend out requested)
    set(_chosen "${requested}")
    if(_chosen STREQUAL "auto")
        # Phase 1: NanoVG's bundled backend is GL3-only, so we use GL on
        # every platform. Once a Metal / D3D11 painter lands we revisit
        # this and pick per-platform defaults again.
        set(_chosen "gl")
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
