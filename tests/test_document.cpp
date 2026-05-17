#include <doctest/doctest.h>

#include "affineui/document.h"
#include "affineui/painter.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

class RecordingPainter final : public affineui::Painter {
public:
    std::vector<affineui::Color> stroke_colors;

    void begin_frame(int, int, float) override {}
    void end_frame() override {}
    void fill_rect(const affineui::Rect&, affineui::Color) override {}
    void stroke_rect(const affineui::Rect&, affineui::Color color, float) override {
        stroke_colors.push_back(color);
    }
    void fill_rounded_rect(const affineui::Rect&, float, affineui::Color) override {}
    void stroke_rounded_rect(const affineui::Rect&, float, affineui::Color color, float) override {
        stroke_colors.push_back(color);
    }
    void fill_rounded_rect_varying(const affineui::Rect&, float, float, float, float, affineui::Color) override {}
    void stroke_rounded_rect_varying(const affineui::Rect&, float, float, float, float,
                                     affineui::Color color, float) override {
        stroke_colors.push_back(color);
    }
    std::uint32_t resolve_font(std::string_view, int, int, bool) override { return 1; }
    int measure_text(std::uint32_t, std::string_view text) override {
        return static_cast<int>(text.size()) * 8;
    }
    TextMetrics text_metrics(std::uint32_t) override { return {12.0f, 4.0f, 18.0f}; }
    void draw_text(std::uint32_t, const affineui::Point&, std::string_view,
                   affineui::Color) override {}
    affineui::Size measure_text_box(std::uint32_t, std::string_view text,
                                    float max_width, float) override {
        const int natural = static_cast<int>(text.size()) * 8;
        return {natural < static_cast<int>(max_width)
                    ? natural
                    : static_cast<int>(max_width),
                18};
    }
    void draw_text_box(std::uint32_t, const affineui::Point&, std::string_view,
                       affineui::Color, float, float) override {}
    std::uint32_t load_image(std::string_view) override { return 0; }
    affineui::Size image_size(std::uint32_t) override { return {}; }
    void draw_image(std::uint32_t, const affineui::Rect&, const affineui::Rect&) override {}
    void push_clip(const affineui::Rect&) override {}
    void pop_clip() override {}
};

bool same_color(affineui::Color a, affineui::Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool saw_stroke(const RecordingPainter& painter, affineui::Color color) {
    for (const auto& stroke : painter.stroke_colors) {
        if (same_color(stroke, color)) return true;
    }
    return false;
}

affineui::Point find_hovered_button(affineui::Document& doc) {
    for (int y = 0; y < 120; y += 2) {
        for (int x = 0; x < 320; x += 2) {
            affineui::Event move{};
            move.type = affineui::EventType::MouseMove;
            move.pos = {x, y};
            doc.dispatch(move);
            if (doc.hovered_info().tag == "button") {
                return {x, y};
            }
        }
    }
    return {-1, -1};
}

}  // namespace

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

TEST_CASE("focused button keeps higher-specificity recovered border color") {
    affineui::Document doc;
    RecordingPainter painter;

    doc.set_html(R"HTML(
        <style>
        body { margin: 0; padding: 0; }
        .btn {
            padding: 8px 16px;
            border-radius: 6px;
            border: 1px solid transparent;
        }
        .btn:focus { border-color: #212529; }
        .btn-primary {
            background-color: #0d6efd;
            color: #ffffff;
            border-color: #0d6efd;
        }
        </style>
        <button class="btn btn-primary">Save</button>
    )HTML");
    doc.layout(320, 0, &painter);

    const auto button_pos = find_hovered_button(doc);
    REQUIRE(button_pos.x >= 0);

    affineui::Event down{};
    down.type = affineui::EventType::MouseDown;
    down.button = affineui::MouseButton::Left;
    down.pos = button_pos;
    CHECK(doc.dispatch(down).redraw_requested);

    affineui::Event up{};
    up.type = affineui::EventType::MouseUp;
    up.button = affineui::MouseButton::Left;
    up.pos = button_pos;
    doc.dispatch(up);

    painter.stroke_colors.clear();
    doc.draw(painter);
    CHECK(saw_stroke(painter, affineui::Color::rgb(0x21, 0x25, 0x29)));
    CHECK_FALSE(saw_stroke(painter, affineui::Color::rgb(0x0d, 0x6e, 0xfd)));

    affineui::Event esc{};
    esc.type = affineui::EventType::KeyDown;
    esc.key = affineui::Key::Escape;
    CHECK(doc.dispatch(esc).redraw_requested);

    painter.stroke_colors.clear();
    doc.draw(painter);
    CHECK(saw_stroke(painter, affineui::Color::rgb(0x0d, 0x6e, 0xfd)));
}
