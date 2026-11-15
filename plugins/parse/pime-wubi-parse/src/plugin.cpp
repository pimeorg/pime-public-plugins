/*
 * pime-wubi-parse: accumulate a Wubi 86 letter code and own the composition
 * lifecycle.
 *
 * State: { buf: text } — the code letters typed so far.
 * Workspace out: code (text) — empty once the code has been pushed.
 * Workspace in: keymap action.
 *
 * Two lifecycle rules need the dictionary and must therefore live here rather
 * than in the translate plugin: only the plugin that owns the state can clear
 * it in the same frame it commits.
 *
 *   - 四码唯一顶屏: reaching four letters with exactly one exact match commits
 *     it immediately.
 *   - 满码再敲: a fifth letter pushes the best exact match (or drops a code
 *     with no match) and starts a new composition.
 *
 * `z` starts a pinyin reverse lookup; that code may grow past four letters and
 * is never pushed.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>
#include <pimeplug/dict.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxCode = 16;
constexpr uint32_t kMaxDictBytes = 65536;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_dict[kMaxDictBytes];
uint32_t g_dict_len = 0;
bool g_dict_loaded = false;

uint32_t g_max_code = 4;
uint32_t g_max_pinyin = 16;
bool g_auto_commit = true;
bool g_pinyin_lookup = true;

bool text_is(const char* text, uint32_t len, const char* expected) {
    return text && len == cstr_len(expected) &&
           Frame::equal(text, len, expected, len);
}

bool load_dict() {
    if (g_dict_loaded) return g_dict_len > 0;
    g_dict_loaded = true;
    AssetHandle handle;
    if (!asset_open("wubi.dict", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 && size + 64 <= sizeof(g_dict);
    if (ok) {
        uint32_t got = 0;
        ok = asset_read(handle, 0, size, g_dict, sizeof(g_dict), &got) && got > 0;
        if (ok) g_dict_len = got;
    }
    asset_close(handle);
    return ok;
}

uint32_t load_buffer(const Frame& frame, char* buffer) {
    MapView state = MapView::of(frame.root.get("state"));
    const char* text = nullptr;
    uint32_t len = 0;
    if (!state.valid || !state.text("buf", &text, &len)) return 0;
    if (len > kMaxCode) len = kMaxCode;
    for (uint32_t i = 0; i < len; ++i) buffer[i] = text[i];
    return len;
}

/* Visits exact matches for `code`, keeping the most frequent entry. */
bool best_exact(const char* code, uint32_t len, uint32_t* matches,
                const char** text, uint32_t* text_len, uint32_t* freq) {
    if (!load_dict()) return false;
    Dict dict;
    dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);
    uint32_t count = 0;
    uint32_t best = 0;
    const char* best_text = nullptr;
    uint32_t best_len = 0;
    dict.match(code, len, [&](const DictEntry& entry) {
        ++count;
        if (!best_text || entry.freq > best) {
            best = entry.freq;
            best_text = entry.text;
            best_len = entry.text_len;
        }
    });
    if (matches) *matches = count;
    if (text) *text = best_text;
    if (text_len) *text_len = best_len;
    if (freq) *freq = best;
    return best_text != nullptr;
}

bool is_pinyin_code(const char* buffer, uint32_t len) {
    return g_pinyin_lookup && len > 0 && buffer[0] == 'z';
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const char* buffer, uint32_t len, bool handled,
                    bool commit_phase, const char* commit_text,
                    uint32_t commit_len) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(1);
    w.key("buf");
    /* On a selection commit the composition is gone, but the workspace still
     * carries the pre-commit code so format can resolve the selection. A push
     * keeps composing the new code. */
    w.text(buffer, commit_phase ? 0 : len);

    w.key("workspace");
    w.map(1);
    w.key("code");
    /* An auto-commit leaves an empty buffer, so the code is consumed with it;
     * a push writes the new code. */
    w.text(buffer, len);

    w.key("handled");
    w.boolean(commit_text ? true : handled);

    w.key("commands");
    if (commit_text) {
        /* A push commits the old code and keeps composing the new one; a
         * unique four-code commit leaves nothing behind. */
        w.array(len > 0 ? 2 : 1);
        cmd_commit(w, commit_text, commit_len);
        if (len > 0) cmd_set_composition(w, buffer, len, len);
    } else if (commit_phase) {
        w.array(0);
    } else if (len > 0) {
        w.array(1);
        w.map(3);
        w.key("type");
        w.text("set_composition");
        w.key("text");
        w.text(buffer, len);
        w.key("cursor");
        w.u64(len);
    } else if (handled) {
        /* The composition is gone: finish it in the editor and drop the bar. */
        w.array(2);
        cmd_set_composition(w, "", 0, 0);
        cmd_hide_candidates(w);
    } else {
        w.array(0);
    }

    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

