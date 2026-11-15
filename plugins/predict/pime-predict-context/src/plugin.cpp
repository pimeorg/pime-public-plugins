/*
 * pime-predict-context: next-word suggestions from four sources.
 *
 * WeaveText parity (core/weave-engine/src/predict.rs), four tiers:
 *   1. user bigrams: `plugin/pime-learn-user-dict/next` written earlier in the
 *      same chain call (the committed word's learned successors).
 *   2. continuation table (`assets/continuation.tsv`): longest matching suffix
 *      of the text before the cursor (3/2/1 characters, longest first).
 *   3. character collocation model (`assets/collocations.tsv`): the last
 *      character of the context followed by its most frequent successor.
 *   4. built-in last-character fallback for common function words.
 *
 * Predictions are emitted as `show_candidates` commands by this plugin: it
 * runs after pime-candidates-format, which owns commit/display for the
 * composition that just ended. While a composition is active (any parse plugin
 * has a non-empty raw code) predictions stay silent so they never replace the
 * real candidate list.
 *
 * Workspace in: learn/next, learn/last, parse raw keys, context.surrounding_before.
 * Workspace out: candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxItems = 12;
constexpr uint32_t kMaxText = 96;
constexpr uint32_t kMaxContinuation = 8192;
constexpr uint32_t kMaxCollocation = 16384;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_continuation[kMaxContinuation];
alignas(8) uint8_t g_collocation[kMaxCollocation];

uint32_t g_continuation_len = 0;
uint32_t g_collocation_len = 0;
bool g_assets_loaded = false;
bool g_assets_ok = false;

bool g_use_user_bigram = true;
bool g_use_continuation = true;
bool g_use_collocation = true;
bool g_use_fallback = true;
bool g_show_command = true;
uint32_t g_max_candidates = 5;

struct Item {
    char text[kMaxText];
    uint32_t text_len = 0;
    char comment[16];
    uint32_t comment_len = 0;
    char source[24];
    uint32_t source_len = 0;
    uint32_t score = 0;
};

void set_text(Item* item, const char* text, uint32_t len) {
    if (len > kMaxText) len = kMaxText;
    for (uint32_t i = 0; i < len; ++i) item->text[i] = text[i];
    item->text_len = len;
}

void set_comment(Item* item, const char* text) {
    const uint32_t len = cstr_len(text);
    const uint32_t n = len < sizeof(item->comment) ? len : sizeof(item->comment);
    for (uint32_t i = 0; i < n; ++i) item->comment[i] = text[i];
    item->comment_len = n;
}

void set_source(Item* item, const char* text) {
    const uint32_t len = cstr_len(text);
    const uint32_t n = len < sizeof(item->source) ? len : sizeof(item->source);
    for (uint32_t i = 0; i < n; ++i) item->source[i] = text[i];
    item->source_len = n;
}

bool has_text(const Item* items, uint32_t count, const char* text,
              uint32_t len) {
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i].text_len == len &&
            Frame::equal(items[i].text, items[i].text_len, text, len))
            return true;
    }
    return false;
}

void push(Item* items, uint32_t* count, const char* text, uint32_t text_len,
          const char* source, const char* comment, uint32_t score) {
    if (*count >= kMaxItems || text_len == 0 || text_len >= kMaxText) return;
    if (has_text(items, *count, text, text_len)) return;
    Item& item = items[(*count)++];
    set_text(&item, text, text_len);
    set_source(&item, source);
    set_comment(&item, comment);
    item.score = score;
}

void sort_items(Item* items, uint32_t count) {
    for (uint32_t i = 1; i < count; ++i) {
        const Item value = items[i];
        uint32_t j = i;
        while (j > 0 && items[j - 1].score < value.score) {
            items[j] = items[j - 1];
            --j;
        }
        items[j] = value;
    }
}

/* --------------------------------------------------------------- assets */

