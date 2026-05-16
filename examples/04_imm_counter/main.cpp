// imm_counter — immediate-mode UI in a sokol-driven window.
//
// First-cut reconciler: "clear and rebuild." Each time a use_state
// reference is mutated (or invalidate() is called), the view fn re-
// runs and replaces the DOM body via lexbor mutation. State slots
// live on the Document, keyed by call-site hash, so they survive
// re-renders.
//
// The dumb-reconcile upgrade ("set until fatal mismatch") is a
// future optimization with the same API.

#include <affineui/affineui.h>

#include <sokol_log.h>

#include <string>

namespace ui = affineui::imm;

int main() {
    affineui::Ui aui;

    // User stylesheet — applied above the UA defaults. Persists
    // across re-renders.
    // NB: use `background-color:` longhand rather than `background:`
    // shorthand. Lexbor 2.4's CSS module exposes the longhand only —
    // shorthand declarations are parsed but silently dropped by our
    // cascade. Phase 3 adds a stylesheet pre-scanner for shorthands
    // lexbor can't parse (same fix unlocks `border-radius:` in CSS
    // rule blocks too).
    aui.css(R"(
        body   { background-color: #1e1e2e; color: #cdd6f4; }
        .card  { background-color: #313244; border: 1px solid #585b70;
                 padding: 24px; margin: 24px; }
        h1     { color: #f38ba8; font-size: 28px; margin: 0 0 8px 0; }
        p      { color: #cdd6f4; font-size: 32px; margin: 0 0 16px 0; }
        button { background-color: #f38ba8; color: #11111b;
                 font-size: 18px; padding: 12px 24px; border: 0; }
    )");

    aui.mount([&]() {
        if (auto _ = ui::div("card")) {
            auto count = ui::use_state(0);
            ui::h1().text("Counter");
            ui::p().text(std::to_string(count.get()));
            ui::button("Increment").on_click([count]() mutable {
                count = count.get() + 1;
            });
        }
    });

    sapp_desc desc{};
    desc.width        = 520;
    desc.height       = 360;
    desc.window_title = "AffineUI — counter (imm)";
    desc.high_dpi     = true;
    desc.swap_interval = 1;
    desc.sample_count  = 1;
    desc.logger.func   = slog_func;
    affineui::sokol::wire(desc, aui);
    sapp_run(&desc);
    return 0;
}
