#include <doctest/doctest.h>

#include "affineui/document.h"

TEST_CASE("default-constructed document has zero content size") {
    affineui::Document doc;
    auto sz = doc.content_size();
    CHECK(sz.width == 0);
    CHECK(sz.height == 0);
}

TEST_CASE("set_html accepts a string without crashing") {
    affineui::Document doc;
    doc.set_html("<h1>Hi</h1>");
    CHECK(true);
}

TEST_CASE("user stylesheet round-trips through set_user_stylesheet") {
    affineui::Document doc;
    doc.set_user_stylesheet("body { color: red; }");
    CHECK(true);
}

TEST_CASE("dispatch of a no-op event returns a quiescent result") {
    affineui::Document doc;
    affineui::Event ev{};
    auto r = doc.dispatch(ev);
    CHECK(r.redraw_requested == false);
    CHECK(r.invalidate_view == false);
}
