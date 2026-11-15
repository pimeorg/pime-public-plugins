/*
 * pime-shortcuts: user-facing shortcut phrases and date/time templates.
 *
 * WeaveText parity (core/weave-engine/src/convert.rs quick phrases):
 *   - an exact code match expands a stored phrase (`dz` -> 地址);
 *   - `{date}` / `{time}` / `{weekday}` / `{iso}` expand to the local date,
 *     time, weekday and ISO date of the current moment.
 *
 * Shortcut candidates are merged in front of the ordinary candidate array so a
 * shortcut key never hides the dictionary entries; format prefers this plugin's
 * array whenever it is non-empty.
 *
 * Workspace in: parse raw, special/rank/translate candidates.
 * Workspace out: candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxItems = 12;
constexpr uint32_t kMaxText = 192;
constexpr uint32_t kMaxShortcuts = 8192;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_shortcuts[kMaxShortcuts];

uint32_t g_shortcuts_len = 0;
bool g_loaded = false;
bool g_enabled = true;
bool g_templates = true;
uint32_t g_max_candidates = 4;

struct Item {
    char text[kMaxText];
    uint32_t text_len = 0;
    char comment[16];
    uint32_t comment_len = 0;
    char source[24];
    uint32_t source_len = 0;
    uint32_t score = 0;
};

const char* const kWeekdays[7] = {"星期日", "星期一", "星期二", "星期三",
                                  "星期四", "星期五", "星期六"};

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

void set_source(Item* item, const char* text) {
    set_source(item, text, cstr_len(text));
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
          const char* comment, const char* source, uint32_t score) {
    if (*count >= kMaxItems || text_len == 0 || text_len >= kMaxText) return;
    if (has_text(items, *count, text, text_len)) return;
    Item& item = items[(*count)++];
    set_text(&item, text, text_len);
    set_comment(&item, comment);
    set_source(&item, source);
    item.score = score;
}

bool load_shortcuts() {
    if (g_loaded) return g_shortcuts_len > 0;
    g_loaded = true;
    AssetHandle handle;
    if (!asset_open("shortcuts.tsv", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 &&
              size + 64 <= sizeof(g_shortcuts);
    uint32_t got = 0;
    if (ok)
        ok = asset_read(handle, 0, size, g_shortcuts, sizeof(g_shortcuts), &got) &&
             got > 0;
    asset_close(handle);
    if (ok) g_shortcuts_len = got;
    return ok;
}

uint32_t read_raw(const Frame& frame, const char** out) {
    static const char* const kSources[] = {
        "pime-pinyin-parse", "pime-wubi-parse", "pime-cangjie-parse",
        "pime-t9-parse", "pime-english-parse",
    };
    static const char* const kKeys[] = {"raw", "code", "code", "raw", "raw"};
    for (uint32_t i = 0; i < 5; ++i) {
        WorkspaceKey key = full_key(kSources[i], kKeys[i]);
        uint32_t len = 0;
        if (!span_text(frame.workspace_value(key.text), out, &len)) continue;
        if (len > 0) return len;
    }
    *out = nullptr;
    return 0;
}

bool same_key(const char* left, uint32_t left_len, const char* right,
              uint32_t right_len) {
    if (left_len != right_len) return false;
    for (uint32_t i = 0; i < left_len; ++i) {
        if (lower_ascii(left[i]) != lower_ascii(right[i])) return false;
    }
    return true;
}

/* ------------------------------------------------------------- templates */

