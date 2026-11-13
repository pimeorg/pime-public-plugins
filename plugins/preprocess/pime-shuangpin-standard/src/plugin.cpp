/*
 * pime-shuangpin-standard: two-key syllable composition.
 *
 * State: { half: text } — the pending first key.
 * Workspace out: consumed (bool), syllable (text) on the second key.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
alignas(8) uint8_t g_scratch[kScratchBytes];

struct FinalCode {
    char key;
    const char* final;
};

/* Starter final table; adjust here to match the target scheme. */
const FinalCode kFinals[] = {
    {'a', "a"},   {'b', "ou"},  {'c', "iao"}, {'d', "ai"},  {'e', "e"},
    {'f', "en"},  {'g', "eng"}, {'h', "ang"}, {'i', "i"},   {'j', "an"},
    {'k', "ao"},  {'l', "in"},  {'m', "ian"}, {'n', "iang"}, {'o', "o"},
    {'p', "ie"},  {'q', "iu"},  {'r', "uan"}, {'s', "ong"}, {'t', "ue"},
    {'u', "u"},   {'v', "v"},   {'w', "ua"},  {'x', "ia"},  {'y', "ing"},
    {'z', "ei"},
};

const char* final_for(char key) {
    for (const auto& entry : kFinals)
        if (entry.key == key) return entry.final;
    return nullptr;
}

/* Two-letter initial for v / i / u; every other key is its own initial. */
const char* initial_for(char key) {
    switch (key) {
        case 'v': return "zh";
        case 'i': return "ch";
        case 'u': return "sh";
        default: return nullptr;
    }
}

uint32_t append_text(char* out, uint32_t len, const char* text) {
    while (text && *text) out[len++] = *text++;
    return len;
}

uint32_t compose(char first, char second, char* out) {
    const char* final = final_for(second);
    if (!final) return 0;

    if (first == 'a' || first == 'e' || first == 'o') {
        /* Zero initial: the first key only selects the vowel class, the
         * final code alone gives the syllable (aj -> an, ad -> ai). */
        return append_text(out, 0, final);
    }

    uint32_t len = 0;
    const char* initial = initial_for(first);
    out[len++] = initial ? initial[0] : first;
    if (initial && initial[1]) out[len++] = initial[1];
    return append_text(out, len, final);
}

uint32_t load_half(const Frame& frame, char* half) {
    MapView state = MapView::of(frame.root.get("state"));
    const char* text = nullptr;
    uint32_t len = 0;
    if (!state.valid || !state.text("half", &text, &len) || len == 0) return 0;
    *half = text[0];
    return 1;
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const char* half, uint32_t half_len, const char* syllable,
                    uint32_t syllable_len) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    if (half_len > 0) {
        w.map(1);
        w.key("half");
        w.text(half, half_len);
    } else {
        w.map(0);
    }
    w.key("workspace");
    w.map(syllable ? 2 : 1);
    w.key("consumed");
    w.boolean(true);
    if (syllable) {
        w.key("syllable");
        w.text(syllable, syllable_len);
    }
    w.key("handled");
    w.boolean(true);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

pime_status pass_through(uint8_t* out, uint32_t cap, uint32_t* out_len,
                         const char* half, uint32_t half_len) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    if (half_len > 0) {
        w.map(1);
        w.key("half");
        w.text(half, half_len);
    } else {
        w.map(0);
    }
    w.key("workspace");
    w.map(0);
    w.key("handled");
    w.boolean(false);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t*, uint32_t) { return PIME_OK; }

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    char half = 0;
    const uint32_t half_len = load_half(frame, &half);

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"), &action,
              &action_len);

    if (action && action_len == 9 && Frame::equal(action, 9, "backspace", 9))
        return respond(out, cap, out_len, nullptr, 0, nullptr, 0);

    const char* text = nullptr;
    uint32_t text_len = 0;
    if (!frame.event_text(&text, &text_len) || text_len != 1)
        return pass_through(out, cap, out_len, half_len ? &half : nullptr,
                            half_len);
    char key = text[0];
    if (key >= 'A' && key <= 'Z') key = static_cast<char>(key - 'A' + 'a');
    if (key < 'a' || key > 'z')
        return pass_through(out, cap, out_len, half_len ? &half : nullptr,
                            half_len);

    if (half_len == 0)
        return respond(out, cap, out_len, &key, 1, nullptr, 0);

    char syllable[8] = {};
    const uint32_t syllable_len = compose(half, key, syllable);
    if (syllable_len == 0)
        return pass_through(out, cap, out_len, nullptr, 0);
    return respond(out, cap, out_len, nullptr, 0, syllable, syllable_len);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return pass_through(out, cap, out_len, nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) {}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
