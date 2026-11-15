/*
 * pime-t9-translate: decode a nine-key / 14-key code stream into candidates.
 *
 * The dictionary is the same syllable-keyed table the pinyin translator uses
 * (`pinyin.dict`, keys like "zhong guo"). Decoding mirrors the pinyin
 * translator: a lattice over the code stream, then a bounded beam search over
 * dictionary entries.
 *
 *   - Each dictionary syllable is encoded to the same key codes ("zhong" →
 *     "94664"), so a word covers a span of codes syllable by syllable.
 *   - A syllable may consume 1..len(code) codes as long as they are a prefix
 *     of its code; the missing codes cost `kAbbrevCost` each.
 *   - Words may be chained, so "9426" reaches both 先 (xian) and 西安 (xi+an).
 *   - `1` forces a syllable boundary: the stream is split into runs, each run
 *     is decoded on its own, and per-run candidates are concatenated by rank.
 *
 * Scoring matches the pinyin translator: log2(freq) - corpus constant +
 * cohesion per extra code - abbreviation cost.
 *
 * State: none. Workspace in: parse raw. Out: candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/t9.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kDictBytes = 1024 * 1024;
constexpr uint32_t kMaxEntries = 32768;
constexpr uint32_t kMaxCodes = 24;
constexpr uint32_t kMaxSyllables = 9;
constexpr uint32_t kBeam = 12;
constexpr uint32_t kEdgesPerSpan = 16;
constexpr uint32_t kPathBytes = 96;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kMaxRuns = 4;
constexpr uint32_t kMaxPerRun = 8;
constexpr uint32_t kBuckets = 24;

constexpr int32_t kContextLog = 26500;
constexpr int32_t kAbbrevCost = 2000;  /* per missing key code */
constexpr int32_t kWordBonus = 9000;   /* cohesion per extra code covered */
constexpr int32_t kScoreBase = 500000;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_dict[kDictBytes];
uint32_t g_dict_len = 0;
bool g_dict_loaded = false;

T9Grouping g_grouping = kT9Nine;
uint32_t g_max_candidates = 8;

struct Entry {
    const char* key;
    const char* text;
    uint32_t key_len;
    uint32_t text_len;
    int32_t score;
    uint8_t syllables;
};

Entry g_entries[kMaxEntries];
uint32_t g_entry_count = 0;
uint32_t g_bucket_begin[kBuckets + 1];

const char* g_input = nullptr;
uint32_t g_input_len = 0;

struct Edge {
    const char* text;
    uint32_t text_len;
    int32_t score;
    uint8_t next;
};

Edge g_edges[kMaxCodes][kMaxSyllables + 1][kEdgesPerSpan];
uint32_t g_edge_count[kMaxCodes][kMaxSyllables + 1];

struct Path {
    char text[kPathBytes];
    uint32_t len;
    int32_t score;
    uint8_t words;
};

Path g_beam[kMaxCodes + 1][kBeam];
uint32_t g_beam_count[kMaxCodes + 1];

struct Pick {
    char text[kPathBytes];
    uint32_t len;
    uint32_t score;
    uint32_t words;
};

uint32_t log2_score(uint32_t value) {
    if (value == 0) return 0;
    uint32_t bits = 0;
    uint32_t rest = value;
    while (rest >>= 1) ++bits;
    if (bits == 0) return 0;
    const uint32_t base = 1u << bits;
    const uint32_t fraction = static_cast<uint32_t>(
        (static_cast<uint64_t>(value - base) * 1000u) >> bits);
    return bits * 1000u + fraction;
}

uint32_t bucket_of_code(char c) {
    if (c >= '2' && c <= '9') return static_cast<uint32_t>(c - '2');
    if (c >= 'a' && c <= 'n') return 8u + static_cast<uint32_t>(c - 'a');
    return kBuckets - 1;
}

/* First key code of a syllable ("zhong" → '9'). */
char first_code(const char* syllable, uint32_t len) {
    if (len == 0) return 0;
    return t9_letter_code(g_grouping, syllable[0]);
}

