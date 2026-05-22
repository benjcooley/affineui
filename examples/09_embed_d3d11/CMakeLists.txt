# Embedded mode: a raw Win32 + D3D11 host that owns the device and the
# present, handing render-target views to AffineUI each frame. D3D11 only.
if(WIN32 AND AFFINEUI_BACKEND_RESOLVED STREQUAL "d3d11")
    affineui_add_example(embed_d3d11 main.cpp)
    target_link_libraries(embed_d3d11 PRIVATE d3d11 dxgi)
endif()
