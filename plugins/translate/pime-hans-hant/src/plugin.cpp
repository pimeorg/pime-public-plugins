/*
 * pime-hans-hant: simplified -> traditional conversion of the candidate list.
 *
 * WeaveText parity (core/weave-engine/src/convert.rs): phrase-first longest
 * match over a fixed table. The plugin takes the best candidate array earlier
 * in the chain and rewrites each candidate text, so the display, paging and
 * commit path stay unchanged.
 *
 * If nothing changes (for example the candidates are already traditional or
 * the text is not covered by the table) the plugin writes an empty array and
 * format keeps using the upstream candidates: a missing conversion never hides
 * the ordinary list.
 *
 * Workspace in: shortcuts/special/rank/translate candidates.
 * Workspace out: candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxItems = 16;
constexpr uint32_t kMaxText = 192;
constexpr uint32_t kMaxTable = 16384;
constexpr uint32_t kMaxKeyChars = 6;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_table[kMaxTable];

uint32_t g_table_len = 0;
bool g_loaded = false;
bool g_enabled = true;
bool g_comment = true;
uint32_t g_max_candidates = 8;

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

void set_source(Item* item, const char* text, uint32_t len) {
    const uint32_t n = len < sizeof(item->source) ? len : sizeof(item->source);
    for (uint32_t i = 0; i < n; ++i) item->source[i] = text[i];
    item->source_len = n;
}

bool load_table() {
    if (g_loaded) return g_table_len > 0;
    g_loaded = true;
    AssetHandle handle;
    if (!asset_open("hans-hant.tsv", &handle)) return false;
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

/* Finds the traditional text for the first `key_chars` codepoints of `text`. */
bool lookup(const char* text, uint32_t len, uint32_t key_chars, const char** out,
            uint32_t* out_len, uint32_t* key_bytes) {
    const uint32_t key_len = utf8_prefix_bytes(text, len, key_chars);
    if (key_len == 0) return false;
    const char* data = reinterpret_cast<const char*>(g_table);
    uint32_t i = 0;
    while (i < g_table_len) {
        const char* line = data + i;
        uint32_t n = 0;
        while (i + n < g_table_len && data[i + n] != '\n' && data[i + n] != '\r')
            ++n;
        i += n;
        while (i < g_table_len && (data[i] == '\n' || data[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t sep = 0;
        while (sep < n && line[sep] != '\t') ++sep;
        if (sep != key_len || sep >= n) continue;
        if (!Frame::equal(line, sep, text, key_len)) continue;
        *out = line + sep + 1;
        *out_len = n - sep - 1;
        *key_bytes = key_len;
        return *out_len > 0;
    }
    return false;
}

/* Returns true when at least one codepoint changed. */
bool convert(const char* text, uint32_t len, char* out, uint32_t cap,
             uint32_t* out_len) {
    Buffer b(out, cap);
    bool changed = false;
    uint32_t i = 0;
    while (i < len) {
        uint32_t remaining = 0;
        uint32_t probe = i;
        while (probe < len && remaining < kMaxKeyChars) {
            uint32_t used = 0;
            utf8_decode(text + probe, len - probe, &used);
            probe += used ? used : 1;
            ++remaining;
        }
        bool matched = false;
        for (uint32_t chars = remaining; chars >= 1; --chars) {
            const char* replacement = nullptr;
            uint32_t replacement_len = 0;
            uint32_t key_bytes = 0;
            if (!lookup(text + i, len - i, chars, &replacement,
                        &replacement_len, &key_bytes))
                continue;
            b.append(replacement, replacement_len);
            i += key_bytes;
            matched = true;
            changed = true;
            break;
        }
        if (matched) continue;
        uint32_t used = 0;
        utf8_decode(text + i, len - i, &used);
        if (used == 0) used = 1;
        b.append(text + i, used);
        i += used;
    }
    *out_len = b.len;
    return changed && !b.overflow;
}

void copy_best(const Frame& frame, Item* items, uint32_t* count) {
    static const char* const kSources[] = {
        "pime-shortcuts",      "pime-special-candidates",
        "pime-pinyin-rank",    "pime-rank-default",
        "pime-pinyin-translate",
    };
    for (const char* id : kSources) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (!array.valid || array.count == 0) continue;
        for (uint64_t i = 0; i < array.count && *count < kMaxItems; ++i) {
            MapView item = MapView::of(array.at(i));
            const char* text = nullptr;
            uint32_t text_len = 0;
            if (!item.text("text", &text, &text_len)) continue;
            Item& out = items[(*count)++];
            set_text(&out, text, text_len);
            const char* source = nullptr;
            uint32_t source_len = 0;
            if (item.text("source", &source, &source_len))
                set_source(&out, source, source_len);
            else
                set_source(&out, "dict", 4);
            uint64_t score = 0;
            item.uint("score", &score);
            out.score = static_cast<uint32_t>(score);
        }
        return;
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
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_enabled = true;
    g_comment = true;
    g_max_candidates = 8;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    if (settings.boolean("comment", &value)) g_comment = value;
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
    if (g_enabled && load_table()) {
        Item source_items[kMaxItems];
        uint32_t source_count = 0;
        copy_best(frame, source_items, &source_count);
        bool any = false;
        for (uint32_t i = 0; i < source_count && count < g_max_candidates; ++i) {
            Item converted = {};
            uint32_t converted_len = 0;
            if (!convert(source_items[i].text, source_items[i].text_len,
                         converted.text, sizeof(converted.text), &converted_len)) {
                converted_len = source_items[i].text_len;
                for (uint32_t k = 0; k < converted_len; ++k)
                    converted.text[k] = source_items[i].text[k];
            } else {
                any = true;
            }
            converted.text_len = converted_len;
            if (g_comment) set_comment(&converted, "繁体");
            set_source(&converted, "hans-hant", 9);
            converted.score = source_items[i].score;
            items[count++] = converted;
        }
        if (!any) count = 0;
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
    g_comment = true;
    g_max_candidates = 8;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
