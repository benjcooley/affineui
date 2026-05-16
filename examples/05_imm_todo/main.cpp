// imm_todo — todo list on top of imm-mode + dumb-reconcile.
//
// Exercises:
//   - use_state with a non-trivial T (std::vector<TodoItem>)
//   - a for-loop in the view fn rendering a variable-length list
//   - per-item click handlers that capture the item index
//   - text-only updates (toggling "done" rewrites the row's class)
//
// Known limit: every iteration of the for-loop invokes the same
// call-site, so all items share the same `aui-imm-{hash}` id. The
// reconciler's cursor walks them in order, so append-only and
// in-place toggle work cleanly. Insertion or deletion in the
// MIDDLE of the list shifts state to the wrong row — the proper
// fix is per-iteration keys (imm::Scope::key, currently a stub).

#include <affineui/affineui.h>

#include <sokol_log.h>

#include <string>
#include <vector>

namespace ui = affineui::imm;

struct TodoItem {
    std::string text;
    bool        done{false};
};

int main() {
    affineui::Ui aui;
    aui.css(R"(
        body { font-family: -apple-system, system-ui, sans-serif;
               background-color: #f8f9fa; color: #212529;
               padding: 32px; }
        h1   { font-size: 28px; font-weight: 600;
               margin: 0 0 16px 0; }
        .card { background-color: #ffffff;
                border: 1px solid #dee2e6;
                border-radius: 8px;
                padding: 16px 20px;
                margin: 0 0 16px 0; }
        .toolbar { margin-bottom: 16px; }
        .toolbar .btn { margin-right: 8px; }
        .btn { display: inline-block;
               padding: 6px 14px;
               border-radius: 6px;
               border: 1px solid #0d6efd;
               background-color: #0d6efd;
               color: #ffffff;
               font-size: 14px; }
        .btn:hover  { background-color: #0b5ed7; }
        .btn:active { background-color: #0a58ca; }
        .btn-ghost { background-color: #ffffff; color: #0d6efd; }
        .btn-ghost:hover  { background-color: #e7f1ff; }
        .btn-ghost:active { background-color: #cfe2ff; }

        .item       { padding: 6px 0; }
        .item-text  { font-size: 16px; color: #212529; }
        .done .item-text { color: #6c757d; }
    )");

    aui.mount([&]() {
        auto state = ui::use_state(std::vector<TodoItem>{
            {"Read the dumb-reconcile commit", false},
            {"Add a todo demo",                true},
            {"Wire keyboard for inputs",       false},
        });
        std::vector<TodoItem>& items = state.get();
        // Per-render scratch — the "next task number" for the Add
        // button. Pure derived state from the vector size.
        const auto next_label = "Task " + std::to_string(
            static_cast<int>(items.size()) + 1);

        if (auto _ = ui::div("card")) {
            ui::h1().text("Todos");

            if (auto _2 = ui::div("toolbar")) {
                ui::button("Add " + next_label, "btn").on_click([state, next_label] {
                    auto s = state;  // copy the StateRef into the closure
                    s.update([&](auto& v) {
                        v.push_back({next_label, false});
                    });
                });
                if (!items.empty()) {
                    ui::button("Pop", "btn btn-ghost").on_click([state] {
                        auto s = state;
                        s.update([](auto& v) { v.pop_back(); });
                    });
                }
            }

            for (std::size_t i = 0; i < items.size(); ++i) {
                const auto& it = items[i];
                // Per-iteration key keeps each row's element ids unique
                // (otherwise every iteration of the loop emits the same
                // call-site hash and clicks all route to the same handler).
                ui::key(static_cast<std::uint64_t>(i));
                if (auto _2 = ui::div(it.done ? "item done" : "item")) {
                    ui::button(it.done ? "[x]" : "[ ]", "btn btn-ghost")
                        .on_click([state, i] {
                            auto s = state;
                            s.update([i](auto& v) {
                                if (i < v.size()) v[i].done = !v[i].done;
                            });
                        });
                    ui::span("item-text").text(it.text);
                }
            }
        }
    });

    sapp_desc desc{};
    desc.width        = 640;
    desc.height       = 480;
    desc.window_title = "AffineUI — imm todo";
    desc.high_dpi     = true;
    desc.swap_interval = 1;
    desc.sample_count  = 1;
    desc.logger.func   = slog_func;
    affineui::sokol::wire(desc, aui);
    sapp_run(&desc);
    return 0;
}