bool load_asset(const char* path, uint8_t* buffer, uint32_t cap,
                uint32_t* out_len) {
    AssetHandle handle;
    if (!asset_open(path, &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 && size + 64 <= cap;
    uint32_t got = 0;
    if (ok)
        ok = asset_read(handle, 0, size, buffer, cap, &got) && got > 0;
    asset_close(handle);
    if (ok) *out_len = got;
    return ok;
}

/* Both tables are optional in a partial test package: a missing asset only
 * disables that tier, it never breaks the chain (the manifest still lists
 * them, so a production package always carries them). */
void load_assets() {
    if (g_assets_loaded) return;
    g_assets_loaded = true;
    const bool continuation =
        load_asset("continuation.tsv", g_continuation, sizeof(g_continuation),
                   &g_continuation_len);
    const bool collocation =
        load_asset("collocations.tsv", g_collocation, sizeof(g_collocation),
                   &g_collocation_len);
    g_assets_ok = continuation || collocation;
}

/* --------------------------------------------------------------- context */

uint32_t read_context(const Frame& frame, const char** out) {
    uint32_t len = 0;
    if (frame.context.text("surrounding_before", out, &len) && len > 0)
        return len;
    if (frame.event.text("before", out, &len) && len > 0) return len;
    WorkspaceKey key = full_key("pime-learn-user-dict", "last");
    if (span_text(frame.workspace_value(key.text), out, &len) && len > 0)
        return len;
    *out = nullptr;
    return 0;
}

bool composition_active(const Frame& frame) {
    static const char* const kSources[] = {
        "pime-pinyin-parse", "pime-wubi-parse", "pime-cangjie-parse",
        "pime-t9-parse", "pime-english-parse",
    };
    static const char* const kKeys[] = {"raw", "code", "code", "raw", "raw"};
    for (uint32_t i = 0; i < 5; ++i) {
        WorkspaceKey key = full_key(kSources[i], kKeys[i]);
        const char* text = nullptr;
        uint32_t len = 0;
        if (!span_text(frame.workspace_value(key.text), &text, &len)) continue;
        if (len > 0) return true;
    }
    return false;
}

/* --------------------------------------------------------------- tier 1 */

void user_bigrams(const Frame& frame, Item* items, uint32_t* count) {
    WorkspaceKey key = full_key("pime-learn-user-dict", "next");
    ArrayView array = ArrayView::of(frame.workspace_value(key.text));
    if (!array.valid) return;
    for (uint64_t i = 0; i < array.count && *count < kMaxItems; ++i) {
        MapView item = MapView::of(array.at(i));
        const char* text = nullptr;
        uint32_t text_len = 0;
        if (!item.text("text", &text, &text_len)) continue;
        push(items, count, text, text_len, "user-bigram", "联想", 5000);
    }
}

/* --------------------------------------------------------------- tier 2 */

/* Longest suffix of the context wins; a shorter match only fills gaps. */
void continuation(const char* text, uint32_t len, Item* items, uint32_t* count) {
    if (!g_continuation_len) return;
    const char* data = reinterpret_cast<const char*>(g_continuation);
    for (uint32_t chars = 4; chars >= 1; --chars) {
        const uint32_t offset = utf8_suffix_offset(text, len, chars);
        if (offset >= len) continue;
        const char* suffix = text + offset;
        const uint32_t suffix_len = len - offset;
        uint32_t i = 0;
        while (i < g_continuation_len) {
            const char* line = data + i;
            uint32_t n = 0;
            while (i + n < g_continuation_len && data[i + n] != '\n' &&
                   data[i + n] != '\r')
                ++n;
            i += n;
            while (i < g_continuation_len &&
                   (data[i] == '\n' || data[i] == '\r'))
                ++i;
            if (n == 0 || line[0] == '#') continue;
            /* <key>\t<next>\t<weight> */
            uint32_t key_len = 0;
            while (key_len < n && line[key_len] != '\t') ++key_len;
            if (key_len != suffix_len) continue;
            if (!Frame::equal(line, key_len, suffix, suffix_len)) continue;
            uint32_t next_start = key_len + 1;
            uint32_t next_len = next_start;
            while (next_len < n && line[next_len] != '\t') ++next_len;
            if (next_len <= next_start) continue;
            uint32_t weight = 0;
            if (next_len + 1 < n)
                dict_parse_freq(line + next_len + 1, n - next_len - 1, &weight);
            push(items, count, line + next_start, next_len - next_start,
                 "continuation", "接续", 3000 + (weight > 999 ? 999 : weight));
        }
        if (*count > 0) return;
    }
}

/* --------------------------------------------------------------- tier 3 */

void collocation(const char* text, uint32_t len, Item* items, uint32_t* count) {
    if (!g_collocation_len || len == 0) return;
    const uint32_t offset = utf8_suffix_offset(text, len, 1);
    if (offset >= len) return;
    const char* last = text + offset;
    const uint32_t last_len = len - offset;

    const char* data = reinterpret_cast<const char*>(g_collocation);
    uint32_t i = 0;
    while (i < g_collocation_len) {
        const char* line = data + i;
        uint32_t n = 0;
        while (i + n < g_collocation_len && data[i + n] != '\n' &&
               data[i + n] != '\r')
            ++n;
        i += n;
        while (i < g_collocation_len && (data[i] == '\n' || data[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t used = 0;
        utf8_decode(line, n, &used);
        if (used == 0 || used >= n) continue;
        if (used != last_len || !Frame::equal(line, used, last, last_len))
            continue;
        uint32_t right_used = 0;
        utf8_decode(line + used, n - used, &right_used);
        if (right_used == 0) continue;
        const uint32_t pos = used + right_used;
        if (pos >= n || line[pos] != '\t') continue;
        uint32_t weight = 0;
        if (!dict_parse_freq(line + pos + 1, n - pos - 1, &weight)) continue;
        push(items, count, line + used, right_used, "collocation", "搭配",
             1000 + weight / 100000);
    }
}

/* --------------------------------------------------------------- tier 4 */

struct Fallback {
    const char* previous;
    const char* next;
};

const Fallback kFallback[] = {
    {"很", "好"}, {"非", "常"}, {"特", "别"}, {"真", "的"}, {"不", "是"},
    {"可", "以"}, {"应", "该"}, {"已", "经"}, {"因", "为"}, {"所", "以"},
    {"就", "是"}, {"还", "有"}, {"一", "个"}, {"这", "个"}, {"那", "个"},
};

void fallback(const char* text, uint32_t len, Item* items, uint32_t* count) {
    if (len == 0) return;
    const uint32_t offset = utf8_suffix_offset(text, len, 1);
    const char* last = text + offset;
    const uint32_t last_len = len - offset;
    for (const auto& entry : kFallback) {
        if (!Frame::equal(last, last_len, entry.previous,
                          cstr_len(entry.previous)))
            continue;
        push(items, count, entry.next, cstr_len(entry.next), "fallback", "续字",
             500);
    }
}

/* --------------------------------------------------------------- output */

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Item* items, uint32_t count, bool show) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        const bool has_comment = items[i].comment_len > 0;
        w.map(has_comment ? 4 : 3);
        w.key("text");
        w.text(items[i].text, items[i].text_len);
        w.key("score");
        w.u64(items[i].score);
        w.key("source");
        w.text(items[i].source, items[i].source_len);
        if (has_comment) {
            w.key("comment");
            w.text(items[i].comment, items[i].comment_len);
        }
    }
    w.key("handled");
    w.boolean(false);
    w.key("commands");
    if (!show || count == 0) {
        w.array(0);
    } else {
        w.array(1);
        cmd_show_candidates_begin(w, count);
        for (uint32_t i = 0; i < count; ++i)
            cmd_candidate_item_ex(w, items[i].text, items[i].text_len,
                                  items[i].comment, items[i].comment_len,
                                  nullptr, 0);
    }
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_use_user_bigram = true;
    g_use_continuation = true;
    g_use_collocation = true;
    g_use_fallback = true;
    g_show_command = true;
    g_max_candidates = 5;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("user_bigram", &value)) g_use_user_bigram = value;
    if (settings.boolean("continuation", &value)) g_use_continuation = value;
    if (settings.boolean("collocation", &value)) g_use_collocation = value;
    if (settings.boolean("fallback", &value)) g_use_fallback = value;
    if (settings.boolean("show_command", &value)) g_show_command = value;
    uint64_t max = 0;
    if (settings.uint("max_candidates", &max) && max >= 1 && max <= kMaxItems)
        g_max_candidates = static_cast<uint32_t>(max);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    load_assets();

    const char* context = nullptr;
    const uint32_t context_len = read_context(frame, &context);
    const bool active = composition_active(frame);

    Item items[kMaxItems];
    uint32_t count = 0;
    if (!active) {
        if (g_use_user_bigram) user_bigrams(frame, items, &count);
        if (context_len > 0) {
            if (g_use_continuation)
                continuation(context, context_len, items, &count);
            if (g_use_collocation)
                collocation(context, context_len, items, &count);
            if (g_use_fallback) fallback(context, context_len, items, &count);
        }
        sort_items(items, count);
        if (count > g_max_candidates) count = g_max_candidates;
    }

    return respond(out, cap, out_len, items, count,
                   g_show_command && !active);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0, false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_assets_loaded = false;
    g_assets_ok = false;
    g_continuation_len = 0;
    g_collocation_len = 0;
    g_use_user_bigram = true;
    g_use_continuation = true;
    g_use_collocation = true;
    g_use_fallback = true;
    g_show_command = true;
    g_max_candidates = 5;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
