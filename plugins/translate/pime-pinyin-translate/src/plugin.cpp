/*
 * pime-pinyin-translate: raw-letter lattice -> scored candidates.
 *
 * The parser still publishes a display segmentation, but translation does not
 * trust it: every position of the raw letters is a node, and a dictionary
 * entry may cover a run of letters if each of its syllables is a prefix of
 * the chunk taken from that run. This handles all three input styles without
 * having to commit to one segmentation first:
 *
 *   wo shi   -> 我是      (full syllables)
 *   w s      -> 我是      (initials)
 *   w sh     -> 我是      (mixed partial syllables)
 *   hai bu gou -> 还不够  (or h b g)
 *
 * It also keeps valid alternative segmentations alive: "xian" can reach 先
 * (xian) and 西安 (xi an), "ysh" can reach 也是 (y/s/h or y/sh) and 有时候.
 *
 * Matching accepts fuzzy spellings (zh/z, ch/c, sh/s, n/l, an/ang, en/eng,
 * in/ing, ian/iang, uan/uang, ü/u) when settings.fuzzy is on. Candidates are
 * produced by a bounded beam search over the lattice, so combinations of one-
 * and multi-syllable words compose into sentences ("w s m" -> 我是吗).
 *
 * State: none. Output: plugin/pime-pinyin-translate/candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/memory.hpp>
#include <pimeplug/pinyin.hpp>
#include <pimeplug/utf8.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxInputBytes = 64;
constexpr uint32_t kMaxSyllables = 8;
constexpr uint32_t kMaxEntries = 32768;
constexpr uint32_t kDictBytes = 1024 * 1024;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kBeam = 16;
constexpr uint32_t kEdgesPerSpan = 24;
constexpr uint32_t kPathBytes = 96;
constexpr uint32_t kCollocationBytes = 512 * 1024;
constexpr uint32_t kCollocationSlots = 32768;

/* Scoring is a small unigram model over the dictionary. Frequencies are
 * counts, so each word contributes log2(freq); subtracting a corpus-scale
 * constant per word turns that into a log probability, which makes comparing
 * "one compound word" against "two single characters" meaningful. Cohesion
 * grows with the number of raw letters a word consumes: an abbreviation like
 * "dd" must reach 等等 rather than the two very frequent characters 的+的. */
constexpr int32_t kContextLog = 26500;
constexpr int32_t kAbbrevCost = 300;   /* per omitted letter */
constexpr int32_t kFuzzyCost = 2000;   /* per fuzzy initial/final */
constexpr int32_t kWordBonus = 9000;   /* compound-word cohesion per extra letter */
constexpr int32_t kScoreBase = 500000;
constexpr int32_t kCollocationFloor = 14000; /* ignore rare pairs */
constexpr int32_t kCollocationCap = 9000;   /* never outweigh word cohesion */

const char kParseId[] = "pime-pinyin-parse";

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_dict[kDictBytes];
uint32_t g_dict_len = 0;
bool g_dict_loaded = false;
bool g_fuzzy = true;
bool g_english_passthrough = true;
uint32_t g_max_candidates = 8;
uint32_t g_collocation_weight = 500;

/* Character collocations: adjacent-character log weight, hashed by
 * (left << 32) | right. Key 0 marks an empty slot; codepoint 0 is never
 * stored, so no separate occupancy array is needed. */
struct CollocationSlot {
    uint64_t key;
    int32_t score;
};

alignas(8) uint8_t g_collocation_buf[kCollocationBytes];
CollocationSlot g_collocations[kCollocationSlots];
uint32_t g_collocation_count = 0;
bool g_collocations_loaded = false;

struct Entry {
    const char* key;
    const char* text;
    uint32_t key_len;
    uint32_t text_len;
    uint32_t score;
    uint8_t syllables;
    uint8_t bucket;
};

Entry g_entries[kMaxEntries];
uint32_t g_entry_count = 0;
uint32_t g_bucket_begin[28];

char g_input[kMaxInputBytes];
uint32_t g_input_len = 0;

