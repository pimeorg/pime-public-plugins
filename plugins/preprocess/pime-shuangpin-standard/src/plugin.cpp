/*
 * pime-shuangpin-standard: two-key syllable composition for the four common
 * schemes (Xiaohe, Ziranma, Microsoft, Sogou).
 *
 * State: { half: text } — the pending first key.
 * Workspace out: consumed (bool), syllable (text).
 *
 * The second key selects a final; the pair expands to the syllable spelling
 * the pinyin parser and translator already consume. Zero-initial syllables
 * follow each scheme's rule (natural double/key form, or the Microsoft/Sogou
 * `o`+key rule).
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/pinyin.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
alignas(8) uint8_t g_scratch[kScratchBytes];

struct FinalEntry {
    char key;
    const char* finals[3];
};

struct Scheme {
    const FinalEntry* finals;
    bool o_key_zero;  /* Microsoft/Sogou */
};

/* Xiaohe (Flypy). */
const FinalEntry kXiaohe[] = {
    {'q', {"iu"}},        {'w', {"ei"}},          {'e', {"e"}},
    {'r', {"uan"}},       {'t', {"ue", "ve"}},    {'y', {"un"}},
    {'u', {"u"}},         {'i', {"i"}},           {'o', {"uo", "o"}},
    {'p', {"ie"}},        {'a', {"a"}},           {'s', {"iong", "ong"}},
    {'d', {"ai"}},        {'f', {"en"}},          {'g', {"eng"}},
    {'h', {"ang"}},       {'j', {"an"}},          {'k', {"ing", "uai"}},
    {'l', {"iang", "uang"}}, {'z', {"ou"}},       {'x', {"ia", "ua"}},
    {'c', {"ao"}},        {'v', {"ui", "v"}},     {'b', {"in"}},
    {'n', {"iao"}},       {'m', {"ian"}},         {0, {}},
};

/* Ziranma. */
const FinalEntry kZiranma[] = {
    {'q', {"iu"}},        {'w', {"ia", "ua"}},    {'e', {"e"}},
    {'r', {"uan"}},       {'t', {"ue", "ve"}},    {'y', {"uai", "ing"}},
    {'u', {"u"}},         {'i', {"i"}},           {'o', {"uo", "o"}},
    {'p', {"un"}},        {'a', {"a"}},           {'s', {"iong", "ong"}},
    {'d', {"iang", "uang"}}, {'f', {"en"}},       {'g', {"eng"}},
    {'h', {"ang"}},       {'j', {"an"}},          {'k', {"ao"}},
    {'l', {"ai"}},        {'z', {"ei"}},          {'x', {"ie"}},
    {'c', {"iao"}},       {'v', {"ui", "v"}},     {'b', {"ou"}},
    {'n', {"in"}},        {'m', {"ian"}},         {0, {}},
};

/* Microsoft: ing on ';' and er on 'r'. */
const FinalEntry kMicrosoft[] = {
    {'q', {"iu"}},        {'w', {"ia", "ua"}},    {'e', {"e"}},
    {'r', {"uan", "er"}}, {'t', {"ue", "ve"}},    {'y', {"uai", "v"}},
    {'u', {"u"}},         {'i', {"i"}},           {'o', {"uo", "o"}},
    {'p', {"un"}},        {'a', {"a"}},           {'s', {"iong", "ong"}},
    {'d', {"iang", "uang"}}, {'f', {"en"}},       {'g', {"eng"}},
    {'h', {"ang"}},       {'j', {"an"}},          {'k', {"ao"}},
    {'l', {"ai"}},        {';', {"ing"}},         {'z', {"ei"}},
    {'x', {"ie"}},        {'c', {"iao"}},         {'v', {"ui", "ue", "ve"}},
    {'b', {"ou"}},        {'n', {"in"}},          {'m', {"ian"}},
    {0, {}},
};

