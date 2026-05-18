affineui_add_sokol_example(forms_focus main.cpp)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/reference.html
    ${CMAKE_BINARY_DIR}/examples/forms_focus/reference.html
    COPYONLY)
