#pragma once

#ifndef AFFINEUI_VERSION_MAJOR
#    define AFFINEUI_VERSION_MAJOR 0
#endif
#ifndef AFFINEUI_VERSION_MINOR
#    define AFFINEUI_VERSION_MINOR 0
#endif
#ifndef AFFINEUI_VERSION_PATCH
#    define AFFINEUI_VERSION_PATCH 1
#endif

#define AFFINEUI_VERSION_STRINGIFY_(x)  #x
#define AFFINEUI_VERSION_STRINGIFY(x)   AFFINEUI_VERSION_STRINGIFY_(x)

#define AFFINEUI_VERSION_STRING                                              \
    AFFINEUI_VERSION_STRINGIFY(AFFINEUI_VERSION_MAJOR) "."                   \
    AFFINEUI_VERSION_STRINGIFY(AFFINEUI_VERSION_MINOR) "."                   \
    AFFINEUI_VERSION_STRINGIFY(AFFINEUI_VERSION_PATCH)

namespace affineui {

struct Version {
    int major = AFFINEUI_VERSION_MAJOR;
    int minor = AFFINEUI_VERSION_MINOR;
    int patch = AFFINEUI_VERSION_PATCH;
};

const char* version_string() noexcept;

}  // namespace affineui
