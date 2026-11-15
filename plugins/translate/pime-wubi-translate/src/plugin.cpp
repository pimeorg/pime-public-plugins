/*
 * pime-wubi-translate: Wubi code -> scored candidates.
 *
 * Reads plugin/pime-wubi-parse/code, looks up the packaged Wubi dictionary, and
 * writes plugin/pime-wubi-translate/candidates.
 *
 * Three lookup modes, mirroring the engine this repository mirrors:
 *   - exact code: every matching entry, most frequent first;
 *   - completion: shorter codes list the rest of the code as a comment;
 *   - `z` + pinyin: reverse lookup, the Wubi code is shown as the comment.
 * When nothing matches a four-or-more-letter code, Damerau-Levenshtein
 * neighbours are offered as "拼写建议".
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/candidates.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxKey = 16;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kExactBoost = 10000;
constexpr uint32_t kCompletionCost = 200;
constexpr uint32_t kSuggestionCost = 4000;
constexpr uint32_t kMaxDictBytes = 65536;
constexpr uint32_t kMaxPinyinBytes = 1024 * 1024;
constexpr uint32_t kMaxReverse = 1024;

const char kParseId[] = "pime-wubi-parse";

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_dict[kMaxDictBytes];
uint32_t g_dict_len = 0;
bool g_dict_loaded = false;

alignas(8) uint8_t g_pinyin[kMaxPinyinBytes];
uint32_t g_pinyin_len = 0;
bool g_pinyin_loaded = false;

struct ReverseEntry {
    const char* text;
    uint32_t text_len;
    const char* key;
    uint32_t key_len;
};

ReverseEntry g_reverse[kMaxReverse];
uint32_t g_reverse_len = 0;
bool g_reverse_ready = false;

uint32_t g_max_candidates = 8;
uint32_t g_completion_limit = 24;
bool g_completion = true;
bool g_corrections = true;
bool g_pinyin_lookup = true;

Candidate g_candidates[kMaxCandidates];
uint32_t g_count = 0;

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

bool load_pinyin() {
    if (g_pinyin_loaded) return g_pinyin_len > 0;
    g_pinyin_loaded = true;
    AssetHandle handle;
    if (!asset_open("pinyin.dict", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 &&
              size + 64 <= sizeof(g_pinyin);
    if (ok) {
        uint32_t got = 0;
        ok = asset_read(handle, 0, size, g_pinyin, sizeof(g_pinyin), &got) &&
             got > 0;
        if (ok) g_pinyin_len = got;
    }
    asset_close(handle);
    return ok;
}

void build_reverse() {
    if (g_reverse_ready) return;
    g_reverse_ready = true;
    if (!load_dict()) return;
    Dict dict;
    dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);
    dict.each([&](const DictEntry& entry) {
        if (g_reverse_len >= kMaxReverse) return;
        for (uint32_t i = 0; i < g_reverse_len; ++i) {
            ReverseEntry& known = g_reverse[i];
            if (known.text_len == entry.text_len &&
                Frame::equal(known.text, known.text_len, entry.text,
                             entry.text_len)) {
                /* Keep the longest (full) code, as the engine does. */
                if (entry.key_len > known.key_len) {
                    known.key = entry.key;
                    known.key_len = entry.key_len;
                }
                return;
            }
        }
        ReverseEntry& slot = g_reverse[g_reverse_len++];
        slot.text = entry.text;
        slot.text_len = entry.text_len;
        slot.key = entry.key;
        slot.key_len = entry.key_len;
    });
}

bool reverse_code(const char* text, uint32_t len, const char** key,
                  uint32_t* key_len) {
    build_reverse();
    for (uint32_t i = 0; i < g_reverse_len; ++i) {
        if (g_reverse[i].text_len == len &&
            Frame::equal(g_reverse[i].text, len, text, len)) {
            *key = g_reverse[i].key;
            *key_len = g_reverse[i].key_len;
            return true;
        }
    }
    return false;
}

