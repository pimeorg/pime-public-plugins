/*
 * pime-emoji-suggest: emoji suggestions from the last word before the caret.
 *
 * WeaveText parity (core/weave-engine/src/convert.rs emoji path): a small
 * keyword -> emoji table is matched against the longest suffix of the context
 * (or the last committed word from pime-learn-user-dict when the host does not
 * expose surrounding text).
 *
 * Workspace out: candidates (source=emoji, comment=表情). Never consumes a key.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 8192;
constexpr uint32_t kMaxItems = 8;
constexpr uint32_t kMaxText = 64;
constexpr uint32_t kMaxTable = 8192;
constexpr uint32_t kMaxKeyChars = 4;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_table[kMaxTable];

uint32_t g_table_len = 0;
bool g_loaded = false;
bool g_enabled = true;
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

bool load_table() {
    if (g_loaded) return g_table_len > 0;
    g_loaded = true;
    AssetHandle handle;
    if (!asset_open("emoji.tsv", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 &&
              size + 64 <= sizeof(g_table);
    uint32_t got = 0;
    if (ok)
        ok = asset_read(handle, 0, size, g_table, sizeof(g_table), &got) &&
             got > 0;
    asset_close(handle);
    if (ok) g_table_len = got;
    return ok;
}

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

void push(Item* items, uint32_t* count, const char* text, uint32_t text_len,
          uint32_t score) {
    if (*count >= kMaxItems || text_len == 0 || text_len >= kMaxText) return;
    if (has_text(items, *count, text, text_len)) return;
    Item& item = items[(*count)++];
    for (uint32_t i = 0; i < text_len; ++i) item.text[i] = text[i];
    item.text_len = text_len;
    const char comment[] = "表情";
    for (uint32_t i = 0; i < cstr_len(comment); ++i)
        item.comment[i] = comment[i];
    item.comment_len = cstr_len(comment);
    const char source[] = "emoji";
    for (uint32_t i = 0; i < cstr_len(source); ++i) item.source[i] = source[i];
    item.source_len = cstr_len(source);
    item.score = score;
}

void match(const char* context, uint32_t len, Item* items, uint32_t* count) {
    const char* data = reinterpret_cast<const char*>(g_table);
    uint32_t chars = kMaxKeyChars;
    const uint32_t total = utf8_prefix_bytes(context, len, kMaxKeyChars);
    uint32_t available = 0;
    for (uint32_t i = 0; i < total;) {
        uint32_t used = 0;
        utf8_decode(context + i, total - i, &used);
        i += used ? used : 1;
        ++available;
    }
    if (available < chars) chars = available;

    for (uint32_t key_chars = chars; key_chars >= 1; --key_chars) {
        const uint32_t offset = utf8_suffix_offset(context, len, key_chars);
        if (offset >= len) continue;
        const char* suffix = context + offset;
        const uint32_t suffix_len = len - offset;
        uint32_t i = 0;
        while (i < g_table_len) {
            const char* line = data + i;
            uint32_t n = 0;
            while (i + n < g_table_len && data[i + n] != '\n' &&
                   data[i + n] != '\r')
                ++n;
            i += n;
            while (i < g_table_len && (data[i] == '\n' || data[i] == '\r')) ++i;
            if (n == 0 || line[0] == '#') continue;
            uint32_t key_len = 0;
            while (key_len < n && line[key_len] != '\t') ++key_len;
            if (key_len != suffix_len || key_len >= n) continue;
            if (!Frame::equal(line, key_len, suffix, suffix_len)) continue;
            push(items, count, line + key_len + 1, n - key_len - 1, 800);
            if (*count >= g_max_candidates) return;
        }
        if (*count > 0) return;
    }
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
    g_max_candidates = 3;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
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

    Item items[kMaxItems];
    uint32_t count = 0;
    const char* context = nullptr;
    const uint32_t len = g_enabled ? read_context(frame, &context) : 0;
    if (len > 0 && load_table()) {
        match(context, len, items, &count);
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
    g_loaded = false;
    g_table_len = 0;
    g_enabled = true;
    g_max_candidates = 3;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
