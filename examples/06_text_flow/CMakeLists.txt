affineui_add_sokol_example(text_flow main.cpp)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/reference.html
    ${CMAKE_BINARY_DIR}/examples/text_flow/reference.html
    COPYONLY)