bool load_dict() {
    if (g_dict_loaded) return g_dict_len > 0;
    g_dict_loaded = true;
    AssetHandle handle;
    if (!asset_open("pinyin.dict", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 && size + 64 <= sizeof(g_dict);
    if (ok) {
        uint32_t got = 0;
        ok = asset_read(handle, 0, size, g_dict, sizeof(g_dict), &got) && got > 0;
        if (ok) g_dict_len = got;
    }
    asset_close(handle);
    if (!ok) return false;

    Dict dict;
    dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);
    uint32_t counts[kBuckets] = {};
    dict.each([&](const DictEntry& e) {
        if (e.key_len == 0 || e.text_len == 0 || e.text_len + 1 > kPathBytes)
            return;
        uint8_t syllables = 1;
        for (uint32_t i = 0; i < e.key_len; ++i)
            if (e.key[i] == ' ') ++syllables;
        if (syllables > kMaxSyllables) return;
        uint32_t first_len = 0;
        while (first_len < e.key_len && e.key[first_len] != ' ') ++first_len;
        const char code = first_code(e.key, first_len);
        if (!code) return;
        counts[bucket_of_code(code)]++;
    });
    uint32_t total = 0;
    for (uint32_t c = 0; c < kBuckets; ++c) {
        g_bucket_begin[c] = total;
        total += counts[c];
    }
    g_bucket_begin[kBuckets] = total;
    if (total == 0 || total > kMaxEntries) return false;

    uint32_t cursor[kBuckets];
    for (uint32_t c = 0; c < kBuckets; ++c) cursor[c] = g_bucket_begin[c];
    dict.each([&](const DictEntry& e) {
        if (e.key_len == 0 || e.text_len == 0 || e.text_len + 1 > kPathBytes)
            return;
        uint8_t syllables = 1;
        for (uint32_t i = 0; i < e.key_len; ++i)
            if (e.key[i] == ' ') ++syllables;
        if (syllables > kMaxSyllables) return;
        uint32_t first_len = 0;
        while (first_len < e.key_len && e.key[first_len] != ' ') ++first_len;
        const char code = first_code(e.key, first_len);
        if (!code) return;
        const uint32_t bucket = bucket_of_code(code);
        if (cursor[bucket] >= g_bucket_begin[bucket + 1]) return;
        Entry& slot = g_entries[cursor[bucket]++];
        slot.key = e.key;
        slot.key_len = e.key_len;
        slot.text = e.text;
        slot.text_len = e.text_len;
        slot.score = static_cast<int32_t>(log2_score(e.freq));
        slot.syllables = syllables;
    });
    g_entry_count = total;
    return g_entry_count > 0;
}

void insert_edge(uint32_t position, uint32_t syllables, const Entry& entry,
                 uint32_t next, int32_t score) {
    Edge* edges = g_edges[position][syllables];
    uint32_t& count = g_edge_count[position][syllables];
    for (uint32_t i = 0; i < count; ++i) {
        if (edges[i].next == next && edges[i].text == entry.text &&
            edges[i].text_len == entry.text_len) {
            if (score <= edges[i].score) return;
            edges[i].score = score;
            while (i > 0 && edges[i - 1].score < edges[i].score) {
                const Edge swap = edges[i - 1];
                edges[i - 1] = edges[i];
                edges[i] = swap;
                --i;
            }
            return;
        }
    }
    uint32_t slot = count;
    while (slot > 0 && edges[slot - 1].score < score) --slot;
    if (count == kEdgesPerSpan && slot >= kEdgesPerSpan) return;
    const uint32_t last = count < kEdgesPerSpan ? count : kEdgesPerSpan - 1;
    for (uint32_t i = last; i > slot; --i) edges[i] = edges[i - 1];
    edges[slot].text = entry.text;
    edges[slot].text_len = entry.text_len;
    edges[slot].score = score;
    edges[slot].next = static_cast<uint8_t>(next);
    if (count < kEdgesPerSpan) ++count;
}

void add_edge(uint32_t position, const Entry& entry, uint32_t next,
              int32_t cost) {
    int32_t score = entry.score - kContextLog - cost;
    score += kWordBonus * static_cast<int32_t>(next - position - 1);
    insert_edge(position, entry.syllables, entry, next, score);
}

/* Try every way this entry can cover the codes from `start`. */
void align(const Entry& entry, uint32_t start, uint32_t syllable,
           uint32_t key_pos, uint32_t pos, int32_t cost) {
    if (syllable == entry.syllables) {
        add_edge(start, entry, pos, cost);
        return;
    }
    const uint32_t remaining = g_input_len - pos;
    if (remaining < entry.syllables - syllable) return;

    uint32_t key_end = key_pos;
    while (key_end < entry.key_len && entry.key[key_end] != ' ') ++key_end;
    const uint32_t syllable_len = key_end - key_pos;
    if (syllable_len == 0) return;
    char code[8];
    const uint32_t code_len =
        t9_encode(g_grouping, entry.key + key_pos, syllable_len, code);
    if (code_len == 0) return;
    const uint32_t next_key =
        key_end < entry.key_len ? key_end + 1 : entry.key_len;
    const uint32_t max_k = code_len < remaining ? code_len : remaining;
    for (uint32_t k = 1; k <= max_k; ++k) {
        bool match = true;
        for (uint32_t i = 0; i < k; ++i) {
            if (g_input[pos + i] != code[i]) {
                match = false;
                break;
            }
        }
        if (!match) break;
        align(entry, start, syllable + 1, next_key, pos + k,
              cost + static_cast<int32_t>(code_len - k) * kAbbrevCost);
    }
}

void build_edges(uint32_t position) {
    for (uint32_t m = 0; m <= kMaxSyllables; ++m)
        g_edge_count[position][m] = 0;
    const uint32_t bucket = bucket_of_code(g_input[position]);
    const uint32_t begin = g_bucket_begin[bucket];
    const uint32_t end = g_bucket_begin[bucket + 1];
    const uint32_t remaining = g_input_len - position;
    for (uint32_t index = begin; index < end; ++index) {
        const Entry& entry = g_entries[index];
        if (entry.syllables > remaining) continue;
        align(entry, position, 0, 0, position, 0);
    }
}

void insert_path(uint32_t position, const Path& path, int32_t score) {
    uint32_t count = g_beam_count[position];
    for (uint32_t i = 0; i < count; ++i) {
        Path& existing = g_beam[position][i];
        if (existing.len == path.len &&
            Frame::equal(existing.text, existing.len, path.text, path.len)) {
            if (score <= existing.score) return;
            existing.score = score;
            uint32_t slot = i;
            while (slot > 0 && g_beam[position][slot - 1].score <
                                   g_beam[position][slot].score) {
                const Path swap = g_beam[position][slot - 1];
                g_beam[position][slot - 1] = g_beam[position][slot];
                g_beam[position][slot] = swap;
                --slot;
            }
            return;
        }
    }
    if (count == kBeam && score <= g_beam[position][kBeam - 1].score) return;
    uint32_t slot = count;
    while (slot > 0 && g_beam[position][slot - 1].score < score) --slot;
    const uint32_t last = count < kBeam ? count : kBeam - 1;
    for (uint32_t i = last; i > slot; --i) g_beam[position][i] = g_beam[position][i - 1];
    g_beam[position][slot] = path;
    g_beam[position][slot].score = score;
    if (count < kBeam) ++g_beam_count[position];
}

/* Decodes one code run; results are copied out because the next run reuses the
 * beam buffers. */
uint32_t decode_run(const char* digits, uint32_t len, Pick* out,
                    uint32_t cap) {
    if (len == 0 || len > kMaxCodes) return 0;
    g_input = digits;
    g_input_len = len;
    for (uint32_t i = 0; i <= kMaxCodes; ++i) g_beam_count[i] = 0;
    g_beam[0][0].len = 0;
    g_beam[0][0].score = 0;
    g_beam[0][0].words = 0;
    g_beam_count[0] = 1;

    for (uint32_t i = 0; i < len; ++i) {
        if (g_beam_count[i] == 0) continue;
        build_edges(i);
        for (uint32_t p = 0; p < g_beam_count[i]; ++p) {
            const Path& base = g_beam[i][p];
            for (uint32_t m = 1; m <= kMaxSyllables; ++m) {
                if (i + m > len) break;
                for (uint32_t e = 0; e < g_edge_count[i][m]; ++e) {
                    const Edge& edge = g_edges[i][m][e];
                    if (base.len + edge.text_len >= kPathBytes) continue;
                    Path next;
                    for (uint32_t k = 0; k < base.len; ++k)
                        next.text[k] = base.text[k];
                    for (uint32_t k = 0; k < edge.text_len; ++k)
                        next.text[base.len + k] = edge.text[k];
                    next.len = base.len + edge.text_len;
                    next.words = static_cast<uint8_t>(base.words + 1);
                    insert_path(edge.next, next, base.score + edge.score);
                }
            }
        }
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < g_beam_count[len] && count < cap; ++i) {
        const Path& path = g_beam[len][i];
        int64_t score = static_cast<int64_t>(path.score) + kScoreBase;
        if (score < 0) score = 0;
        Pick& pick = out[count++];
        for (uint32_t k = 0; k < path.len; ++k) pick.text[k] = path.text[k];
        pick.len = path.len;
        pick.score = static_cast<uint32_t>(score);
        pick.words = path.words;
    }
    return count;
}

uint32_t split_runs(const char* raw, uint32_t raw_len, const char** runs,
                    uint32_t* lengths) {
    uint32_t count = 0;
    uint32_t i = 0;
    while (i < raw_len && count < kMaxRuns) {
        while (i < raw_len && raw[i] == '1') ++i;
        if (i >= raw_len) break;
        const uint32_t start = i;
        while (i < raw_len && raw[i] != '1') ++i;
        runs[count] = raw + start;
        lengths[count] = i - start;
        ++count;
    }
    return count;
}

uint32_t read_raw(const Frame& frame, const char** text) {
    WorkspaceKey key = full_key("pime-t9-parse", "raw");
    uint32_t len = 0;
    if (span_text(frame.workspace_value(key.text), text, &len)) return len;
    return 0;
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Pick* list, uint32_t count) {
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
        w.text(list[i].text, list[i].len);
        w.key("score");
        w.u64(list[i].score);
        w.key("source");
        w.text("t9");
        w.key("words");
        w.u64(list[i].words);
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
    g_dict_loaded = false;
    g_dict_len = 0;
    g_entry_count = 0;
    g_input = nullptr;
    g_input_len = 0;
    g_grouping = kT9Nine;
    g_max_candidates = 8;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    const char* grouping = nullptr;
    uint32_t grouping_len = 0;
    if (settings.text("grouping", &grouping, &grouping_len))
        t9_grouping_from_text(grouping, grouping_len, &g_grouping);
    uint64_t value = 0;
    if (settings.uint("max_candidates", &value) && value >= 1 &&
        value <= kMaxCandidates)
        g_max_candidates = static_cast<uint32_t>(value);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;
    if (!load_dict()) return PIME_NOT_FOUND;

    const char* raw = nullptr;
    const uint32_t raw_len = read_raw(frame, &raw);
    const char* runs[kMaxRuns];
    uint32_t lengths[kMaxRuns] = {};
    const uint32_t run_count = raw_len ? split_runs(raw, raw_len, runs, lengths) : 0;

    Pick result[kMaxCandidates];
    uint32_t count = 0;

    if (run_count == 1) {
        count = decode_run(runs[0], lengths[0], result, g_max_candidates);
    } else if (run_count > 1) {
        Pick per_run[kMaxRuns][kMaxPerRun];
        uint32_t sizes[kMaxRuns] = {};
        for (uint32_t r = 0; r < run_count; ++r)
            sizes[r] = decode_run(runs[r], lengths[r], per_run[r], kMaxPerRun);

        uint32_t depth = 0;
        for (uint32_t r = 0; r < run_count; ++r)
            if (sizes[r] > depth) depth = sizes[r];
        for (uint32_t i = 0; i < depth && count < g_max_candidates; ++i) {
            Pick combined;
            combined.len = 0;
            combined.score = 0;
            combined.words = 0;
            bool ok = true;
            for (uint32_t r = 0; r < run_count; ++r) {
                if (sizes[r] == 0) {
                    ok = false;
                    break;
                }
                const Pick& pick = per_run[r][i < sizes[r] ? i : 0];
                if (combined.len + pick.len >= kPathBytes) {
                    ok = false;
                    break;
                }
                for (uint32_t k = 0; k < pick.len; ++k)
                    combined.text[combined.len + k] = pick.text[k];
                combined.len += pick.len;
                combined.score += pick.score;
                combined.words += pick.words;
            }
            if (ok) result[count++] = combined;
        }
    }

    return respond(out, cap, out_len, result, count);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) {
    g_dict_loaded = false;
    g_dict_len = 0;
    g_entry_count = 0;
    g_input = nullptr;
    g_input_len = 0;
    g_grouping = kT9Nine;
    g_max_candidates = 8;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
