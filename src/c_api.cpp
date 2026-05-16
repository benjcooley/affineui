// Thin extern "C" surface mirroring the C++ API.
//
// Lets bindings from Rust / Zig / Odin / Python ctypes drive AffineUI
// without a C++ ABI dance. Functions are named affineui_<verb_object>;
// opaque handle types are `affineui_<type>*`.

#include "affineui/affineui.h"

extern "C" {

const char* affineui_version() {
    return AFFINEUI_VERSION_STRING;
}

int affineui_run_html(const char* html) {
    if (!html) return 1;
    return ::affineui::run(std::string_view{html});
}

// Additional handle-based functions land here as the engine surface
// solidifies. Pattern is one C function per C++ method we want to expose.

}  // extern "C"
