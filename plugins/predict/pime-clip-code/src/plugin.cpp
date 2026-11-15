/*
 * pime-clip-code: surface SMS verification codes found around the caret.
 *
 * WeaveText parity (core/weave-engine/src/convert.rs verification-code path):
 * a 4-8 digit run that is not part of a longer number becomes a candidate.
 * The plugin reads the surrounding text (before/after) and, when the host
 * exposes it, `context.clipboard`; a nearby keyword (验证码 / 校验码 / 动态码 /
 * code) only raises the score, it is never required.
 *
 * Workspace out: candidates. Predictions never consume the key (handled=false).
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 4096;
constexpr uint32_t kMaxItems = 4;
constexpr uint32_t kMaxText = 24;
constexpr uint32_t kMaxSource = 4096;

alignas(8) uint8_t g_scratch[kScratchBytes];

bool g_enabled = true;
uint32_t g_min_len = 4;
uint32_t g_max_len = 8;
uint32_t g_max_candidates = 3;

struct Item {
    char text[kMaxText];
    uint32_t text_len = 0;
    char comment[16];
    uint32_t comment_len = 0;
    char source[16];
    uint32_t source_len = 0;
    uint32_t score = 0;
};

bool has_text(const Item* items, uint32_t count, const char* text,
              uint32_t len) {
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i].text_len == len &&
            Frame::equal(items[i].text, items[i].text_len, text, len))
            return true;
    }
    return false;
}

bool keyword_near(const char* text, uint32_t len, uint32_t start,
                  uint32_t stop) {
    static const char* const kKeywords[] = {"验证码", "校验码", "动态码",
                                            "确认码", "code"};
    const uint32_t begin = start > 12 ? start - 12 : 0;
    const uint32_t end = stop + 12 < len ? stop + 12 : len;
    for (const char* keyword : kKeywords) {
        const uint32_t n = cstr_len(keyword);
        for (uint32_t i = begin; i + n <= end; ++i) {
            if (Frame::equal(text + i, n, keyword, n)) return true;
        }
    }
    return false;
}

/* Collects digit runs; `consumed` marks characters already used so the same
 * text does not produce duplicates from overlapping sources. */
void scan(const char* text, uint32_t len, Item* items, uint32_t* count) {
    uint32_t i = 0;
    while (i < len && *count < kMaxItems) {
        if (!ascii_digit(text[i])) {
            ++i;
            continue;
        }
        const uint32_t start = i;
        while (i < len && ascii_digit(text[i])) ++i;
        const uint32_t run = i - start;
        if (run < g_min_len || run > g_max_len) continue;
        if (has_text(items, *count, text + start, run)) continue;
        Item& item = items[(*count)++];
        for (uint32_t k = 0; k < run; ++k) item.text[k] = text[start + k];
        item.text_len = run;
        const char comment[] = "验证码";
        for (uint32_t k = 0; k < cstr_len(comment); ++k)
            item.comment[k] = comment[k];
        item.comment_len = cstr_len(comment);
        const char source[] = "clip";
        for (uint32_t k = 0; k < cstr_len(source); ++k) item.source[k] = source[k];
        item.source_len = cstr_len(source);
        item.score = 1000 + run * 10 + (keyword_near(text, len, start, i) ? 500 : 0);
    }
}

uint32_t gather_sources(const Frame& frame, char* out, uint32_t cap) {
    Buffer b(out, cap);
    const char* text = nullptr;
    uint32_t len = 0;
    if (frame.context.text("surrounding_before", &text, &len))
        b.append(text, len);
    if (frame.event.text("before", &text, &len)) b.append(text, len);
    if (frame.context.text("clipboard", &text, &len)) b.append(text, len);
    if (frame.event.text("after", &text, &len)) b.append(text, len);
    if (frame.context.text("surrounding_after", &text, &len))
        b.append(text, len);
    return b.len;
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Item* items, uint32_t count) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        w.map(4);
        w.key("text");
        w.text(items[i].text, items[i].text_len);
        w.key("score");
        w.u64(items[i].score);
        w.key("source");
        w.text(items[i].source, items[i].source_len);
        w.key("comment");
        w.text(items[i].comment, items[i].comment_len);
    }
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
    g_min_len = 4;
    g_max_len = 8;
    g_max_candidates = 3;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    uint64_t number = 0;
    if (settings.uint("min_len", &number) && number >= 3 && number <= 12)
        g_min_len = static_cast<uint32_t>(number);
    if (settings.uint("max_len", &number) && number >= g_min_len &&
        number <= 12)
        g_max_len = static_cast<uint32_t>(number);
    if (g_max_len < g_min_len) g_max_len = g_min_len;
    if (settings.uint("max_candidates", &number) && number >= 1 &&
        number <= kMaxItems)
        g_max_candidates = static_cast<uint32_t>(number);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    Item items[kMaxItems];
    uint32_t count = 0;
    if (g_enabled) {
        static char source_text[kMaxSource];
        const uint32_t len = gather_sources(frame, source_text, sizeof(source_text));
        if (len > 0) scan(source_text, len, items, &count);
        if (count > g_max_candidates) count = g_max_candidates;
    }
    return respond(out, cap, out_len, items, count);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) {
    g_enabled = true;
    g_min_len = 4;
    g_max_len = 8;
    g_max_candidates = 3;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
