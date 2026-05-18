// bootstrap — real Bootstrap CSS loaded from a checked-in stylesheet.
//
// This demo intentionally does not include Bootstrap's JavaScript.
// Components that require JS in a browser are future AffineUI-native
// C++ behavior shims; the layout/paint baseline here is the CSS.

#include <affineui/affineui.h>

#include <sokol_log.h>

#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

namespace {

std::string read_first_existing(std::initializer_list<const char*> paths) {
    for (const char* path : paths) {
        std::ifstream file{path, std::ios::binary};
        if (!file.good()) continue;
        std::stringstream bytes;
        bytes << file.rdbuf();
        return bytes.str();
    }
    return {};
}

}  // namespace

int main() {
    affineui::Ui ui;

    ui.css(read_first_existing({
        "examples/01_bootstrap/bootstrap-4.6.2.min.css",
        "bootstrap-4.6.2.min.css",
    }));

    ui.html(R"HTML(
        <nav class="navbar navbar-expand navbar-dark bg-dark">
            <a class="navbar-brand" href="#">AffineUI</a>
            <div class="navbar-nav">
                <a class="nav-link" href="#">Docs</a>
                <a class="nav-link" href="#">Examples</a>
                <a class="nav-link" href="#">GitHub</a>
            </div>
        </nav>

        <main class="container py-4">
            <div class="jumbotron py-4">
                <h1 class="display-4">Bootstrap CSS in AffineUI</h1>
                <p class="lead">
                    This demo loads the real Bootstrap 4.6 stylesheet and
                    renders normal Bootstrap markup in the game UI pipeline.
                </p>
                <button class="btn btn-primary">Primary action</button>
                <button class="btn btn-secondary">Secondary</button>
                <button class="btn btn-outline-primary">Outline</button>
            </div>

            <div class="row">
                <div class="col">
                    <div class="card">
                        <div class="card-body">
                            <h5 class="card-title">Cards</h5>
                            <p class="card-text">
                                Bootstrap card classes, spacing utilities,
                                and button variants come from bootstrap.css.
                            </p>
                            <button class="btn btn-primary">Save</button>
                        </div>
                    </div>
                </div>

                <div class="col">
                    <div class="card">
                        <div class="card-body">
                            <h5 class="card-title">Pseudo-classes</h5>
                            <p class="card-text">
                                Hover, active, and focus selectors exercise
                                the same cascade path used by frameworks.
                            </p>
                            <button class="btn btn-outline-primary">Focus me</button>
                        </div>
                    </div>
                </div>
            </div>
        </main>
    )HTML");

    sapp_desc desc{};
    desc.width         = 1280;
    desc.height        = 800;
    desc.window_title  = "AffineUI — Bootstrap CSS";
    desc.high_dpi      = true;
    desc.swap_interval = 1;
    desc.sample_count  = 1;
    desc.logger.func   = slog_func;
    affineui::sokol::wire(desc, ui);
    sapp_run(&desc);
    return 0;
}
