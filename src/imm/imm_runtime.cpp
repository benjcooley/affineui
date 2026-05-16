// ImmRuntime — per-Document immediate-mode reconciler (Phase 2D:
// "clear and rebuild" via lexbor DOM mutation). Dumb reconcile
// ("set until fatal mismatch") is the next-step upgrade — same
// public surface, different body of `run_view_fn`.

#include "imm/imm_runtime.h"

#include "affineui/document.h"

#include <cstdio>
#include <cstring>

#if !defined(AFFINEUI_STUB_BUILD)
#    include <lexbor/dom/dom.h>
#    include <lexbor/html/html.h>
#endif

namespace affineui::detail {

ImmRuntime*& imm_active_runtime() {
    thread_local ImmRuntime* tls = nullptr;
    return tls;
}

ImmRuntime::ImmRuntime()  = default;
ImmRuntime::~ImmRuntime() {
    // Free any state slots we own. The user's T destructors run via
    // the stored function pointer.
    for (auto& [key, slot] : state_slots_) {
        if (slot.data) {
            if (slot.dtor) slot.dtor(slot.data);
            ::operator delete(slot.data);
        }
    }
}

void ImmRuntime::bind(Document* owner, lxb_html_document_t* doc) {
    owner_ = owner;
    doc_   = doc;
}

void ImmRuntime::set_view_fn(std::function<void()> fn) {
    view_fn_ = std::move(fn);
    dirty_   = true;   // first paint needs to run the fresh view fn
}

#if defined(AFFINEUI_STUB_BUILD)

void ImmRuntime::run_view_fn() {}
void* ImmRuntime::get_or_create_slot(std::uint64_t, std::size_t size, std::size_t,
                                     void (*ctor)(void*), void (*dtor)(void*)) {
    (void)dtor;
    void* p = ::operator new(size);
    ctor(p);
    return p;
}
void ImmRuntime::register_click(std::uint64_t, std::function<void()>) {}
bool ImmRuntime::invoke_click(std::string_view) { return false; }
lxb_dom_element_t* ImmRuntime::open_element(std::string_view, std::uint64_t, std::string_view) { return nullptr; }
void ImmRuntime::close_element() {}
void ImmRuntime::set_class(lxb_dom_element_t*, std::string_view) {}
void ImmRuntime::set_attr(lxb_dom_element_t*, std::string_view, std::string_view) {}
void ImmRuntime::append_text_to_current(std::string_view) {}

#else  // !AFFINEUI_STUB_BUILD

namespace {

inline const lxb_char_t* as_lxb(const char* p) {
    return reinterpret_cast<const lxb_char_t*>(p);
}
inline const lxb_char_t* as_lxb(std::string_view s) {
    return reinterpret_cast<const lxb_char_t*>(s.data());
}

// Render a hex-encoded 64-bit hash for use in element ids. Returns
// "aui-imm-{16-hex-chars}", deterministic per call-site.
std::string format_imm_id(std::uint64_t hash) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "aui-imm-%016llx",
                  static_cast<unsigned long long>(hash));
    return std::string(buf);
}

// Destroy every child of `node` (and its subtree) outright.
//
// Lexbor's CSS mutation hooks are fragile for our immediate-mode reset
// pattern: node destruction calls remove hooks and destroy hooks while
// walking the same styled subtree. For a full clear-and-rebuild pass we
// do not need those per-node detach notifications because no removed
// element survives. Temporarily disabling the remove/destroy hooks keeps
// the DOM unlink/free walk simple; the document's stylesheet state stays
// attached for the new tree and is reclaimed with the document.
void clear_children(lxb_dom_node_t* node) {
    if (!node) return;
    auto* doc = node->owner_document;
    auto* prev_remove = doc ? doc->ev_remove : nullptr;
    auto* prev_destroy = doc ? doc->ev_destroy : nullptr;
    if (doc) {
        doc->ev_remove = nullptr;
        doc->ev_destroy = nullptr;
    }
    while (auto* c = lxb_dom_node_first_child(node)) {
        lxb_dom_node_destroy_deep(c);
    }
    if (doc) {
        doc->ev_remove = prev_remove;
        doc->ev_destroy = prev_destroy;
    }
}

}  // namespace

void ImmRuntime::run_view_fn() {
    if (!view_fn_ || !doc_) return;

    auto* body = lxb_dom_interface_node(
        lxb_html_document_body_element(doc_));
    if (!body) return;

    clear_children(body);
    click_handlers_.clear();
    parent_stack_.clear();
    parent_stack_.push_back(body);

    auto*& tls = imm_active_runtime();
    auto*  prev = tls;
    tls = this;

    view_fn_();

    tls = prev;
    parent_stack_.clear();
    dirty_ = false;
}

