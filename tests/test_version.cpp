#include <doctest/doctest.h>

#include "affineui/version.h"

TEST_CASE("version constants are wired through") {
    CHECK(::affineui::version_string() != nullptr);
    ::affineui::Version v;
    CHECK(v.major >= 0);
    CHECK(v.minor >= 0);
    CHECK(v.patch >= 0);
}
