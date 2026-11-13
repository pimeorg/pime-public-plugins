/*
 * pime-predict-context: suggest the next word from the text before the cursor.
 *
 * Reads context.surrounding_before, matches its longest suffix against a small
 * phrase table, and writes plugin/pime-predict-context/candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 8192;
alignas(8) uint8_t g_scratch[kScratchBytes];

struct Suggestion {
    const char* phrase;
    const char* next;
    uint32_t score;
};

const Suggestion kTable[] = {
    {"你好", "吗", 120},  {"你好", "啊", 100},
    {"我们", "的", 120},  {"我们", "一起", 100},
    {"今天", "的", 110},  {"今天", "天气", 130},
    {"谢谢", "你", 140},  {"对不", "起", 150},
    {"可以", "的", 110},  {"可以", "吗", 100},
    {"没有", "问题", 120},
};

uint32_t g_max_candidates = 3;

/* True when `text` ends with `suffix` (UTF-8 byte suffix is codepoint safe
 * because both are valid UTF-8). */
bool ends_with(const char* text, uint32_t len, const char* suffix,
               uint32_t suffix_len) {
    if (!text || suffix_len == 0 || len < suffix_len) return false;
    const char* tail = text + (len - suffix_len);
    return Frame::equal(tail, suffix_len, suffix, suffix_len);
}

uint32_t load_surrounding(const Frame& frame, const char** out) {
    uint32_t len = 0;
    if (!frame.context.text("surrounding_before", out, &len)) {
        *out = nullptr;
        return 0;
    }
    return len;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_max_candidates = 3;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    uint64_t value = 0;
    if (settings.uint("max_candidates", &value) && value >= 1 && value <= 8)
        g_max_candidates = static_cast<uint32_t>(value);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    const char* text = nullptr;
    const uint32_t text_len = load_surrounding(frame, &text);

    const Suggestion* found[8];
    uint32_t count = 0;
    if (text_len > 0) {
        for (const auto& entry : kTable) {
            if (count >= g_max_candidates) break;
            if (ends_with(text, text_len, entry.phrase,
                          cstr_len(entry.phrase)))
                found[count++] = &entry;
        }
    }

    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        w.map(3);
        w.key("text");
        w.text(found[i]->next);
        w.key("score");
        w.u64(found[i]->score);
        w.key("source");
        w.text("context");
    }
    w.key("handled");
    w.boolean(false);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(0);
    w.key("handled");
    w.boolean(false);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

PIME_EXPORT void pime_shutdown(void) { g_max_candidates = 3; }

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