uint32_t read_code(const Frame& frame, char* key) {
    uint32_t key_len = 0;
    WorkspaceKey code = full_key(kParseId, "code");
    const char* text = nullptr;
    uint32_t len = 0;
    if (span_text(frame.workspace_value(code.text), &text, &len)) {
        for (uint32_t i = 0; i < len && key_len < kMaxKey; ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c >= 'a' && c <= 'z') key[key_len++] = c;
        }
    }
    return key_len;
}

void clear_candidates() { g_count = 0; }

void push_candidate(const char* text, uint32_t text_len, int32_t score,
                    const char* source, const char* comment,
                    uint32_t comment_len) {
    const uint32_t limit =
        g_max_candidates < kMaxCandidates ? g_max_candidates : kMaxCandidates;
    if (g_count >= limit) return;
    Candidate& c = g_candidates[g_count++];
    c.set_text(text, text_len);
    c.score = score;
    c.set_source(source);
    c.comment_len = 0;
    if (comment && comment_len) c.set_comment(comment, comment_len);
}

/* Bounded Damerau-Levenshtein distance (transposition counts as one edit). */
uint32_t edit_distance(const char* a, uint32_t a_len, const char* b,
                       uint32_t b_len) {
    if (a_len > b_len + 2 || b_len > a_len + 2) return 3;
    uint32_t prev2[18];
    uint32_t prev[18];
    uint32_t row[18];
    for (uint32_t j = 0; j <= b_len; ++j) prev[j] = j;
    for (uint32_t i = 1; i <= a_len; ++i) {
        row[0] = i;
        for (uint32_t j = 1; j <= b_len; ++j) {
            const uint32_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            uint32_t value = prev[j - 1] + cost;
            if (prev[j] + 1 < value) value = prev[j] + 1;
            if (row[j - 1] + 1 < value) value = row[j - 1] + 1;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
                a[i - 2] == b[j - 1] && prev2[j - 2] + 1 < value)
                value = prev2[j - 2] + 1;
            row[j] = value;
        }
        for (uint32_t j = 0; j <= b_len; ++j) {
            prev2[j] = prev[j];
            prev[j] = row[j];
        }
    }
    return prev[b_len];
}

void lookup_code(const char* key, uint32_t key_len) {
    Dict dict;
    dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);

    dict.match(key, key_len, [&](const DictEntry& entry) {
        push_candidate(entry.text, entry.text_len,
                       static_cast<int32_t>(entry.freq + kExactBoost), "dict",
                       nullptr, 0);
    });

    if (!g_completion || key_len >= 4) return;
    uint32_t completions = 0;
    dict.prefix(key, key_len, [&](const DictEntry& entry) {
        if (entry.key_len <= key_len || completions >= g_completion_limit)
            return;
        const uint32_t rest = entry.key_len - key_len;
        const int32_t score = static_cast<int32_t>(entry.freq) -
                              static_cast<int32_t>(kCompletionCost * rest);
        push_candidate(entry.text, entry.text_len, score, "completion",
                       entry.key + key_len, rest);
        ++completions;
    });
}

void suggest(const char* key, uint32_t key_len) {
    if (!g_corrections || key_len < 4) return;
    Dict dict;
    dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);
    uint32_t found = 0;
    dict.each([&](const DictEntry& entry) {
        if (found >= g_max_candidates) return;
        if (entry.key_len < 4) return;
        const uint32_t distance =
            edit_distance(key, key_len, entry.key, entry.key_len);
        if (distance > 2) return;
        const int32_t score =
            static_cast<int32_t>(entry.freq) -
            static_cast<int32_t>(kSuggestionCost * distance);
        push_candidate(entry.text, entry.text_len, score, "suggestion",
                       "拼写建议", cstr_len("拼写建议"));
        ++found;
    });
}

/* Compares the concatenated syllables of a dictionary key with the typed
 * letters; `syllable_end` reports how much of the key was consumed. */