/* Sogou: same as Microsoft without ue/ve on 'v'. */
const FinalEntry kSogou[] = {
    {'q', {"iu"}},        {'w', {"ia", "ua"}},    {'e', {"e"}},
    {'r', {"uan", "er"}}, {'t', {"ue", "ve"}},    {'y', {"uai", "v"}},
    {'u', {"u"}},         {'i', {"i"}},           {'o', {"uo", "o"}},
    {'p', {"un"}},        {'a', {"a"}},           {'s', {"iong", "ong"}},
    {'d', {"iang", "uang"}}, {'f', {"en"}},       {'g', {"eng"}},
    {'h', {"ang"}},       {'j', {"an"}},          {'k', {"ao"}},
    {'l', {"ai"}},        {';', {"ing"}},         {'z', {"ei"}},
    {'x', {"ie"}},        {'c', {"iao"}},         {'v', {"ui"}},
    {'b', {"ou"}},        {'n', {"in"}},          {'m', {"ian"}},
    {0, {}},
};

const Scheme kSchemes[] = {
    {kXiaohe, false},
    {kZiranma, false},
    {kMicrosoft, true},
    {kSogou, true},
};

uint32_t g_scheme = 0;

const Scheme& scheme() { return kSchemes[g_scheme]; }

int scheme_index(const char* name, uint32_t len) {
    if (text_equal(name, len, "xiaohe")) return 0;
    if (text_equal(name, len, "ziranma")) return 1;
    if (text_equal(name, len, "microsoft")) return 2;
    if (text_equal(name, len, "sogou")) return 3;
    return -1;
}

const char* initial_for(char key) {
    switch (key) {
        case 'v': return "zh";
        case 'i': return "ch";
        case 'u': return "sh";
        default: return nullptr;
    }
}

bool key_is(const FinalEntry& entry, char key) {
    return entry.key == key;
}

/* Returns the first final carried by `key` that keeps `initial+final` valid. */
bool compose_with_initial(const char* initial, char key, char* out,
                          uint32_t* out_len) {
    for (const FinalEntry* entry = scheme().finals; entry->key; ++entry) {
        if (!key_is(*entry, key)) continue;
        for (uint32_t i = 0; i < 3 && entry->finals[i]; ++i) {
            Buffer b(out, 16);
            b.append(initial);
            b.append(entry->finals[i]);
            if (is_syllable(b.data, b.len)) {
                *out_len = b.len;
                return true;
            }
        }
    }
    return false;
}

bool starts_with(const char* text, char c) { return text[0] == c; }