/* Pushes the current code: best exact match when there is one, otherwise the
 * code is dropped (never committing letters). */
bool push_code(const char* buffer, uint32_t len, const char** text,
               uint32_t* text_len) {
    uint32_t matches = 0;
    const char* best = nullptr;
    uint32_t best_len = 0;
    if (!best_exact(buffer, len, &matches, &best, &best_len, nullptr))
        return false;
    *text = best;
    *text_len = best_len;
    return true;
}

struct AppendResult {
    bool changed = false;
    const char* commit_text = nullptr;
    uint32_t commit_len = 0;
};

/* Appends lowercase letters. In pinyin mode the code may exceed four letters;
 * otherwise a full code is pushed before the next letter is taken. */
AppendResult append(char* buffer, uint32_t* len, const char* text,
                    uint32_t text_len) {
    AppendResult result;
    for (uint32_t i = 0; i < text_len; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c < 'a' || c > 'z') continue;

        const bool pinyin = is_pinyin_code(buffer, *len);
        const uint32_t limit = pinyin ? g_max_pinyin : g_max_code;
        if (*len >= limit) {
            if (!g_auto_commit || pinyin) break;
            const char* pushed = nullptr;
            uint32_t pushed_len = 0;
            if (push_code(buffer, *len, &pushed, &pushed_len)) {
                result.commit_text = pushed;
                result.commit_len = pushed_len;
            }
            *len = 0;
        }
        buffer[(*len)++] = c;
        result.changed = true;
    }
    return result;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_max_code = 4;
    g_max_pinyin = 16;
    g_auto_commit = true;
    g_pinyin_lookup = true;
    g_dict_loaded = false;
    g_dict_len = 0;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t value = 0;
    if (settings.uint("max_code", &value) && value >= 1 && value <= kMaxCode)
        g_max_code = static_cast<uint32_t>(value);
    if (settings.uint("max_pinyin", &value) && value >= 1 && value <= kMaxCode)
        g_max_pinyin = static_cast<uint32_t>(value);
    bool flag = true;
    if (settings.boolean("auto_commit", &flag)) g_auto_commit = flag;
    if (settings.boolean("pinyin_lookup", &flag)) g_pinyin_lookup = flag;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    char buffer[kMaxCode];
    uint32_t len = load_buffer(frame, buffer);
    bool handled = false;
    const char* commit_text = nullptr;
    uint32_t commit_len = 0;

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_key = has_type && text_is(type, type_len, "key");
    const bool is_text = has_type && text_is(type, type_len, "text");
    const bool is_candidate =
        has_type && text_is(type, type_len, "candidate");

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"),
              &action, &action_len);

    bool commit_phase = false;
    if (is_candidate) {
        /* The candidate is being committed: clear the state, but keep the code
         * in the workspace so format can resolve the selection. */
        commit_phase = true;
    } else if (is_key || is_text) {
        if (text_is(action, action_len, "backspace")) {
            /* Empty buffer: let the editor delete the committed text. */
            if (len > 0) {
                --len;
                handled = true;
            }
        } else if (text_is(action, action_len, "escape")) {
            len = 0;
            handled = true;
        } else if (text_is(action, action_len, "space") ||
                   text_is(action, action_len, "enter")) {
            /* format commits the highlighted candidate on these keys. */
            commit_phase = true;
        } else {
            const char* text = nullptr;
            uint32_t text_len = 0;
            if (frame.event_text(&text, &text_len)) {
                AppendResult appended = append(buffer, &len, text, text_len);
                handled = appended.changed;
                commit_text = appended.commit_text;
                commit_len = appended.commit_len;
            }
        }
    }

    /* 四码唯一顶屏: a full, non-pinyin code with a single exact match commits
     * without waiting for a selection key. */
    if (!commit_text && !commit_phase && handled && g_auto_commit &&
        !is_pinyin_code(buffer, len) && len == g_max_code) {
        uint32_t matches = 0;
        const char* best = nullptr;
        uint32_t best_len = 0;
        if (best_exact(buffer, len, &matches, &best, &best_len, nullptr) &&
            matches == 1) {
            commit_text = best;
            commit_len = best_len;
            len = 0;
        }
    }

    return respond(out, cap, out_len, buffer, len, handled, commit_phase,
                   commit_text, commit_len);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t* in, uint32_t in_len,
                                      uint8_t* out, uint32_t cap,
                                      uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;
    char buffer[kMaxCode];
    const uint32_t len = load_buffer(frame, buffer);
    return respond(out, cap, out_len, buffer, len, false, false, nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) {
    g_max_code = 4;
    g_max_pinyin = 16;
    g_auto_commit = true;
    g_pinyin_lookup = true;
    g_dict_loaded = false;
    g_dict_len = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
