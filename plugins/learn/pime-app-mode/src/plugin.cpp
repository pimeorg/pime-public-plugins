/*
 * pime-app-mode: remember the Chinese/English mode per foreground app.
 *
 * WeaveText parity (core/weave-engine/src/session.rs): the session stores the
 * input mode keyed by the application id, so switching back to an app restores
 * the mode it was left in.
 *
 * The plugin infers the observed mode from the active parse plugin's raw code:
 *   - pime-english-parse/raw non-empty            -> "en"
 *   - pinyin / wubi / cangjie / t9 raw non-empty  -> "zh"
 * A `focus` event or a `context` event with an app id restores the stored mode
 * without observing a keystroke.
 *
 * Workspace out: mode ("zh" / "en"), app (the app id, empty when absent).
 * There is no workspace input; the plugin only reads context and parse codes.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 4096;
constexpr uint32_t kMaxApp = 96;

alignas(8) uint8_t g_scratch[kScratchBytes];

bool g_enabled = true;
bool g_remember = true;
bool g_observe = true;
char g_default_mode[3] = "zh";

struct Mode {
    char app[kMaxApp];
    uint32_t app_len = 0;
    bool has_app = false;
    char mode[3];
    uint32_t mode_len = 2;
};

bool read_app(const Frame& frame, char* out, uint32_t* out_len) {
    const char* app = nullptr;
    uint32_t len = 0;
    if (!frame.context.text("app", &app, &len))
        if (!frame.context.text("app_id", &app, &len)) return false;
    if (len == 0 || len >= kMaxApp) return false;
    for (uint32_t i = 0; i < len; ++i) out[i] = app[i];
    out[len] = '\0';
    *out_len = len;
    return true;
}

bool active_language(const Frame& frame, const char** mode) {
    struct Source {
        const char* plugin;
        const char* key;
        const char* mode;
    };
    static const Source kSources[] = {
        {"pime-english-parse", "raw", "en"},
        {"pime-pinyin-parse", "raw", "zh"},
        {"pime-wubi-parse", "code", "zh"},
        {"pime-cangjie-parse", "code", "zh"},
        {"pime-t9-parse", "raw", "zh"},
    };
    for (const Source& source : kSources) {
        WorkspaceKey key = full_key(source.plugin, source.key);
        const char* text = nullptr;
        uint32_t len = 0;
        if (!span_text(frame.workspace_value(key.text), &text, &len)) continue;
        if (len > 0) {
            *mode = source.mode;
            return true;
        }
    }
    return false;
}

uint32_t store_key(const Mode& entry, char* out, uint32_t cap) {
    const char prefix[] = "app/";
    uint32_t n = 0;
    for (const char* p = prefix; *p && n < cap - 1; ++p) out[n++] = *p;
    for (uint32_t i = 0; i < entry.app_len && n < cap - 1; ++i)
        out[n++] = entry.app[i];
    out[n] = '\0';
    return n;
}

void load_mode(Mode* entry) {
    char key[kMaxApp + 8];
    const uint32_t key_len = store_key(*entry, key, sizeof(key));
    char value[8];
    uint32_t value_len = 0;
    bool found = false;
    if (!store_get(key, key_len, value, sizeof(value), &value_len, &found) ||
        !found) {
        for (uint32_t i = 0; i < 2; ++i) entry->mode[i] = g_default_mode[i];
        entry->mode_len = 2;
        return;
    }
    if (value_len == 2 && (Frame::equal(value, 2, "en", 2) ||
                           Frame::equal(value, 2, "zh", 2))) {
        entry->mode[0] = value[0];
        entry->mode[1] = value[1];
        entry->mode_len = 2;
        return;
    }
    for (uint32_t i = 0; i < 2; ++i) entry->mode[i] = g_default_mode[i];
    entry->mode_len = 2;
}

void store_mode(const Mode& entry) {
    if (!g_remember || !entry.has_app) return;
    char key[kMaxApp + 8];
    const uint32_t key_len = store_key(entry, key, sizeof(key));
    store_set(key, key_len, entry.mode, entry.mode_len);
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Mode& entry) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(2);
    w.key("mode");
    w.text(entry.mode, entry.mode_len);
    w.key("app");
    w.text(entry.app, entry.app_len);
    w.key("handled");
    w.boolean(false);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_enabled = true;
    g_remember = true;
    g_observe = true;
    g_default_mode[0] = 'z';
    g_default_mode[1] = 'h';
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    if (settings.boolean("remember", &value)) g_remember = value;
    if (settings.boolean("observe", &value)) g_observe = value;
    const char* mode = nullptr;
    uint32_t mode_len = 0;
    if (settings.text("default_mode", &mode, &mode_len) && mode_len == 2 &&
        (Frame::equal(mode, 2, "zh", 2) || Frame::equal(mode, 2, "en", 2))) {
        g_default_mode[0] = mode[0];
        g_default_mode[1] = mode[1];
    }
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    Mode entry;
    if (!g_enabled) {
        for (uint32_t i = 0; i < 2; ++i) entry.mode[i] = g_default_mode[i];
        entry.mode_len = 2;
        return respond(out, cap, out_len, entry);
    }

    entry.has_app = read_app(frame, entry.app, &entry.app_len);
    if (entry.has_app) {
        load_mode(&entry);
    } else {
        for (uint32_t i = 0; i < 2; ++i) entry.mode[i] = g_default_mode[i];
        entry.mode_len = 2;
    }

    /* A keystroke in a language overrides the remembered mode. */
    const char* observed = nullptr;
    if (g_observe && active_language(frame, &observed)) {
        entry.mode[0] = observed[0];
        entry.mode[1] = observed[1];
        entry.mode_len = 2;
        store_mode(entry);
    }

    return respond(out, cap, out_len, entry);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Mode entry;
    for (uint32_t i = 0; i < 2; ++i) entry.mode[i] = g_default_mode[i];
    entry.mode_len = 2;
    return respond(out, cap, out_len, entry);
}

PIME_EXPORT void pime_shutdown(void) {
    g_enabled = true;
    g_remember = true;
    g_observe = true;
    g_default_mode[0] = 'z';
    g_default_mode[1] = 'h';
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
