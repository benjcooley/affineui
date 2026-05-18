// text_flow — inline text, anchors, wrapping, and mixed flow coverage.

#include <affineui/affineui.h>

#include <sokol_log.h>

int main() {
    affineui::Ui ui;
    ui.html(R"HTML(
        <style>
        body { margin: 0; background: #ffffff; color: #212529;
               font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
        main { padding: 24px; max-width: 920px; }
        h1 { margin: 0 0 20px 0; font-size: 32px; }
        p { margin: 0 0 16px 0; line-height: 1.5; font-size: 18px; }
        a { color: #0d6efd; text-decoration: underline; }
        strong { font-weight: 700; }
        em { font-style: italic; }
        code { font-family: Menlo, Consolas, monospace; background: #f1f3f5;
               padding: 2px 4px; border-radius: 4px; }
        .flow-row { display: flex; gap: 12px; align-items: center;
                    border: 1px solid #dee2e6; padding: 12px; }
        .pill { display: inline-block; padding: 6px 10px; border-radius: 999px;
                background: #e7f1ff; color: #084298; }
        .anchor-card { display: block; margin-top: 16px; padding: 16px;
                       border: 1px solid #b6d4fe; border-radius: 6px;
                       text-decoration: none; color: #052c65; }
        .anchor-card:hover { background: #e7f1ff; }
        </style>
        <main>
            <h1>Text And Flow Compatibility</h1>
            <p>
                This paragraph exercises inline text with
                <strong>bold spans</strong>, <em>italic spans</em>,
                <code>inline code</code>, and an
                <a href="#target">anchor link</a> inside one wrapping
                text flow.
            </p>
            <p>
                Long text should wrap within the containing block while
                preserving line-height, margins, and the baseline rhythm
                that component libraries depend on for dense panels.
            </p>
            <div class="flow-row">
                <span class="pill">Inline pill</span>
                <span>Text next to an inline-block control should stay centered.</span>
                <button>Inline button</button>
            </div>
            <a class="anchor-card" id="target" href="#">
                Block anchor card with hover styling and inherited text.
            </a>
        </main>
    )HTML");

    sapp_desc desc{};
    desc.width         = 1100;
    desc.height        = 700;
    desc.window_title  = "AffineUI — text flow";
    desc.high_dpi      = true;
    desc.swap_interval = 1;
    desc.sample_count  = 1;
    desc.logger.func   = slog_func;
    affineui::sokol::wire(desc, ui);
    sapp_run(&desc);
    return 0;
}
