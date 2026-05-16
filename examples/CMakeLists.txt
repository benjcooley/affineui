# Example apps. Each subdirectory builds one executable.

function(affineui_add_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE affineui::affineui)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/${name}")
endfunction()

# Sokol-flavored example — same as above but also pulls in the sokol
# adapter (gets AFFINEUI_WITH_SOKOL + sokol_app.h include path).
function(affineui_add_sokol_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE affineui::sokol)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/${name}")
endfunction()

# SDL2 example. Skipped silently when SDL2 isn't available so the
# rest of the build still works.
function(affineui_add_sdl_example name)
    if(NOT TARGET affineui::sdl)
        return()
    endif()
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE affineui::sdl)
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/${name}")
endfunction()

add_subdirectory(00_hello)
add_subdirectory(01_bootstrap)
add_subdirectory(02_hello_sdl)
add_subdirectory(04_imm_counter)
