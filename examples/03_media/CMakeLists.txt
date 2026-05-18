affineui_add_sokol_example(media main.cpp)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/reference.html
    ${CMAKE_BINARY_DIR}/examples/media/reference.html
    COPYONLY)