void expand(const char* text, uint32_t len, Item* out) {
    Buffer b(out->text, kMaxText);
    const CivilTime now = civil_time(host_time(1), static_cast<int>(host_time(2)));

    uint32_t i = 0;
    while (i < len) {
        if (text[i] != '{') {
            b.append(text[i++]);
            continue;
        }
        uint32_t end = i + 1;
        while (end < len && text[end] != '}') ++end;
        if (end == len) {
            b.append(text[i++]);
            continue;
        }
        const char* name = text + i + 1;
        const uint32_t name_len = end - i - 1;
        char tmp[32];
        uint32_t n = 0;
        bool known = true;
        if (g_templates && text_equal(name, name_len, "date")) {
            n = format_uint(static_cast<uint64_t>(now.year), tmp, sizeof(tmp));
            b.append(tmp, n);
            b.append("年");
            n = format_uint(now.month, tmp, sizeof(tmp));
            b.append(tmp, n);
            b.append("月");
            n = format_uint(now.day, tmp, sizeof(tmp));
            b.append(tmp, n);
            b.append("日");
        } else if (g_templates && text_equal(name, name_len, "time")) {
            char two[4];
            n = format_uint(now.hour, tmp, sizeof(tmp));
            if (now.hour < 10) {
                two[0] = '0';
                format_uint(now.hour, two + 1, sizeof(two) - 1);
                b.append(two, 2);
            } else {
                b.append(tmp, n);
            }
            b.append(':');
            if (now.minute < 10) {
                two[0] = '0';
                format_uint(now.minute, two + 1, sizeof(two) - 1);
                b.append(two, 2);
            } else {
                n = format_uint(now.minute, tmp, sizeof(tmp));
                b.append(tmp, n);
            }
        } else if (g_templates && text_equal(name, name_len, "weekday")) {
            b.append(kWeekdays[now.weekday]);
        } else if (g_templates && text_equal(name, name_len, "iso")) {
            char two[4];
            n = format_uint(static_cast<uint64_t>(now.year), tmp, sizeof(tmp));
            b.append(tmp, n);
            b.append('-');
            two[0] = '0';
            format_uint(now.month, two + 1, sizeof(two) - 1);
            b.append(two, 2);
            b.append('-');
            two[0] = '0';
            format_uint(now.day, two + 1, sizeof(two) - 1);
            b.append(two, 2);
        } else {
            known = false;
        }
        if (!known) {
            b.append(text + i, end - i + 1);
        }
        i = end + 1;
    }
    out->text_len = b.len;
}

/* --------------------------------------------------------------- merging */

void copy_existing(const Frame& frame, Item* items, uint32_t* count) {
    static const char* const kSources[] = {
        "pime-special-candidates", "pime-pinyin-rank", "pime-rank-default",
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
            const char* source = nullptr;
            uint32_t source_len = 0;
            const char* comment = nullptr;
            uint32_t comment_len = 0;
            if (has_text(items, *count, text, text_len)) continue;
            Item& out = items[(*count)++];
            set_text(&out, text, text_len);
            if (item.text("source", &source, &source_len))
                set_source(&out, source, source_len);
            else
                set_source(&out, "dict");
            if (item.text("comment", &comment, &comment_len))
                set_comment(&out, comment);
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
    g_templates = true;
    g_max_candidates = 4;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    if (settings.boolean("templates", &value)) g_templates = value;
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

    const char* raw = nullptr;
    const uint32_t raw_len = g_enabled ? read_raw(frame, &raw) : 0;
    if (raw_len > 0 && load_shortcuts()) {
        const char* data = reinterpret_cast<const char*>(g_shortcuts);
        uint32_t i = 0;
        while (i < g_shortcuts_len && count == 0) {
            const char* line = data + i;
            uint32_t n = 0;
            while (i + n < g_shortcuts_len && data[i + n] != '\n' &&
                   data[i + n] != '\r')
                ++n;
            i += n;
            while (i < g_shortcuts_len &&
                   (data[i] == '\n' || data[i] == '\r'))
                ++i;
            if (n == 0 || line[0] == '#') continue;
            uint32_t key_len = 0;
            while (key_len < n && line[key_len] != '\t') ++key_len;
            if (key_len == 0 || key_len >= n) continue;
            if (!same_key(line, key_len, raw, raw_len)) continue;
            const char* text = line + key_len + 1;
            const uint32_t text_len = n - key_len - 1;
            if (text_len == 0) continue;
            Item expanded = {};
            expand(text, text_len, &expanded);
            push(items, &count, expanded.text, expanded.text_len, "快捷短语",
                 "shortcut", 9000);
        }
    }

    if (count > 0) {
        uint32_t existing = 0;
        Item merged[kMaxItems];
        for (uint32_t i = 0; i < count; ++i) merged[existing++] = items[i];
        uint32_t before = existing;
        copy_existing(frame, merged, &existing);
        (void)before;
        count = existing;
        if (count > g_max_candidates + 1) count = g_max_candidates + 1;
        for (uint32_t i = 0; i < count; ++i) items[i] = merged[i];
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
    g_shortcuts_len = 0;
    g_enabled = true;
    g_templates = true;
    g_max_candidates = 4;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
