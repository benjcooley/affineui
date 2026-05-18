// forms_focus — form controls, focus rings, and text input coverage.

#include <affineui/affineui.h>

#include <sokol_log.h>

int main() {
    affineui::Ui ui;
    ui.html(R"HTML(
        <style>
        body { margin: 0; background: #f8f9fa; color: #212529;
               font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
        main { padding: 24px; max-width: 760px; }
        h1 { margin: 0 0 20px 0; font-size: 32px; }
        label { display: block; margin: 0 0 6px 0; font-weight: 600; }
        .field { margin-bottom: 16px; }
        input, textarea, select {
            display: block;
            width: 420px;
            padding: 8px 12px;
            font-size: 16px;
            line-height: 1.5;
            color: #212529;
            background-color: #fff;
            border: 1px solid #ced4da;
            border-radius: 4px;
        }
        textarea { height: 88px; }
        input:focus, textarea:focus, select:focus, button:focus {
            outline: 0;
            border-color: #86b7fe;
            box-shadow: 0 0 0 .25rem rgba(13,110,253,.25);
        }
        .actions { display: flex; gap: 8px; margin-top: 20px; }
        button {
            display: inline-block;
            padding: 8px 14px;
            border-radius: 4px;
            border: 1px solid #0d6efd;
            background: #0d6efd;
            color: #fff;
            font-size: 16px;
        }
        button.secondary {
            border-color: #6c757d;
            background: #6c757d;
        }
        </style>
        <main>
            <h1>Forms And Focus</h1>
            <div class="field">
                <label for="name">Text input</label>
                <input id="name" value="Editable text" placeholder="Name">
            </div>
            <div class="field">
                <label for="notes">Textarea</label>
                <textarea id="notes">Multi-line value</textarea>
            </div>
            <div class="field">
                <label for="plan">Select</label>
                <select id="plan">
                    <option>Basic</option>
                    <option>Pro</option>
                </select>
            </div>
            <div class="actions">
                <button>Primary</button>
                <button class="secondary">Secondary</button>
            </div>
        </main>
    )HTML");

    sapp_desc desc{};
    desc.width         = 1000;
    desc.height        = 720;
    desc.window_title  = "AffineUI — forms and focus";
    desc.high_dpi      = true;
    desc.swap_interval = 1;
    desc.sample_count  = 1;
    desc.logger.func   = slog_func;
    affineui::sokol::wire(desc, ui);
    sapp_run(&desc);
    return 0;
}
