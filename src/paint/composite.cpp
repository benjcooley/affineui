// Compositor: tiny hand-written GL pass that runs every frame, samples
// a layer's cached FBO texture, blits it into the default framebuffer.
// For idle UI this is the entire per-frame GPU cost.

#include "internal/compositor.h"

#if !defined(AFFINEUI_STUB_BUILD)

#    include <cstdint>
#    include <cstdio>

#    if defined(__APPLE__)
#        define GL_SILENCE_DEPRECATION
#        include <OpenGL/gl3.h>
#    elif defined(_WIN32)
#        define WIN32_LEAN_AND_MEAN
#        include <windows.h>
#        include <GL/gl.h>
#    else
#        define GL_GLEXT_PROTOTYPES
#        include <GL/gl.h>
#        include <GL/glext.h>
#    endif

namespace affineui::detail {

namespace {

constexpr const char* kVS = R"GLSL(
#version 330 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
out vec2 v_uv;
uniform int u_flip_y;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = (u_flip_y != 0) ? vec2(a_uv.x, 1.0 - a_uv.y) : a_uv;
}
)GLSL";

constexpr const char* kFS = R"GLSL(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
uniform float u_opacity;
void main() {
    vec4 c = texture(u_tex, v_uv);
    frag = c * u_opacity;
}
)GLSL";

GLuint compile_shader(GLenum kind, const char* src) {
    GLuint s = glCreateShader(kind);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[affineui] compositor shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[affineui] compositor program link failed: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

}  // namespace

Compositor::~Compositor() { shutdown(); }

bool Compositor::init() {
    if (ready_) return true;
    GLuint vs = compile_shader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    program_ = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!program_) return false;

    u_tex_     = glGetUniformLocation(program_, "u_tex");
    u_opacity_ = glGetUniformLocation(program_, "u_opacity");
    u_flip_y_  = glGetUniformLocation(program_, "u_flip_y");

    // Fullscreen NDC quad: two triangles, with UVs.
    // Position (-1..1) and UV (0..1).
    const float verts[] = {
        // pos       uv
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,
         1.f,  1.f,  1.f, 1.f,

        -1.f, -1.f,  0.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 1.f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                          reinterpret_cast<void*>(std::uintptr_t{0}));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4,
                          reinterpret_cast<void*>(std::uintptr_t{sizeof(float) * 2}));
    glBindVertexArray(0);

    ready_ = true;
    return true;
}

void Compositor::shutdown() {
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    ready_ = false;
}

void Compositor::blit_fullscreen(unsigned texture, float opacity, bool flip_y) {
    if (!ready_ || !texture) return;
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(u_tex_,     0);
    glUniform1f(u_opacity_, opacity);
    glUniform1i(u_flip_y_,  flip_y ? 1 : 0);

    // NanoVG_GL leaves GL_SCISSOR_TEST and other state in whatever
    // configuration its last draw needed. Force the compositor's
    // expected baseline before issuing the quad — otherwise a stale
    // scissor rect (or depth test) can silently swallow the entire
    // blit and the screen renders blank.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // premultiplied

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
}

}  // namespace affineui::detail

#else  // AFFINEUI_STUB_BUILD

namespace affineui::detail {
Compositor::~Compositor() {}
bool Compositor::init() { return false; }
void Compositor::shutdown() {}
void Compositor::blit_fullscreen(unsigned, float, bool) {}
}  // namespace affineui::detail

#endif
