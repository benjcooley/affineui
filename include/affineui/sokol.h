#pragma once

// Sokol adapter — opt-in.
//
// Include via the umbrella header by defining the macro first:
//
//     #define AFFINEUI_WITH_SOKOL
//     #include <affineui/affineui.h>
//
// Or include this header directly *after* including sokol_app.h:
//
//     #include <sokol_app.h>
//     #include <affineui/sokol.h>
//
// What's in here:
//   • affineui::sokol::dispatch(Ui&, sapp_event*)
//       Translate a sokol_app event into affineui::Event and route it
//       through the Ui's hit-test + click-handler pipeline. Sets the
//       OS cursor synchronously inside the mouse event so Cocoa
//       honors it. Returns true if the UI consumed the event (you
//       should skip your game's handling of it in that case).
//
//   • affineui::sokol::render(Ui&)
//       Render the UI into the current sokol_gfx pass, querying
//       framebuffer dimensions + DPI from sokol_app itself. Call once
//       per sokol frame callback.
//
//   • affineui::sokol::wire(sapp_desc&, Ui&)
//       Wire frame_userdata_cb / event_userdata_cb / cleanup_userdata_cb
//       to default implementations that forward to the Ui. Use when
//       your app is *just* AffineUI (a tool / settings dialog / pure-
//       UI game) — for mixed games, write your own callbacks and call
//       dispatch/render explicitly.
//
// Threading: same as Ui — same thread that owns the GL context.

#include "affineui/ui.h"

#include <sokol_app.h>

namespace affineui::sokol {

// ── Cursor mapping ──────────────────────────────────────────────────

inline sapp_mouse_cursor cursor_to_sokol(int c) {
    switch (c) {
        case 1: return SAPP_MOUSECURSOR_POINTING_HAND;
        case 2: return SAPP_MOUSECURSOR_IBEAM;
        case 3: return SAPP_MOUSECURSOR_CROSSHAIR;
        case 4: return SAPP_MOUSECURSOR_RESIZE_ALL;
        case 5: return SAPP_MOUSECURSOR_NOT_ALLOWED;
        case 6: return SAPP_MOUSECURSOR_RESIZE_EW;
        case 7: return SAPP_MOUSECURSOR_RESIZE_NS;
        default: return SAPP_MOUSECURSOR_DEFAULT;
    }
}

// ── Event translation + dispatch ────────────────────────────────────

/// Translate a sokol_app event to affineui::Event. Mouse coords are
/// converted from framebuffer pixels (sokol's units) to CSS points
/// (Ui's units) using `sapp_dpi_scale()`.
inline Event translate(const sapp_event* ev) {
    Event out{};
    if (!ev) return out;
    switch (ev->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE:   out.type = EventType::MouseMove;  break;
        case SAPP_EVENTTYPE_MOUSE_DOWN:   out.type = EventType::MouseDown;  break;
        case SAPP_EVENTTYPE_MOUSE_UP:     out.type = EventType::MouseUp;    break;
        case SAPP_EVENTTYPE_MOUSE_SCROLL: out.type = EventType::MouseWheel; break;
        case SAPP_EVENTTYPE_MOUSE_LEAVE:
            out.type = EventType::MouseMove;
            out.pos  = Point{-1, -1};
            return out;
        default:
            return out;  // type stays None → caller skips
    }
    const float dpi = sapp_dpi_scale();
    const float d   = dpi > 0.0f ? dpi : 1.0f;
    out.pos.x = static_cast<int>(ev->mouse_x / d);
    out.pos.y = static_cast<int>(ev->mouse_y / d);
    if (ev->type == SAPP_EVENTTYPE_MOUSE_DOWN ||
        ev->type == SAPP_EVENTTYPE_MOUSE_UP) {
        switch (ev->mouse_button) {
            case SAPP_MOUSEBUTTON_LEFT:   out.button = MouseButton::Left;   break;
            case SAPP_MOUSEBUTTON_RIGHT:  out.button = MouseButton::Right;  break;
            case SAPP_MOUSEBUTTON_MIDDLE: out.button = MouseButton::Middle; break;
            default: break;
        }
    }
    if (ev->type == SAPP_EVENTTYPE_MOUSE_SCROLL) {
        out.wheel_dx = ev->scroll_x;
        out.wheel_dy = ev->scroll_y;
    }
    return out;
}

/// Forward a sokol_app event to the Ui's dispatch pipeline. Applies
/// the OS cursor synchronously (required on macOS — `[NSCursor set]`
/// must happen inside the mouseMoved: handler). Returns true if the
/// UI handled the event (click hit a registered handler), false
/// otherwise so the caller can route it to game logic.
inline bool dispatch(Ui& ui, const sapp_event* ev) {
    if (!ev) return false;
    const auto e = translate(ev);
    if (e.type == EventType::None) return false;
    const bool consumed = ui.dispatch(e);
    if (e.type == EventType::MouseMove) {
        sapp_set_mouse_cursor(cursor_to_sokol(ui.hovered_cursor()));
    }
    return consumed;
}

// ── Frame ───────────────────────────────────────────────────────────

/// Render the UI into the current pass. Queries dimensions + DPI from
/// sokol_app. Call once per sokol_app frame callback, anywhere inside
/// your `sg_begin_default_pass(...)` ... `sg_end_pass()` bracket.
inline void render(Ui& ui) {
    ui.render(sapp_width(), sapp_height(), sapp_dpi_scale());
}

// ── One-call wire-up ────────────────────────────────────────────────

namespace detail {
inline void cb_frame_(void* user) {
    auto& ui = *static_cast<Ui*>(user);
    affineui::sokol::render(ui);
}
inline void cb_event_(const sapp_event* ev, void* user) {
    auto& ui = *static_cast<Ui*>(user);
    affineui::sokol::dispatch(ui, ev);
}
inline void cb_cleanup_(void* user) {
    auto& ui = *static_cast<Ui*>(user);
    ui.renderer().shutdown();
}
}  // namespace detail

/// Configure `desc` so sokol_app drives the given Ui automatically:
/// frame + event + cleanup callbacks installed, `user_data` set to
/// `&ui`. After this, call `sapp_run(&desc)`.
///
/// Useful for "the app IS the UI" (tools, settings panels, debug
/// menus). For mixed games (game + UI overlay), write your own
/// callbacks and call `render(ui)` / `dispatch(ui, ev)` explicitly
/// from inside them.
inline void wire(sapp_desc& desc, Ui& ui) {
    desc.user_data           = &ui;
    desc.frame_userdata_cb   = detail::cb_frame_;
    desc.event_userdata_cb   = detail::cb_event_;
    desc.cleanup_userdata_cb = detail::cb_cleanup_;
}

}  // namespace affineui::sokol
