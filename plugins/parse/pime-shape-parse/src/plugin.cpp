/*
 * pime-shape-parse: detect the u-mode (component lookup) and backtick glyph
 * filter prefixes in the pinyin raw code.
 *
 * WeaveText parity (weave-dict/shape.rs): typing `u` starts component lookup
 * ("umumu" -> 木+木), typing a backtick starts a glyph filter over the current
 * candidates ("`mu" -> characters in the 木 group).
 *
 * This plugin only classifies the prefix and exposes the remaining code:
 *   - `plugin/pime-shape-parse/mode` = "shape" | "glyph" | ""
 *   - `plugin/pime-shape-parse/code` = raw code without the prefix
 * The actual lookup lives in pime-shape-translate (translate role).
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 4096;
constexpr uint32_t kMaxCode = 64;

alignas(8) uint8_t g_scratch[kScratchBytes];
bool g_enabled = true;

uint32_t read_raw(const Frame& frame, const char** out) {
    static const char* const kSources[] = {
        "pime-pinyin-parse", "pime-wubi-parse", "pime-cangjie-parse",
        "pime-t9-parse", "pime-english-parse",
    };
    static const char* const kKeys[] = {"raw", "code", "code", "raw", "raw"};
    for (uint32_t i = 0; i < 5; ++i) {
        WorkspaceKey key = full_key(kSources[i], kKeys[i]);
        uint32_t len = 0;
        if (!span_text(frame.workspace_value(key.text), out, &len)) continue;
        if (len > 0) return len;
    }
    *out = nullptr;
    return 0;
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const char* mode, const char* code, uint32_t code_len) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(2);
    w.key("mode");
    w.text(mode);
    w.key("code");
    w.text(code, code_len);
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
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    const char* raw = nullptr;
    const uint32_t raw_len = g_enabled ? read_raw(frame, &raw) : 0;
    if (raw_len >= 1 && raw[0] == 'u' && raw_len <= kMaxCode + 1)
        return respond(out, cap, out_len, "shape", raw + 1, raw_len - 1);
    if (raw_len >= 1 && raw[0] == '`' && raw_len <= kMaxCode + 1)
        return respond(out, cap, out_len, "glyph", raw + 1, raw_len - 1);

    /* Keymap-style backtick without text: start the glyph filter on the
     * current candidate list (empty code means "use the first candidate"). */
    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"), &action,
              &action_len);
    if (g_enabled && text_equal(action, action_len, "backtick"))
        return respond(out, cap, out_len, "glyph", "", 0);

    return respond(out, cap, out_len, "", "", 0);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, "", "", 0);
}

PIME_EXPORT void pime_shutdown(void) { g_enabled = true; }

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
