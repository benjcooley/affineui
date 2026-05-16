#include <doctest/doctest.h>

#include "affineui/imm.h"

namespace ui = affineui::imm;

TEST_CASE("CallSite::hash distinguishes different lines, matches identical inputs") {
    auto a = ui::CallSite{__FILE__, __LINE__, 0}.hash();
    auto b = ui::CallSite{__FILE__, __LINE__, 0}.hash();
    // Same file pointer, different lines → different hashes.
    CHECK(a != b);
    // Identical inputs → identical hash.
    ui::CallSite c{__FILE__, 42, 0};
    ui::CallSite d{__FILE__, 42, 0};
    CHECK(c.hash() == d.hash());
}

TEST_CASE("imm scope builders return active scopes") {
    auto s = ui::div("card");
    CHECK(static_cast<bool>(s) == true);
}

TEST_CASE("imm::invalidate / is_dirty don't throw") {
    ui::invalidate();
    CHECK(ui::is_dirty() == false);
}
