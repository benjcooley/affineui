affineui_add_sokol_example(bootstrap main.cpp)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap-4.6.2.min.css
    ${CMAKE_BINARY_DIR}/examples/bootstrap/bootstrap-4.6.2.min.css
    COPYONLY)
