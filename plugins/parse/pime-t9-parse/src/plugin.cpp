/*
 * pime-t9-parse: accumulate a nine-key (2-9) or 14-key (a-n) code stream.
 *
 * State: { buf: text } — key codes typed so far, `1` separates syllables in
 * the nine-key grouping, max `max_codes` characters.
 * Workspace out: raw (code string, separators included) and input (same).
 * Workspace in: keymap action.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>
#include <pimeplug/t9.hpp>
#include <pimeplug/text.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 8192;
constexpr uint32_t kMaxBuffer = 32;

alignas(8) uint8_t g_scratch[kScratchBytes];

T9Grouping g_grouping = kT9Nine;
uint32_t g_max_codes = 24;

bool text_is(const char* text, uint32_t len, const char* expected) {
    return text && len == cstr_len(expected) &&
           Frame::equal(text, len, expected, len);
}

uint32_t load_buffer(const Frame& frame, char* buffer) {
    MapView state = MapView::of(frame.root.get("state"));
    const char* text = nullptr;
    uint32_t len = 0;
    if (!state.valid || !state.text("buf", &text, &len)) return 0;
    if (len > kMaxBuffer) len = kMaxBuffer;
    for (uint32_t i = 0; i < len; ++i) buffer[i] = text[i];
    return len;
}

/* Appends key codes. A `1` separator is kept only between codes and never
 * twice in a row. */
bool append(char* buffer, uint32_t* len, const char* text, uint32_t text_len) {
    bool changed = false;
    for (uint32_t i = 0; i < text_len && *len < g_max_codes; ++i) {
        char c = lower_ascii(text[i]);
        if (c == '1') {
            if (g_grouping != kT9Nine || *len == 0) continue;
            if (buffer[*len - 1] == '1') continue;
            buffer[(*len)++] = c;
            changed = true;
            continue;
        }
        if (!t9_is_code(g_grouping, c)) continue;
        buffer[(*len)++] = c;
        changed = true;
    }
    return changed;
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const char* buffer, uint32_t len, bool handled,
                    bool commit_phase) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(1);
    w.key("buf");
    /* On a commit the composition is gone, but the workspace still carries the
     * pre-commit code so translate and format can resolve the selection. */
    w.text(buffer, commit_phase ? 0 : len);

    w.key("workspace");
    w.map(2);
    w.key("raw");
    w.text(buffer, len);
    w.key("input");
    w.text(buffer, len);

    w.key("handled");
    w.boolean(handled);

    w.key("commands");
    if (commit_phase) {
        w.array(0);
    } else if (len > 0) {
        w.array(1);
        cmd_set_composition(w, buffer, len, len);
    } else if (handled) {
        w.array(2);
        cmd_set_composition(w, "", 0, 0);
        cmd_hide_candidates(w);
    } else {
        w.array(0);
    }

    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_grouping = kT9Nine;
    g_max_codes = 24;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    const char* grouping = nullptr;
    uint32_t grouping_len = 0;
    if (settings.text("grouping", &grouping, &grouping_len))
        t9_grouping_from_text(grouping, grouping_len, &g_grouping);
    uint64_t value = 0;
    if (settings.uint("max_codes", &value) && value >= 1 && value <= kMaxBuffer)
        g_max_codes = static_cast<uint32_t>(value);
    if (g_max_codes > kMaxBuffer) g_max_codes = kMaxBuffer;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    char buffer[kMaxBuffer];
    uint32_t len = load_buffer(frame, buffer);
    bool handled = false;

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_key = has_type && text_is(type, type_len, "key");
    const bool is_text = has_type && text_is(type, type_len, "text");
    const bool is_candidate =
        has_type && text_is(type, type_len, "candidate");

    /* `reset` cancels the composition: drop the buffer and tell the host to
     * finish the preedit and close the candidate bar. */
    if (has_type && text_is(type, type_len, "reset"))
        return respond(out, cap, out_len, buffer, 0, true, false);

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"),
              &action, &action_len);

    bool commit_phase = false;
    if (is_candidate) {
        commit_phase = true;
    } else if (is_key || is_text) {
        if (text_is(action, action_len, "backspace")) {
            if (len > 0) {
                --len;
                handled = true;
            }
        } else if (text_is(action, action_len, "escape")) {
            len = 0;
            handled = true;
        } else if (text_is(action, action_len, "space") ||
                   text_is(action, action_len, "enter")) {
            commit_phase = true;
        } else {
            const char* text = nullptr;
            uint32_t text_len = 0;
            if (frame.event_text(&text, &text_len))
                handled = append(buffer, &len, text, text_len);
        }
    }

    return respond(out, cap, out_len, buffer, len, handled, commit_phase);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t* in, uint32_t in_len,
                                      uint8_t* out, uint32_t cap,
                                      uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;
    char buffer[kMaxBuffer];
    const uint32_t len = load_buffer(frame, buffer);
    return respond(out, cap, out_len, buffer, len, false, false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_grouping = kT9Nine;
    g_max_codes = 24;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
