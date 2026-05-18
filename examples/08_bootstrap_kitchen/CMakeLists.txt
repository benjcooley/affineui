affineui_add_sokol_example(bootstrap_kitchen main.cpp)

configure_file(
    ${CMAKE_SOURCE_DIR}/examples/01_bootstrap/bootstrap-4.6.2.min.css
    ${CMAKE_BINARY_DIR}/examples/bootstrap_kitchen/bootstrap-4.6.2.min.css
    COPYONLY)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/reference.html
    ${CMAKE_BINARY_DIR}/examples/bootstrap_kitchen/reference.html
    COPYONLY)