bool pinyin_prefix(const char* key, uint32_t key_len, const char* typed,
                   uint32_t typed_len, bool* exact) {
    uint32_t i = 0;
    uint32_t j = 0;
    while (i < key_len && j < typed_len) {
        if (key[i] == ' ') {
            ++i;
            continue;
        }
        if (key[i] != typed[j]) return false;
        ++i;
        ++j;
    }
    if (j < typed_len) return false;
    while (i < key_len && key[i] == ' ') ++i;
    *exact = i >= key_len;
    return true;
}

void pinyin_lookup(const char* typed, uint32_t typed_len) {
    if (!load_pinyin()) return;
    Dict dict;
    dict.load(reinterpret_cast<const char*>(g_pinyin), g_pinyin_len);
    uint32_t exact = 0;
    uint32_t loose = 0;
    dict.each([&](const DictEntry& entry) {
        bool is_exact = false;
        if (!pinyin_prefix(entry.key, entry.key_len, typed, typed_len, &is_exact))
            return;
        if (!is_exact && loose >= g_completion_limit) return;
        if (is_exact && exact >= g_max_candidates) return;
        const char* code = nullptr;
        uint32_t code_len = 0;
        const bool has_code = reverse_code(entry.text, entry.text_len, &code,
                                           &code_len);
        const int32_t score = static_cast<int32_t>(
            entry.freq + (is_exact ? kExactBoost : 0));
        push_candidate(entry.text, entry.text_len, score, "pinyin-lookup",
                       has_code ? code : nullptr, has_code ? code_len : 0);
        if (is_exact)
            ++exact;
        else
            ++loose;
    });
}

void sort_and_write(Writer& w) {
    sort_candidates(g_candidates, g_count);
    uint32_t written = 0;
    for (uint32_t i = 0; i < g_count; ++i) {
        bool duplicate = false;
        for (uint32_t j = 0; j < written; ++j) {
            if (g_candidates[j].text_len == g_candidates[i].text_len &&
                Frame::equal(g_candidates[j].text, g_candidates[j].text_len,
                             g_candidates[i].text, g_candidates[i].text_len)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        g_candidates[written++] = g_candidates[i];
    }
    write_candidates(w, g_candidates, written);
    g_count = 0;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_dict_loaded = false;
    g_dict_len = 0;
    g_pinyin_loaded = false;
    g_pinyin_len = 0;
    g_reverse_ready = false;
    g_reverse_len = 0;
    g_max_candidates = 8;
    g_completion_limit = 24;
    g_completion = true;
    g_corrections = true;
    g_pinyin_lookup = true;
    g_count = 0;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t value = 0;
    if (settings.uint("max_candidates", &value) && value >= 1 && value <= 32)
        g_max_candidates = static_cast<uint32_t>(value);
    if (settings.uint("completion_limit", &value) && value >= 0 && value <= 32)
        g_completion_limit = static_cast<uint32_t>(value);
    bool flag = true;
    if (settings.boolean("completion", &flag)) g_completion = flag;
    if (settings.boolean("corrections", &flag)) g_corrections = flag;
    if (settings.boolean("pinyin_lookup", &flag)) g_pinyin_lookup = flag;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    if (!load_dict()) return PIME_NOT_FOUND;

    char key[kMaxKey];
    const uint32_t key_len = read_code(frame, key);

    clear_candidates();
    if (key_len > 1 && g_pinyin_lookup && key[0] == 'z') {
        pinyin_lookup(key + 1, key_len - 1);
    } else if (key_len > 0) {
        const uint32_t before = g_count;
        lookup_code(key, key_len);
        if (g_count == before) suggest(key, key_len);
    }

    const bool any = g_count > 0;
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    sort_and_write(w);
    w.key("handled");
    w.boolean(any);
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

PIME_EXPORT void pime_shutdown(void) {
    g_dict_loaded = false;
    g_dict_len = 0;
    g_pinyin_loaded = false;
    g_pinyin_len = 0;
    g_reverse_ready = false;
    g_reverse_len = 0;
    g_count = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