/* Zero-initial: natural (double/verbatim/key) or o-key. */
bool compose_zero(char first, char second, char* out, uint32_t* out_len) {
    if (!scheme().o_key_zero && (first == 'a' || first == 'e' || first == 'o')) {
        for (const FinalEntry* entry = scheme().finals; entry->key; ++entry) {
            if (!key_is(*entry, second)) continue;
            for (uint32_t i = 0; i < 3 && entry->finals[i]; ++i) {
                const char* fin = entry->finals[i];
                if (!starts_with(fin, first)) continue;
                Buffer b(out, 8);
                b.append(fin);
                if (!is_syllable(b.data, b.len)) continue;
                /* Prefer the verbatim two-letter form (ai, an) and the
                 * doubled single vowel (aa is not a syllable: keep a). */
                *out_len = b.len;
                return true;
            }
        }
        /* a + a -> a */
        if (first == second) {
            out[0] = first;
            *out_len = 1;
            return true;
        }
        return false;
    }

    if (scheme().o_key_zero) {
        if (first == 'o') {
            if (second == 'u') {  /* ou typed as o,u */
                out[0] = 'o';
                out[1] = 'u';
                *out_len = 2;
                return true;
            }
            for (const FinalEntry* entry = scheme().finals; entry->key; ++entry) {
                if (!key_is(*entry, second)) continue;
                for (uint32_t i = 0; i < 3 && entry->finals[i]; ++i) {
                    Buffer b(out, 8);
                    b.append(entry->finals[i]);
                    if (is_syllable(b.data, b.len)) {
                        *out_len = b.len;
                        return true;
                    }
                }
            }
            return false;
        }
        if (first == 'a' || first == 'e') {
            for (const FinalEntry* entry = scheme().finals; entry->key; ++entry) {
                if (!key_is(*entry, second)) continue;
                for (uint32_t i = 0; i < 3 && entry->finals[i]; ++i) {
                    if (!starts_with(entry->finals[i], first)) continue;
                    Buffer b(out, 8);
                    b.append(entry->finals[i]);
                    if (is_syllable(b.data, b.len)) {
                        *out_len = b.len;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* Composes the syllable for a key pair. Falls back to the raw pair so the
 * translator still sees what was typed. */
uint32_t compose(char first, char second, char* out) {
    first = lower_ascii(first);
    second = lower_ascii(second);
    uint32_t len = 0;

    const bool zero = (first == 'a' || first == 'e' || first == 'o');
    if (zero && compose_zero(first, second, out, &len)) return len;
    if (!zero) {
        const char* initial = initial_for(first);
        char single[2] = {first, 0};
        const char* ini = initial ? initial : single;
        if (compose_with_initial(ini, second, out, &len)) return len;
        /* j/q/x/y + ü key also means the u spelling. */
        if ((first == 'j' || first == 'q' || first == 'x' || first == 'y') &&
            compose_with_initial(ini, 'v', out, &len))
            return len;
    }

    out[0] = first;
    out[1] = second;
    return 2;
}

bool load_half(const Frame& frame, char* half) {
    MapView state = MapView::of(frame.root.get("state"));
    const char* text = nullptr;
    uint32_t len = 0;
    if (!state.valid || !state.text("half", &text, &len) || len == 0) return false;
    *half = text[0];
    return true;
}

bool is_compose_key(const char* text, uint32_t len) {
    if (!text || len != 1) return false;
    const char c = lower_ascii(text[0]);
    return ascii_alpha(c) || c == ';';
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len, char half,
                    bool handled, const char* syllable, uint32_t syllable_len) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    if (half) {
        w.map(1);
        w.key("half");
        char one[2] = {half, 0};
        w.text(one, 1);
    } else {
        w.map(0);
    }
    w.key("workspace");
    if (!handled) {
        w.map(0);
    } else {
        w.map(syllable ? 2 : 1);
        w.key("consumed");
        w.boolean(true);
        if (syllable) {
            w.key("syllable");
            w.text(syllable, syllable_len);
        }
    }
    w.key("handled");
    w.boolean(handled);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_scheme = 0;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    const char* name = nullptr;
    uint32_t name_len = 0;
    if (settings.text("scheme", &name, &name_len)) {
        const int index = scheme_index(name, name_len);
        if (index >= 0) g_scheme = static_cast<uint32_t>(index);
    }
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    char half = 0;
    const bool has_half = load_half(frame, &half);

    /* `reset` drops a half-typed syllable without consuming anything. */
    if (frame.is_event("reset"))
        return respond(out, cap, out_len, 0, false, nullptr, 0);

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"), &action,
              &action_len);

    if (text_equal(action, action_len, "backspace")) {
        if (!has_half) return respond(out, cap, out_len, 0, false, nullptr, 0);
        return respond(out, cap, out_len, 0, true, nullptr, 0);
    }
    if (text_equal(action, action_len, "escape")) {
        if (!has_half) return respond(out, cap, out_len, 0, false, nullptr, 0);
        return respond(out, cap, out_len, 0, true, nullptr, 0);
    }

    const char* text = nullptr;
    uint32_t text_len = 0;
    frame.event_text(&text, &text_len);

    if (text_equal(action, action_len, "space") ||
        text_equal(action, action_len, "enter") ||
        frame.is_event("candidate")) {
        if (!has_half) return respond(out, cap, out_len, 0, false, nullptr, 0);
        char one[2] = {half, 0};
        return respond(out, cap, out_len, 0, true, one, 1);
    }

    if (!has_half) {
        if (!is_compose_key(text, text_len))
            return respond(out, cap, out_len, 0, false, nullptr, 0);
        return respond(out, cap, out_len, text[0], true, nullptr, 0);
    }

    if (!is_compose_key(text, text_len)) {
        char one[2] = {half, 0};
        return respond(out, cap, out_len, 0, true, one, 1);
    }

    char syllable[16];
    const uint32_t syllable_len = compose(half, text[0], syllable);
    return respond(out, cap, out_len, 0, true, syllable, syllable_len);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t* in, uint32_t in_len,
                                      uint8_t* out, uint32_t cap,
                                      uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;
    char half = 0;
    const bool has_half = load_half(frame, &half);
    return respond(out, cap, out_len, half, false, nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) { g_scheme = 0; }

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
