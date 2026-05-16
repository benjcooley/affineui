// hello — the smallest sokol-based AffineUI app.
//
// This is the "drop into your sokol game" shape. There is no
// affineui::App and no affineui::run(html). Instead:
//
//   1. Link affineui::sokol (CMake) OR define AFFINEUI_WITH_SOKOL
//      before <affineui/affineui.h>. Either path pulls in
//      <affineui/sokol.h>, the adapter that bridges sokol_app's
//      event + callback model to the engine.
//   2. Construct one `affineui::Ui` — the friendly facade. It owns a
//      Document + Renderer internally.
//   3. Tell it the HTML/CSS content and any click handlers.
//   4. `affineui::sokol::wire(desc, ui)` installs sokol_app callbacks
//      that forward to the Ui. Equivalent to writing the event /
//      frame / cleanup callbacks yourself.
//
// For a *mixed* game (your game logic + a UI overlay), you'd skip
// `wire()` and write your own callbacks that call
// `affineui::sokol::dispatch(ui, ev)` + `affineui::sokol::render(ui)`
// at the right points around your own rendering.

#include <affineui/affineui.h>

#include <sokol_log.h>

int main() {
    affineui::Ui ui;
    ui.html(R"(
        <style>
            body { color: #cdd6f4; background-color: #1e1e2e;
                   font-size: 16px; }
            .card { background-color: #313244;
                    border: 1px solid #585b70;
                    padding: 16px 20px;
                    margin: 16px 0; }
            .clickable { background-color: #1e1e2e;
                         border: 1px solid #f38ba8; }
            h1 { color: #f38ba8; font-size: 28px;
                 margin: 0 0 8px 0; }
            p  { color: #a6adc8; font-size: 16px; margin: 0; }
        </style>
        <!-- cursor lives in the inline style because lexbor 2.4 has no
             `cursor` property; the cascade only sees it via the inline
             attribute scanner. Same gap as border-radius / gap. -->
        <div id="quit" class="card clickable"
             style="border-radius: 10px; cursor: pointer">
            <h1>Click to quit</h1>
            <p>Press the card to close the window. CSS selectors
               drive the click handler — this card has id="quit".</p>
        </div>
        <div class="card" style="border-radius: 10px">
            <h1>Plain card</h1>
            <p>This one doesn't react. Hit-testing is bounds-based;
               handlers are matched by CSS selectors.</p>
        </div>
    )");

    ui.on_click("#quit", []{ sapp_request_quit(); });

    sapp_desc desc{};
    desc.width        = 1024;
    desc.height       = 768;
    desc.window_title = "AffineUI — hello";
    desc.high_dpi     = true;
    desc.swap_interval = 1;
    desc.sample_count  = 1;
    desc.logger.func   = slog_func;
    affineui::sokol::wire(desc, ui);
    sapp_run(&desc);
    return 0;
}