void* ImmRuntime::get_or_create_slot(std::uint64_t key,
                                     std::size_t   size,
                                     std::size_t   /*align*/,
                                     void (*ctor)(void*),
                                     void (*dtor)(void*)) {
    auto it = state_slots_.find(key);
    if (it != state_slots_.end()) return it->second.data;
    void* p = ::operator new(size);
    if (ctor) ctor(p);
    state_slots_.emplace(key, ImmStateSlot{p, size, dtor});
    return p;
}

void ImmRuntime::register_click(std::uint64_t scope_hash,
                                std::function<void()> cb) {
    click_handlers_[scope_hash] = std::move(cb);
}

bool ImmRuntime::invoke_click(std::string_view elem_id) {
    // Element ids look like "aui-imm-{16-hex}". Parse the hex back
    // into the scope hash and look up.
    constexpr std::string_view prefix = "aui-imm-";
    if (elem_id.size() != prefix.size() + 16) return false;
    if (elem_id.substr(0, prefix.size()) != prefix) return false;
    std::uint64_t hash = 0;
    for (std::size_t i = 0; i < 16; ++i) {
        const char c = elem_id[prefix.size() + i];
        std::uint64_t v;
        if      (c >= '0' && c <= '9') v = static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') v = static_cast<std::uint64_t>(10 + c - 'a');
        else return false;
        hash = (hash << 4) | v;
    }
    auto it = click_handlers_.find(hash);
    if (it == click_handlers_.end()) return false;
    // Bind the active-runtime TLS while the handler runs so
    // `mark_state_mutated` calls from within `use_state` references
    // find us.
    auto*& tls  = imm_active_runtime();
    auto*  prev = tls;
    tls = this;
    it->second();
    tls = prev;
    // Pessimistically mark dirty after any click handler (Phase 3
    // upgrades to per-state-slot tracking).
    dirty_ = true;
    return true;
}

lxb_dom_element_t* ImmRuntime::open_element(std::string_view tag,
                                            std::uint64_t scope_hash,
                                            std::string_view classes) {
    if (parent_stack_.empty() || !doc_) return nullptr;
    auto* parent = parent_stack_.back();
    auto* elem = lxb_dom_document_create_element(
        lxb_dom_interface_document(doc_),
        as_lxb(tag), tag.size(),
        nullptr);
    if (!elem) return nullptr;

    // Set id + class BEFORE insert, so lexbor's ev_insert cascade sees
    // them and matches `.cls` / `#id` selectors on first attach.
    const auto id_str = format_imm_id(scope_hash);
    lxb_dom_element_set_attribute(
        elem,
        as_lxb("id"), 2,
        as_lxb(id_str.c_str()), id_str.size());

    if (!classes.empty()) {
        lxb_dom_element_set_attribute(
            elem,
            as_lxb("class"), 5,
            as_lxb(classes), classes.size());
    }

    auto* elem_node = lxb_dom_interface_node(elem);
    lxb_dom_node_insert_child(parent, elem_node);

    parent_stack_.push_back(elem_node);
    return elem;
}

void ImmRuntime::close_element() {
    if (parent_stack_.size() > 1) {
        parent_stack_.pop_back();
    }
}

void ImmRuntime::set_class(lxb_dom_element_t* el, std::string_view cls) {
    if (!el || cls.empty()) return;
    lxb_dom_element_set_attribute(
        el,
        as_lxb("class"), 5,
        as_lxb(cls), cls.size());
}

void ImmRuntime::set_attr(lxb_dom_element_t* el,
                          std::string_view name,
                          std::string_view value) {
    if (!el || name.empty()) return;
    lxb_dom_element_set_attribute(
        el,
        as_lxb(name), name.size(),
        as_lxb(value), value.size());
}

void ImmRuntime::append_text_to_current(std::string_view text) {
    if (parent_stack_.empty() || !doc_ || text.empty()) return;
    auto* parent = parent_stack_.back();
    auto* tn = lxb_dom_document_create_text_node(
        lxb_dom_interface_document(doc_),
        as_lxb(text), text.size());
    if (!tn) return;
    lxb_dom_node_insert_child(parent, lxb_dom_interface_node(tn));
}

#endif  // AFFINEUI_STUB_BUILD

}  // namespace affineui::detail