/* Mixed input: a trailing run of uppercase letters is kept verbatim and
 * appended to every pinyin candidate ("nihaoABC" -> 你好ABC). */
char g_tail[32];
uint32_t g_tail_len = 0;
char g_mixed_raw[kMaxInputBytes + 32];
uint32_t g_mixed_raw_len = 0;
bool g_mixed_uppercase = true;

/* Edges are bucketed by the number of syllables they cover. Keeping a
 * separate bucket per span length stops a crowd of high-frequency single
 * characters from pruning longer phrases (hbg -> hai bu gou) before the beam
 * ever sees them. */
struct Edge {
    const char* text;
    uint32_t text_len;
    int32_t score;
    uint32_t first_cp;
    uint32_t last_cp;
    uint8_t next;
};

Edge g_edges[kMaxInputBytes][kMaxSyllables + 1][kEdgesPerSpan];
uint32_t g_edge_count[kMaxInputBytes][kMaxSyllables + 1];

struct Path {
    char text[kPathBytes];
    uint32_t len;
    int32_t score;
    uint32_t last_cp;
    uint8_t words;
};

Path g_beam[kMaxInputBytes + 1][kBeam];
uint32_t g_beam_count[kMaxInputBytes + 1];

struct Candidate {
    char text[kPathBytes];
    uint32_t len;
    uint32_t score;
    const char* source;
    uint8_t words;
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

uint8_t bucket_of(char c) {
    if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(c - 'a');
    return 26;
}

uint32_t first_codepoint(const char* text, uint32_t len) {
    uint32_t used = 0;
    return utf8_decode(text, len, &used);
}

uint32_t last_codepoint(const char* text, uint32_t len) {
    uint32_t last = 0;
    uint32_t i = 0;
    while (i < len) {
        uint32_t used = 0;
        const uint32_t cp = utf8_decode(text + i, len - i, &used);
        if (used == 0) return last;
        last = cp;
        i += used;
    }
    return last;
}

uint32_t collocation_slot(uint64_t key) {
    return static_cast<uint32_t>((key * 0x9E3779B97F4A7C15ull) >> 33) &
           (kCollocationSlots - 1);
}

void collocation_insert(uint64_t key, int32_t score) {
    uint32_t slot = collocation_slot(key);
    for (uint32_t probes = 0; probes < kCollocationSlots; ++probes) {
        CollocationSlot& entry = g_collocations[slot];
        if (entry.key == key) {
            if (score > entry.score) entry.score = score;
            return;
        }
        if (entry.key == 0) {
            entry.key = key;
            entry.score = score;
            ++g_collocation_count;
            return;
        }
        slot = (slot + 1) & (kCollocationSlots - 1);
    }
}

int32_t collocation_score(uint32_t left, uint32_t right) {
    if (left == 0 || right == 0 || g_collocation_weight == 0) return 0;
    const uint64_t key = (static_cast<uint64_t>(left) << 32) | right;
    uint32_t slot = collocation_slot(key);
    for (uint32_t probes = 0; probes < kCollocationSlots; ++probes) {
        const CollocationSlot& entry = g_collocations[slot];
        if (entry.key == key) {
            int32_t bonus = static_cast<int32_t>(
                static_cast<int64_t>(entry.score) * g_collocation_weight / 1000);
            return bonus > kCollocationCap ? kCollocationCap : bonus;
        }
        if (entry.key == 0) return 0;
        slot = (slot + 1) & (kCollocationSlots - 1);
    }
    return 0;
}

/* Loads the character bigram table once. A weight of 0 disables the model and
 * skips the asset; any other weight requires the asset to be present because
 * the manifest declares it (a missing asset is a broken package, not a reason
 * to silently rank without the model). */
bool load_collocations() {
    if (g_collocations_loaded) return true;
    g_collocations_loaded = true;
    if (g_collocation_weight == 0) return true;

    AssetHandle handle;
    if (!asset_open("pinyin.collocations", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 &&
              size + 64 <= sizeof(g_collocation_buf);
    uint32_t got = 0;
    if (ok)
        ok = asset_read(handle, 0, size, g_collocation_buf,
                        sizeof(g_collocation_buf), &got) &&
             got > 0;
    asset_close(handle);
    if (!ok) return false;

    const char* data = reinterpret_cast<const char*>(g_collocation_buf);
    const uint32_t len = got;
    uint32_t i = 0;
    while (i < len) {
        const char* line = data + i;
        uint32_t n = 0;
        while (i + n < len && data[i + n] != '\n' && data[i + n] != '\r') ++n;
        i += n;
        while (i < len && (data[i] == '\n' || data[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;

        uint32_t used = 0;
        const uint32_t left = utf8_decode(line, n, &used);
        if (used == 0 || used >= n) continue;
        uint32_t used2 = 0;
        const uint32_t right = utf8_decode(line + used, n - used, &used2);
        if (used2 == 0) continue;
        const uint32_t pos = used + used2;
        if (pos >= n || line[pos] != '\t') continue;
        uint32_t weight = 0;
        if (!dict_parse_freq(line + pos + 1, n - pos - 1, &weight)) continue;
        const int32_t score =
            static_cast<int32_t>(log2_score(weight)) - kCollocationFloor;
        if (score <= 0) continue;
        collocation_insert((static_cast<uint64_t>(left) << 32) | right, score);
    }
    return g_collocation_count > 0;
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
    uint32_t counts[27] = {};
    dict.each([&](const DictEntry& e) {
        if (e.key_len == 0 || e.text_len == 0 || e.text_len + 1 > kPathBytes)
            return;
        uint8_t syllables = 1;
        for (uint32_t i = 0; i < e.key_len; ++i) {
            if (e.key[i] == ' ') ++syllables;
        }
        if (syllables == 0 || syllables > kMaxSyllables) return;
        counts[bucket_of(e.key[0])]++;
    });
    uint32_t total = 0;
    for (uint32_t c = 0; c < 27; ++c) {
        g_bucket_begin[c] = total;
        total += counts[c];
    }
    g_bucket_begin[27] = total;
    if (total == 0 || total > kMaxEntries) return false;

    uint32_t cursor[27];
    for (uint32_t c = 0; c < 27; ++c) cursor[c] = g_bucket_begin[c];
    dict.each([&](const DictEntry& e) {
        if (e.key_len == 0 || e.text_len == 0 || e.text_len + 1 > kPathBytes)
            return;
        uint8_t syllables = 1;
        for (uint32_t i = 0; i < e.key_len; ++i) {
            if (e.key[i] == ' ') ++syllables;
        }
        if (syllables == 0 || syllables > kMaxSyllables) return;
        const uint8_t bucket = bucket_of(e.key[0]);
        if (cursor[bucket] >= g_bucket_begin[bucket + 1]) return;
        Entry& slot = g_entries[cursor[bucket]++];
        slot.key = e.key;
        slot.key_len = e.key_len;
        slot.text = e.text;
        slot.text_len = e.text_len;
        slot.score = log2_score(e.freq);
        slot.syllables = syllables;
        slot.bucket = bucket;
    });
    g_entry_count = total;
    return g_entry_count > 0;
}

uint32_t canonical(const char* text, uint32_t len, char* out) {
    uint32_t i = 0;
    uint32_t o = 0;
    if (len >= 2 && text[0] == 'z' && text[1] == 'h') {
        out[o++] = 'z';
        i = 2;
    } else if (len >= 2 && text[0] == 'c' && text[1] == 'h') {
        out[o++] = 'c';
        i = 2;
    } else if (len >= 2 && text[0] == 's' && text[1] == 'h') {
        out[o++] = 's';
        i = 2;
    } else if (text[0] == 'l') {
        out[o++] = 'n';
        i = 1;
    } else {
        out[o++] = text[0];
        i = 1;
    }
    while (i < len) {
        if (i + 4 <= len && text[i] == 'i' && text[i + 1] == 'a' &&
            text[i + 2] == 'n' && text[i + 3] == 'g') {
            out[o++] = 'i';
            out[o++] = 'a';
            out[o++] = 'n';
            i += 4;
        } else if (i + 4 <= len && text[i] == 'u' && text[i + 1] == 'a' &&
                   text[i + 2] == 'n' && text[i + 3] == 'g') {
            out[o++] = 'u';
            out[o++] = 'a';
            out[o++] = 'n';
            i += 4;
        } else if (i + 3 <= len && text[i] == 'a' && text[i + 1] == 'n' &&
                   text[i + 2] == 'g') {
            out[o++] = 'a';
            out[o++] = 'n';
            i += 3;
        } else if (i + 3 <= len && text[i] == 'e' && text[i + 1] == 'n' &&
                   text[i + 2] == 'g') {
            out[o++] = 'e';
            out[o++] = 'n';
            i += 3;
        } else if (i + 3 <= len && text[i] == 'i' && text[i + 1] == 'n' &&
                   text[i + 2] == 'g') {
            out[o++] = 'i';
            out[o++] = 'n';
            i += 3;
        } else {
            char c = text[i++];
            if (c == 'v') c = 'u';
            out[o++] = c;
        }
    }
    return o;
}

bool match_chunk(const char* chunk, uint32_t chunk_len,
                 const char* syllable, uint32_t syllable_len,
                 int32_t* cost) {
    if (chunk_len == 0 || chunk_len > syllable_len) return false;
    if (Frame::equal(chunk, chunk_len, syllable, chunk_len)) {
        *cost = static_cast<int32_t>(syllable_len - chunk_len) * kAbbrevCost;
        return true;
    }
    if (!g_fuzzy) return false;
    char left[16];
    char right[16];
    const uint32_t left_len = canonical(chunk, chunk_len, left);
    const uint32_t right_len = canonical(syllable, syllable_len, right);
    if (left_len == 0 || left_len > right_len) return false;
    if (!Frame::equal(left, left_len, right, left_len)) return false;
    *cost = kFuzzyCost +
            static_cast<int32_t>(right_len - left_len) * kAbbrevCost;
    return true;
}

void insert_edge(uint32_t position, uint32_t syllables, const Entry& entry,
                 uint32_t next, int32_t score, uint32_t first_cp,
                 uint32_t last_cp) {
    Edge* edges = g_edges[position][syllables];
    uint32_t& count = g_edge_count[position][syllables];
    for (uint32_t i = 0; i < count; ++i) {
        if (edges[i].next == next && edges[i].text == entry.text &&
            edges[i].text_len == entry.text_len) {
            if (score <= edges[i].score) return;
            edges[i].score = score;
            while (i > 0 && edges[i - 1].score < edges[i].score) {
                Edge swap = edges[i - 1];
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
    edges[slot].first_cp = first_cp;
    edges[slot].last_cp = last_cp;
    edges[slot].next = static_cast<uint8_t>(next);
    if (count < kEdgesPerSpan) ++count;
}

void add_edge(uint32_t position, const Entry& entry, uint32_t next,
              int32_t cost) {
    int32_t score = static_cast<int32_t>(entry.score) - kContextLog - cost;
    /* Cohesion grows with the number of raw letters the word covers. A word
     * that consumes "zh" as one partial syllable is preferred over two words
     * that split it into z+h, while a three-syllable phrase still beats three
     * single characters. */
    score += kWordBonus * static_cast<int32_t>(next - position - 1);
    insert_edge(position, entry.syllables, entry, next, score,
                first_codepoint(entry.text, entry.text_len),
                last_codepoint(entry.text, entry.text_len));
}

/* Try every way this entry can cover the raw letters from `start`: each
 * syllable consumes between one letter (initial) and its full spelling. */
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
    const uint32_t max_k = syllable_len < remaining ? syllable_len : remaining;
    const uint32_t next_key =
        key_end < entry.key_len ? key_end + 1 : entry.key_len;
    for (uint32_t k = 1; k <= max_k; ++k) {
        int32_t chunk_cost = 0;
        if (!match_chunk(g_input + pos, k, entry.key + key_pos, syllable_len,
                         &chunk_cost))
            continue;
        align(entry, start, syllable + 1, next_key, pos + k,
              cost + chunk_cost);
    }
}

void build_edges(uint32_t position) {
    for (uint32_t m = 0; m <= kMaxSyllables; ++m)
        g_edge_count[position][m] = 0;
    const uint8_t bucket = bucket_of(g_input[position]);
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
            if (score > existing.score) {
                existing.score = score;
                /* Keep the beam ordered after a late, better arrival. */
                uint32_t slot = i;
                while (slot > 0 &&
                       g_beam[position][slot - 1].score <
                           g_beam[position][slot].score) {
                    Path swap = g_beam[position][slot - 1];
                    g_beam[position][slot - 1] = g_beam[position][slot];
                    g_beam[position][slot] = swap;
                    --slot;
                }
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

void compose() {
    for (uint32_t i = 0; i <= kMaxInputBytes; ++i) g_beam_count[i] = 0;
    g_beam[0][0].len = 0;
    g_beam[0][0].score = 0;
    g_beam[0][0].words = 0;
    g_beam_count[0] = 1;

    for (uint32_t i = 0; i < g_input_len; ++i) {
        if (g_beam_count[i] == 0) continue;
        build_edges(i);
        for (uint32_t p = 0; p < g_beam_count[i]; ++p) {
            const Path& base = g_beam[i][p];
            for (uint32_t m = 1; m <= kMaxSyllables; ++m) {
                if (i + m > g_input_len) break;
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
                    next.last_cp = edge.last_cp;
                    /* A word boundary is where the character bigram model
                     * applies: 他 + 们 is a natural pair, 他 + 猫 is not. */
                    const int32_t join =
                        base.len ? collocation_score(base.last_cp,
                                                     edge.first_cp)
                                 : 0;
                    insert_path(edge.next, next, base.score + edge.score + join);
                }
            }
        }
    }
}

void append_letter(char c) {
    if (g_input_len >= kMaxInputBytes) return;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c >= 'a' && c <= 'z') g_input[g_input_len++] = c;
}

/* True when the whole raw string splits into complete syllables, i.e. it can
 * only be pinyin and a raw-letters candidate would be noise. */
bool all_syllables(const char* text, uint32_t len) {
    bool reachable[kMaxInputBytes + 1] = {};
    reachable[0] = true;
    for (uint32_t i = 0; i < len; ++i) {
        if (!reachable[i]) continue;
        uint32_t limit = len - i;
        if (limit > 6) limit = 6;
        for (uint32_t n = 1; n <= limit; ++n) {
            if (is_syllable(text + i, n)) reachable[i + n] = true;
        }
    }
    return reachable[len];
}

uint32_t read_input(const Frame& frame) {
    g_input_len = 0;
    g_tail_len = 0;
    g_mixed_raw_len = 0;

    WorkspaceKey raw = full_key(kParseId, "raw");
    const char* text = nullptr;
    uint32_t len = 0;
    if (span_text(frame.workspace_value(raw.text), &text, &len)) {
        uint32_t split = len;
        if (g_mixed_uppercase) {
            /* Longest trailing uppercase run, but only when the prefix holds a
             * lowercase letter: "ABC" alone is not a mixed segment. */
            while (split > 0 && text[split - 1] >= 'A' &&
                   text[split - 1] <= 'Z')
                --split;
            bool has_lower = false;
            for (uint32_t i = 0; i < split; ++i) {
                if (text[i] >= 'a' && text[i] <= 'z') has_lower = true;
            }
            if (!has_lower || len - split < 2 || split == 0) split = len;
        }
        if (split < len && split > 0 && len - split < sizeof(g_tail) &&
            len < sizeof(g_mixed_raw)) {
            for (uint32_t i = 0; i < split; ++i) append_letter(text[i]);
            g_tail_len = len - split;
            for (uint32_t i = 0; i < g_tail_len; ++i) g_tail[i] = text[split + i];
            for (uint32_t i = 0; i < split; ++i) g_mixed_raw[i] = g_input[i];
            for (uint32_t i = 0; i < g_tail_len; ++i)
                g_mixed_raw[split + i] = g_tail[i];
            g_mixed_raw_len = split + g_tail_len;
        } else {
            for (uint32_t i = 0; i < len; ++i) append_letter(text[i]);
        }
        if (g_input_len > 0) return g_input_len;
    }

    /* No raw buffer (standalone use): reconstruct it from the syllables. */
    WorkspaceKey key = full_key(kParseId, "syllables");
    ArrayView array = ArrayView::of(frame.workspace_value(key.text));
    if (array.valid) {
        for (uint64_t i = 0; i < array.count; ++i) {
            const char* part = nullptr;
            uint32_t part_len = 0;
            if (!span_text(array.at(i), &part, &part_len)) continue;
            for (uint32_t k = 0; k < part_len; ++k) append_letter(part[k]);
        }
    }
    return g_input_len;
}


/* ------------------------------------------------------ corrected reading */

/* Correcting a fragmented literal reading (你好好 for nihhao) changes the number
 * of characters, so it needs a much larger gain; a single lexicon entry that
 * already covers the whole input is protected (WeaveText's typo_margin rule). */
constexpr int32_t kCorrectionMargin = 6000;

struct Built {
    Candidate list[kMaxCandidates];
    uint32_t count = 0;
    uint32_t best = 0;
};

bool has_text(const Candidate* list, uint32_t count, const char* text,
              uint32_t len) {
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i].len == len &&
            Frame::equal(list[i].text, list[i].len, text, len))
            return true;
    }
    return false;
}

uint32_t read_corrected(const Frame& frame, char* out, uint32_t cap) {
    WorkspaceKey key = full_key(kParseId, "corrected");
    const char* text = nullptr;
    uint32_t len = 0;
    if (!span_text(frame.workspace_value(key.text), &text, &len)) return 0;
    if (len == 0 || len >= cap) return 0;
    for (uint32_t i = 0; i < len; ++i) out[i] = lower_ascii(text[i]);
    out[len] = '\0';
    return len;
}

/* Collects candidates from the current g_input beam. */
void build_candidates(Built* out, bool allow_raw) {
    out->count = 0;
    out->best = 0;
    const bool pure = all_syllables(g_input, g_input_len);
    const bool offer_raw = allow_raw && g_english_passthrough &&
                           (!pure || g_beam_count[g_input_len] == 0);
    uint32_t limit =
        g_max_candidates < kMaxCandidates ? g_max_candidates : kMaxCandidates;
    if (offer_raw && limit > 1) --limit;
    for (uint32_t i = 0; i < g_beam_count[g_input_len] && out->count < limit;
         ++i) {
        const Path& path = g_beam[g_input_len][i];
        int64_t score = static_cast<int64_t>(path.score) + kScoreBase;
        if (score < 0) score = 0;
        Candidate& candidate = out->list[out->count++];
        for (uint32_t k = 0; k < path.len; ++k) candidate.text[k] = path.text[k];
        candidate.len = path.len;
        candidate.score = static_cast<uint32_t>(score);
        candidate.source = path.words > 1 ? "compose" : "dict";
        candidate.words = path.words;
        if (candidate.score > out->best) out->best = candidate.score;
    }
    for (uint32_t i = 1; i < out->count; ++i) {
        if (out->list[i].words > 1) {
            out->list[i].source = "sentence";
            break;
        }
    }
    if (g_tail_len > 0) {
        for (uint32_t i = 0; i < out->count; ++i) {
            if (out->list[i].len + g_tail_len >= kPathBytes) continue;
            for (uint32_t k = 0; k < g_tail_len; ++k)
                out->list[i].text[out->list[i].len + k] = g_tail[k];
            out->list[i].len += g_tail_len;
        }
        if (out->count >= g_max_candidates && out->count > 0) --out->count;
        {
            Candidate& mixed = out->list[out->count++];
            for (uint32_t k = 0; k < g_mixed_raw_len; ++k)
                mixed.text[k] = g_mixed_raw[k];
            mixed.len = g_mixed_raw_len;
            mixed.score = 1;
            mixed.source = "mixed";
            mixed.words = 1;
        }
    }
    if (offer_raw && g_tail_len == 0 && out->count < g_max_candidates) {
        Candidate& raw = out->list[out->count++];
        for (uint32_t k = 0; k < g_input_len; ++k) raw.text[k] = g_input[k];
        raw.len = g_input_len;
        raw.score = 1;
        raw.source = "raw";
        raw.words = 1;
    }
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_dict_loaded = false;
    g_dict_len = 0;
    g_entry_count = 0;
    g_input_len = 0;
    g_tail_len = 0;
    g_mixed_raw_len = 0;
    g_mixed_uppercase = true;
    g_fuzzy = true;
    g_english_passthrough = true;
    g_max_candidates = 8;
    g_collocation_weight = 500;
    g_collocation_count = 0;
    g_collocations_loaded = false;
    for (uint32_t i = 0; i < kCollocationSlots; ++i) {
        g_collocations[i].key = 0;
        g_collocations[i].score = 0;
    }
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (settings.valid) {
        uint64_t value = 0;
        if (settings.uint("max_candidates", &value) && value >= 1 && value <= 32)
            g_max_candidates = static_cast<uint32_t>(value);
        if (settings.uint("collocation_weight", &value) && value <= 2000)
            g_collocation_weight = static_cast<uint32_t>(value);
        bool fuzzy = true;
        if (settings.boolean("fuzzy", &fuzzy)) g_fuzzy = fuzzy;
        bool passthrough = true;
        if (settings.boolean("english_passthrough", &passthrough))
            g_english_passthrough = passthrough;
        bool mixed = true;
        if (settings.boolean("mixed_uppercase", &mixed))
            g_mixed_uppercase = mixed;
    }
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    if (!load_dict()) return PIME_NOT_FOUND;
    if (!load_collocations()) return PIME_NOT_FOUND;

    Candidate candidates[kMaxCandidates];
    uint32_t count = 0;

    Built literal;
    if (read_input(frame) > 0) {
        compose();
        build_candidates(&literal, true);
    }

    /* Auto-correction is an alternative reading, not a rewrite: the literal
     * keys have their own lattice ("xxie" -> 谢谢), and the corrected spelling
     * only takes over when it is clearly better. */
    Built fixed;
    char corrected[kMaxInputBytes];
    const uint32_t corrected_len = read_corrected(frame, corrected,
                                                  sizeof(corrected));
    if (corrected_len > 0 &&
        !(corrected_len == g_input_len &&
          Frame::equal(corrected, corrected_len, g_input, g_input_len))) {
        g_input_len = 0;
        g_tail_len = 0;
        g_mixed_raw_len = 0;
        for (uint32_t i = 0; i < corrected_len; ++i)
            append_letter(corrected[i]);
        if (g_input_len > 0) {
            compose();
            build_candidates(&fixed, false);
            for (uint32_t i = 0; i < fixed.count; ++i)
                fixed.list[i].source = "corrected";
        }
    }

    /* A single lexicon entry already covers the whole input: it is deliberate,
     * never delete letters to turn it into another word. */
    const bool literal_whole_word =
        literal.count > 0 && literal.list[0].words == 1;
    const bool use_corrected =
        fixed.count > 0 && !literal_whole_word &&
        (literal.count == 0 ||
         static_cast<int32_t>(fixed.best) >
             static_cast<int32_t>(literal.best) + kCorrectionMargin);
    const Built& primary = use_corrected ? fixed : literal;
    for (uint32_t i = 0; i < primary.count && count < kMaxCandidates; ++i)
        candidates[count++] = primary.list[i];

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
        w.text(candidates[i].text, candidates[i].len);
        w.key("score");
        w.u64(candidates[i].score);
        w.key("source");
        w.text(candidates[i].source);
        w.key("words");
        w.u64(candidates[i].words);
    }
    w.key("handled");
    w.boolean(count > 0);
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
    g_entry_count = 0;
    g_input_len = 0;
    g_collocation_count = 0;
    g_collocations_loaded = false;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
