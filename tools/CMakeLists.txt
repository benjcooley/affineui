# Tools are independent helper executables (preview, benchmarks).
# Each is optional; subdirectories are only added when their source
# is present, so the directory can grow incrementally.

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui-preview/main.cpp")
    add_subdirectory(ui-preview)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmark/main.cpp")
    add_subdirectory(benchmark)
endif()

# ─── Amalgamation ────────────────────────────────────────────────────────────
# `tools/amalgamate.py` concatenates the modular sources into a single
# `affineui.h` + `affineui.cpp` pair under ${CMAKE_BINARY_DIR}/dist/. That
# two-file form is the default integration story for end users (see
# docs/DESIGN.md, "Packaging plan"). This target *produces* those files;
# it does not try to compile them — compiling the amalgamation is the
# downstream consumer's job, and doing it here would re-introduce the
# modular build's platform wiring (Apple ObjC for sokol_app, the GL
# header pick per platform, etc.) that the two-file release deliberately
# decouples from.

find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    set(_amalg_script "${CMAKE_CURRENT_SOURCE_DIR}/amalgamate.py")
    set(_amalg_outdir "${CMAKE_BINARY_DIR}/dist")
    set(_amalg_h      "${_amalg_outdir}/affineui.h")
    set(_amalg_cpp    "${_amalg_outdir}/affineui.cpp")

    # Glob the inputs that feed the amalgamator so re-running it is
    # gated on actual source changes. CONFIGURE_DEPENDS makes Ninja
    # re-glob on each build.
    file(GLOB_RECURSE _amalg_inputs CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/include/affineui/*.h"
        "${CMAKE_SOURCE_DIR}/src/*.h"
        "${CMAKE_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/*.c"
    )

    add_custom_command(
        OUTPUT  "${_amalg_h}" "${_amalg_cpp}"
        COMMAND "${Python3_EXECUTABLE}" "${_amalg_script}"
                --root "${CMAKE_SOURCE_DIR}"
                --out  "${_amalg_outdir}"
        DEPENDS "${_amalg_script}" ${_amalg_inputs}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Producing affineui.h + affineui.cpp two-file release"
        VERBATIM
    )

    add_custom_target(amalgamate DEPENDS "${_amalg_h}" "${_amalg_cpp}")
else()
    message(STATUS "Python3 interpreter not found; `amalgamate` target disabled")
endif()
