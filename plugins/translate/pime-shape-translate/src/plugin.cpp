/*
 * pime-shape-translate: component lookup and glyph filter for the u-mode.
 *
 * WeaveText parity (weave-dict/shape.rs):
 *   - shape mode: the code after `u` is a component pinyin sequence
 *     ("umumu" -> 林). Exact component sequences win; a code that is a prefix
 *     of an entry lists its completions with the remaining code as comment.
 *   - glyph mode: the code after a backtick optionally names a glyph group
 *     ("`mu" -> 木林森树); without a code the group of the first current
 *     candidate is used ("`" after 林 lists 木-group characters).
 *
 * Workspace in: shape-parse mode/code, shortcuts/hans-hant/special/rank
 * candidates (glyph mode picks the group from the first candidate).
 * Workspace out: candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxItems = 24;
constexpr uint32_t kMaxText = 32;
constexpr uint32_t kMaxShape = 8192;
constexpr uint32_t kMaxGlyphs = 4096;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_shape[kMaxShape];
alignas(8) uint8_t g_glyphs[kMaxGlyphs];

uint32_t g_shape_len = 0;
uint32_t g_glyphs_len = 0;
bool g_loaded = false;
bool g_enabled = true;
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
          const char* comment, const char* source, uint32_t score) {
    if (*count >= kMaxItems || text_len == 0 || text_len >= kMaxText) return;
    if (has_text(items, *count, text, text_len)) return;
    Item& item = items[(*count)++];
    for (uint32_t i = 0; i < text_len; ++i) item.text[i] = text[i];
    item.text_len = text_len;
    const uint32_t comment_len = cstr_len(comment);
    for (uint32_t i = 0; i < comment_len && i < sizeof(item.comment); ++i)
        item.comment[i] = comment[i];
    item.comment_len = comment_len < sizeof(item.comment) ? comment_len
                                                          : sizeof(item.comment);
    const uint32_t source_len = cstr_len(source);
    for (uint32_t i = 0; i < source_len && i < sizeof(item.source); ++i)
        item.source[i] = source[i];
    item.source_len = source_len < sizeof(item.source) ? source_len
                                                       : sizeof(item.source);
    item.score = score;
}

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

void load_assets() {
    if (g_loaded) return;
    g_loaded = true;
    load_asset("shape.tsv", g_shape, sizeof(g_shape), &g_shape_len);
    load_asset("glyphs.tsv", g_glyphs, sizeof(g_glyphs), &g_glyphs_len);
}

uint32_t read_workspace(const Frame& frame, const char* plugin, const char* key,
                        const char** out) {
    WorkspaceKey full = full_key(plugin, key);
    uint32_t len = 0;
    if (!span_text(frame.workspace_value(full.text), out, &len)) {
        *out = nullptr;
        return 0;
    }
    return len;
}

/* Component lookup: exact entries first, then prefix completions. */
void shape_lookup(const char* code, uint32_t code_len, Item* items,
                  uint32_t* count) {
    if (g_shape_len == 0 || code_len == 0) return;
    const char* data = reinterpret_cast<const char*>(g_shape);
    uint32_t i = 0;
    while (i < g_shape_len) {
        const char* line = data + i;
        uint32_t n = 0;
        while (i + n < g_shape_len && data[i + n] != '\n' && data[i + n] != '\r')
            ++n;
        i += n;
        while (i < g_shape_len && (data[i] == '\n' || data[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t key_len = 0;
        while (key_len < n && line[key_len] != '\t') ++key_len;
        if (key_len == 0 || key_len >= n) continue;
        const char* text = line + key_len + 1;
        const uint32_t text_len = n - key_len - 1;
        if (key_len == code_len &&
            Frame::equal(line, key_len, code, code_len)) {
            push(items, count, text, text_len, "拆字", "shape", 10000);
            continue;
        }
        if (key_len > code_len &&
            Frame::equal(line, code_len, code, code_len)) {
            char comment[16];
            Buffer c(comment, sizeof(comment));
            c.append("补全");
            c.append(line + code_len, key_len - code_len);
            const uint32_t remaining = key_len - code_len;
            push(items, count, text, text_len, c.data, "shape",
                 static_cast<uint32_t>(3000 > remaining * 100
                                           ? 3000 - remaining * 100
                                           : 0));
        }
    }
}

/* Glyph group: explicit key, or the group containing `character`. */
bool glyph_group(const char* code, uint32_t code_len, const char* character,
                 uint32_t character_len, const char** chars,
                 uint32_t* chars_len) {
    if (g_glyphs_len == 0) return false;
    const char* data = reinterpret_cast<const char*>(g_glyphs);
    uint32_t i = 0;
    while (i < g_glyphs_len) {
        const char* line = data + i;
        uint32_t n = 0;
        while (i + n < g_glyphs_len && data[i + n] != '\n' && data[i + n] != '\r')
            ++n;
        i += n;
        while (i < g_glyphs_len && (data[i] == '\n' || data[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t key_len = 0;
        while (key_len < n && line[key_len] != '\t') ++key_len;
        if (key_len == 0 || key_len >= n) continue;
        const char* group = line + key_len + 1;
        const uint32_t group_len = n - key_len - 1;
        if (code_len > 0) {
            if (key_len == code_len &&
                Frame::equal(line, key_len, code, code_len)) {
                *chars = group;
                *chars_len = group_len;
                return true;
            }
            continue;
        }
        if (character_len == 0) continue;
        for (uint32_t k = 0; k < group_len;) {
            uint32_t used = 0;
            utf8_decode(group + k, group_len - k, &used);
            if (used == 0) break;
            if (used == character_len &&
                Frame::equal(group + k, used, character, character_len)) {
                *chars = group;
                *chars_len = group_len;
                return true;
            }
            k += used;
        }
    }
    return false;
}

/* First text of the best current candidate array. */
void first_candidate(const Frame& frame, const char** text, uint32_t* len) {
    static const char* const kSources[] = {
        "pime-shortcuts",      "pime-hans-hant",
        "pime-special-candidates", "pime-pinyin-rank",
        "pime-rank-default",   "pime-pinyin-translate",
    };
    for (const char* id : kSources) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (!array.valid || array.count == 0) continue;
        MapView item = MapView::of(array.at(0));
        if (item.text("text", text, len)) return;
    }
    *text = nullptr;
    *len = 0;
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
    g_max_candidates = 8;
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
    if (g_enabled) {
        load_assets();
        const char* mode = nullptr;
        const uint32_t mode_len = read_workspace(frame, "pime-shape-parse", "mode",
                                                 &mode);
        const char* code = nullptr;
        const uint32_t code_len = read_workspace(frame, "pime-shape-parse", "code",
                                                 &code);
        if (text_equal(mode, mode_len, "shape")) {
            shape_lookup(code, code_len, items, &count);
        } else if (text_equal(mode, mode_len, "glyph")) {
            const char* character = nullptr;
            uint32_t character_len = 0;
            if (code_len == 0) first_candidate(frame, &character, &character_len);
            const char* group = nullptr;
            uint32_t group_len = 0;
            if (glyph_group(code, code_len, character, character_len, &group,
                            &group_len)) {
                for (uint32_t i = 0; i < group_len && count < g_max_candidates;) {
                    uint32_t used = 0;
                    utf8_decode(group + i, group_len - i, &used);
                    if (used == 0) break;
                    push(items, &count, group + i, used, "字形", "glyph", 5000);
                    i += used;
                }
            }
        }
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
    g_shape_len = 0;
    g_glyphs_len = 0;
    g_enabled = true;
    g_max_candidates = 8;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
