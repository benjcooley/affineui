// hello_sdl — the same content as the sokol hello, but plugged into
// an SDL2 + OpenGL game loop. This is the "real game engine"
// integration shape: SDL owns the window, the GL context, and the
// event loop; AffineUI is an overlay that paints + handles input
// through the affineui::sdl adapter.
//
// Wiring (full): ~30 lines of glue. The UI itself (HTML, CSS, click
// handlers) is identical to the sokol example.

#include <affineui/affineui.h>      // CMake target affineui::sdl adds AFFINEUI_WITH_SDL

#include <SDL.h>

#include <cstdio>

int main(int /*argc*/, char* /*argv*/[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // Ask for an OpenGL 3.3 core context — that's what NanoVG_GL3
    // (the Renderer's only backend today) needs.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "AffineUI — hello (SDL2)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1024, 768,
        SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(1);

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
        <div id="quit" class="card clickable"
             style="border-radius: 10px; cursor: pointer">
            <h1>Click to quit (SDL)</h1>
            <p>Same AffineUI engine, running inside SDL2 + OpenGL
               instead of sokol_app. Cursor + click handlers work
               identically through the affineui::sdl adapter.</p>
        </div>
        <div class="card" style="border-radius: 10px">
            <h1>Plain card</h1>
            <p>This one does nothing on click. Hit-testing is
               bounds-based; handlers are matched by CSS selectors.</p>
        </div>
    )");

    bool running = true;
    ui.on_click("#quit", [&]{ running = false; });

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) { running = false; continue; }
            // dispatch returns whether the UI consumed the event. In a
            // real game you'd skip your own handling in that case.
            if (affineui::sdl::dispatch(ui, ev)) continue;
            // your game's event handling would go here
        }
        // your game's per-frame rendering would go here, then the UI
        // composites on top.
        affineui::sdl::render(ui, window);
        SDL_GL_SwapWindow(window);
    }

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
