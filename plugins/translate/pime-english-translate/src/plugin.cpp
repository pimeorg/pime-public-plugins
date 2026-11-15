/*
 * pime-english-translate: English fragment -> scored candidates.
 *
 * Reads plugin/pime-english-parse/raw and writes
 * plugin/pime-english-translate/candidates.
 *
 * The typed text keeps its own case and always comes first, so space, enter or
 * punctuation never silently replaces it with a dictionary word. Completion and
 * spelling suggestions follow, transformed to the case the user is typing:
 * all capitals stay all capitals, a leading capital keeps capitalising.
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
constexpr uint32_t kMaxTyped = 64;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kExactBoost = 10000;
constexpr uint32_t kLetterCost = 350;
constexpr uint32_t kSuggestionCost = 4000;
constexpr uint32_t kMaxDictBytes = 1024 * 1024;

const char kParseId[] = "pime-english-parse";

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_dict[kMaxDictBytes];
uint32_t g_dict_len = 0;
bool g_dict_loaded = false;

uint32_t g_max_candidates = 8;
uint32_t g_completion_limit = 24;
bool g_completion = true;
bool g_corrections = true;

Candidate g_candidates[kMaxCandidates];
uint32_t g_count = 0;

bool load_dict() {
    if (g_dict_loaded) return g_dict_len > 0;
    g_dict_loaded = true;
    AssetHandle handle;
    if (!asset_open("english.dict", &handle)) return false;
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

char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

uint32_t read_typed(const Frame& frame, char* out) {
    uint32_t len = 0;
    WorkspaceKey raw = full_key(kParseId, "raw");
    const char* text = nullptr;
    uint32_t text_len = 0;
    if (span_text(frame.workspace_value(raw.text), &text, &text_len)) {
        if (text_len > kMaxTyped) text_len = kMaxTyped;
        for (uint32_t i = 0; i < text_len; ++i) out[len++] = text[i];
    }
    return len;
}

/* Matching key: lowercase with apostrophes removed (`dont` finds `don't`). */
uint32_t match_key(const char* typed, uint32_t len, char* out) {
    uint32_t out_len = 0;
    for (uint32_t i = 0; i < len; ++i) {
        const char c = lower_ascii(typed[i]);
        if (c != '\'') out[out_len++] = c;
    }
    return out_len;
}

uint32_t append_utf8(uint32_t cp, char* out) {
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xc0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xe0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out[2] = static_cast<char>(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = static_cast<char>(0xf0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    out[3] = static_cast<char>(0x80 | (cp & 0x3f));
    return 4;
}

uint32_t decode_utf8(const char* text, uint32_t len, uint32_t* index) {
    const uint8_t b0 = static_cast<uint8_t>(text[*index]);
    uint32_t width = 1;
    uint32_t cp = b0;
    if (b0 >= 0xf0) {
        width = 4;
        cp = b0 & 0x07;
    } else if (b0 >= 0xe0) {
        width = 3;
        cp = b0 & 0x0f;
    } else if (b0 >= 0xc0) {
        width = 2;
        cp = b0 & 0x1f;
    }
    if (*index + width > len) width = 1;
    for (uint32_t i = 1; i < width; ++i)
        cp = (cp << 6) | (static_cast<uint8_t>(text[*index + i]) & 0x3f);
    *index += width;
    return cp;
}

/* Applies the typed capitalisation to a dictionary word. */
void copy_with_case(const char* word, uint32_t len, bool all_caps, bool cap,
                    char* out, uint32_t* out_len) {
    uint32_t written = 0;
    uint32_t index = 0;
    bool first = true;
    while (index < len) {
        uint32_t cp = decode_utf8(word, len, &index);
        if (first && !all_caps && cap && cp >= 'a' && cp <= 'z')
            cp = static_cast<uint32_t>(cp - 'a' + 'A');
        if (all_caps && cp >= 'a' && cp <= 'z')
            cp = static_cast<uint32_t>(cp - 'a' + 'A');
        first = false;
        written += append_utf8(cp, out + written);
    }
    *out_len = written;
}

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

bool key_starts_with(const char* key, uint32_t key_len, const char* typed,
                     uint32_t typed_len) {
    if (key_len < typed_len) return false;
    for (uint32_t i = 0; i < typed_len; ++i)
        if (key[i] != typed[i]) return false;
    return true;
}

/* Bounded Damerau-Levenshtein distance (transposition counts as one edit). */
uint32_t edit_distance(const char* a, uint32_t a_len, const char* b,
                       uint32_t b_len) {
    if (a_len > b_len + 2 || b_len > a_len + 2) return 3;
    uint32_t prev2[66];
    uint32_t prev[66];
    uint32_t row[66];
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

void lookup(const char* key, uint32_t key_len, bool all_caps, bool cap) {
    Dict dict;
    dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);

    char formed[128];
    uint32_t formed_len = 0;
    uint32_t completions = 0;
    bool matched = false;
    dict.each([&](const DictEntry& entry) {
        if (!key_starts_with(entry.key, entry.key_len, key, key_len)) return;
        matched = true;
        if (entry.key_len > key_len && !g_completion) return;
        if (entry.key_len > key_len && completions >= g_completion_limit) return;
        const bool exact = entry.key_len == key_len;
        copy_with_case(entry.text, entry.text_len, all_caps, cap, formed,
                       &formed_len);
        const uint32_t extra = entry.key_len - key_len;
        const int32_t score =
            static_cast<int32_t>(entry.freq) +
            static_cast<int32_t>(exact ? kExactBoost : 0) -
            static_cast<int32_t>(kLetterCost * extra);
        push_candidate(formed, formed_len, score,
                       exact ? "dict" : "completion", nullptr, 0);
        if (!exact) ++completions;
    });
    if (matched || !g_corrections) return;

    uint32_t found = 0;
    dict.each([&](const DictEntry& entry) {
        if (found >= g_max_candidates) return;
        const uint32_t distance = edit_distance(key, key_len, entry.key,
                                                entry.key_len);
        if (distance > 2) return;
        copy_with_case(entry.text, entry.text_len, all_caps, cap, formed,
                       &formed_len);
        const int32_t score = static_cast<int32_t>(entry.freq) -
                              static_cast<int32_t>(kSuggestionCost * distance);
        push_candidate(formed, formed_len, score, "suggestion", "拼写建议",
                       cstr_len("拼写建议"));
        ++found;
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
    g_max_candidates = 8;
    g_completion_limit = 24;
    g_completion = true;
    g_corrections = true;
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
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    if (!load_dict()) return PIME_NOT_FOUND;

    char typed[kMaxTyped];
    const uint32_t typed_len = read_typed(frame, typed);

    g_count = 0;
    if (typed_len > 0) {
        /* The typed text is never replaced without the user asking for it. */
        push_candidate(typed, typed_len, 0x7ffffff, "raw", nullptr, 0);

        char key[kMaxTyped];
        const uint32_t key_len = match_key(typed, typed_len, key);

        bool all_caps = typed_len > 1;
        for (uint32_t i = 0; i < typed_len; ++i) {
            if (typed[i] >= 'a' && typed[i] <= 'z') {
                all_caps = false;
                break;
            }
        }
        const bool cap = typed[0] >= 'A' && typed[0] <= 'Z';
        lookup(key, key_len, all_caps, cap);
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
    g_count = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"