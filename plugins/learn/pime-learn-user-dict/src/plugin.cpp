/*
 * pime-learn-user-dict: user dictionary learning.
 *
 * WeaveText parity (core/weave-engine/src/userdict.rs), split from the ranking
 * hot path:
 *   - append-only journal: every commit appends `log/<seq>`; a backspace right
 *     after a commit appends an "undo" tombstone instead of rewriting history.
 *   - decay: counters are stored as `<count>:<day>` and halved once per
 *     `decay_days` period, so long-idle words lose weight without a rewrite
 *     pass.
 *   - recent selection: MRU list of the last `max_recent` committed words,
 *     exposed as `recent` for hosts and later plugins.
 *   - backspace undo: a backspace within `undo_window_ms` after a commit
 *     decrements the counters written by that commit and drops the word from
 *     the MRU list.
 *   - continuous word creation: consecutive single-character commits are
 *     joined (`你` + `好` typed as ni + hao) and stored as a new word for the
 *     concatenated reading.
 *   - user bigrams: `next/<word1>\x1f<word2>` counters, exposed as `next`
 *     predictions after a commit.
 *
 * The plugin observes `committed` from pime-candidates-format, so it runs after
 * format. Learned words for the *current* reading are re-surfaced by the
 * ranking plugin (which owns the same-call display budget); this plugin owns
 * the cross-word statistics and the next-word predictions.
 *
 * State: last/prev word, continuous-word chain, MRU list, undo record, seq.
 * Workspace in: format/committed, keymap/action, parse raw.
 * Workspace out: candidates, next, recent, last.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/utf8.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxItems = 16;
constexpr uint32_t kMaxText = 96;
constexpr uint32_t kMaxKey = 256;
constexpr uint32_t kMaxRecent = 8;
constexpr uint32_t kMaxLog = 65535;
constexpr uint32_t kCountCap = 1000000000u;

alignas(8) uint8_t g_scratch[kScratchBytes];

bool g_enabled = true;
uint32_t g_decay_days = 30;
uint32_t g_undo_window_ms = 10000;
uint32_t g_max_candidates = 8;
uint32_t g_max_recent = 8;

struct Item {
    char text[kMaxText];
    uint32_t text_len = 0;
    char comment[24];
    uint32_t comment_len = 0;
    char source[32];
    uint32_t source_len = 0;
    uint32_t score = 0;
};

struct State {
    char last[kMaxText];
    uint32_t last_len = 0;
    char prev[kMaxText];
    uint32_t prev_len = 0;
    char chain[kMaxText];
    uint32_t chain_len = 0;
    char chain_raw[kMaxText];
    uint32_t chain_raw_len = 0;
    char recent[kMaxRecent][kMaxText];
    uint32_t recent_len[kMaxRecent] = {0};
    uint32_t recent_count = 0;
    char undo_word[kMaxText];
    uint32_t undo_word_len = 0;
    char undo_raw[kMaxText];
    uint32_t undo_raw_len = 0;
    char undo_prev[kMaxText];
    uint32_t undo_prev_len = 0;
    uint64_t undo_ms = 0;
    uint32_t seq = 0;
};

struct Counter {
    uint32_t count = 0;
    uint32_t day = 0;
    bool found = false;
};

uint32_t utf8_chars(const char* text, uint32_t len) {
    uint32_t i = 0;
    uint32_t n = 0;
    while (i < len) {
        uint32_t used = 0;
        utf8_decode(text + i, len - i, &used);
        if (used == 0) break;
        i += used;
        ++n;
    }
    return n;
}

/* Single CJK ideograph (3-byte UTF-8 lead, no ASCII/latin). */
bool single_han(const char* text, uint32_t len) {
    return utf8_chars(text, len) == 1 &&
           static_cast<uint8_t>(text[0]) >= 0xe0;
}

bool all_ascii_letters(const char* text, uint32_t len) {
    if (len == 0) return false;
    for (uint32_t i = 0; i < len; ++i) {
        const char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

void set_source(Item* item, const char* source, uint32_t len) {
    if (len > sizeof(item->source)) len = sizeof(item->source);
    for (uint32_t i = 0; i < len; ++i) item->source[i] = source[i];
    item->source_len = len;
}

bool copy_text(char* out, uint32_t* out_len, const char* text, uint32_t len) {
    if (!text || len >= kMaxText) return false;
    for (uint32_t i = 0; i < len; ++i) out[i] = text[i];
    out[len] = '\0';
    *out_len = len;
    return true;
}

void append_text(char* out, uint32_t* out_len, const char* text, uint32_t len) {
    if (*out_len + len >= kMaxText) return;
    for (uint32_t i = 0; i < len; ++i) out[*out_len + i] = text[i];
    *out_len += len;
    out[*out_len] = '\0';
}

uint32_t make_key(const char* prefix, const char* raw, uint32_t raw_len,
                  const char* word, uint32_t word_len, char* out) {
    uint32_t n = 0;
    for (const char* p = prefix; *p && n < kMaxKey - 2; ++p) out[n++] = *p;
    for (uint32_t i = 0; i < raw_len && n < kMaxKey - 2; ++i) out[n++] = raw[i];
    if (word) {
        if (n < kMaxKey - 2) out[n++] = '\x1f';
        for (uint32_t i = 0; i < word_len && n < kMaxKey - 1; ++i)
            out[n++] = word[i];
    }
    out[n] = '\0';
    return n;
}

uint32_t today() {
    return static_cast<uint32_t>((host_time(1) / 86400000ull) & 0xffffffffu);
}

void parse_counter(const char* value, uint32_t len, Counter* out) {
    out->found = true;
    uint32_t i = 0;
    uint64_t count = 0;
    while (i < len && value[i] >= '0' && value[i] <= '9') {
        count = count * 10 + static_cast<uint64_t>(value[i] - '0');
        ++i;
    }
    uint64_t day = 0;
    if (i < len && value[i] == ':') {
        ++i;
        while (i < len && value[i] >= '0' && value[i] <= '9') {
            day = day * 10 + static_cast<uint64_t>(value[i] - '0');
            ++i;
        }
    }
    out->count = count > kCountCap ? kCountCap : static_cast<uint32_t>(count);
    out->day = static_cast<uint32_t>(day);
}

void load_counter(const char* key, uint32_t key_len, Counter* out) {
    char value[32];
    uint32_t value_len = 0;
    bool found = false;
    *out = Counter{};
    if (!store_get(key, key_len, value, sizeof(value), &value_len, &found) ||
        !found)
        return;
    parse_counter(value, value_len, out);
}

/* Halves once per elapsed decay period (never below 1). */
uint32_t decayed(uint32_t count, uint32_t day) {
    if (g_decay_days == 0 || count <= 1) return count;
    const uint32_t now = today();
    if (now <= day) return count;
    uint32_t periods = (now - day) / g_decay_days;
    while (periods > 0 && count > 1) {
        count = (count + 1) / 2;
        --periods;
    }
    return count;
}

void save_counter(const char* key, uint32_t key_len, uint32_t count,
                  uint32_t day) {
    char value[32];
    uint32_t n = format_uint(count, value, sizeof(value));
    value[n++] = ':';
    n += format_uint(day, value + n, sizeof(value) - n);
    store_set(key, key_len, value, n);
}

void bump_counter(const char* key, uint32_t key_len, uint32_t add) {
    Counter counter;
    load_counter(key, key_len, &counter);
    uint32_t count = counter.found ? decayed(counter.count, counter.day) : 0;
    count += add;
    if (count > kCountCap) count = kCountCap;
    save_counter(key, key_len, count, today());
}

void decrement_counter(const char* key, uint32_t key_len) {
    Counter counter;
    load_counter(key, key_len, &counter);
    if (!counter.found) return;
    uint32_t count = decayed(counter.count, counter.day);
    if (count > 0) --count;
    save_counter(key, key_len, count, today());
}

/* ------------------------------------------------------------------ state */

void load_state(const Frame& frame, State* state) {
    MapView root = MapView::of(frame.root.get("state"));
    const char* text = nullptr;
    uint32_t len = 0;
    if (root.text("last", &text, &len)) copy_text(state->last, &state->last_len, text, len);
    if (root.text("prev", &text, &len)) copy_text(state->prev, &state->prev_len, text, len);
    if (root.text("chain", &text, &len)) copy_text(state->chain, &state->chain_len, text, len);
    if (root.text("chain_raw", &text, &len))
        copy_text(state->chain_raw, &state->chain_raw_len, text, len);
    if (root.text("undo_word", &text, &len))
        copy_text(state->undo_word, &state->undo_word_len, text, len);
    if (root.text("undo_raw", &text, &len))
        copy_text(state->undo_raw, &state->undo_raw_len, text, len);
    if (root.text("undo_prev", &text, &len))
        copy_text(state->undo_prev, &state->undo_prev_len, text, len);
    uint64_t value = 0;
    if (root.uint("undo_ms", &value)) state->undo_ms = value;
    if (root.uint("seq", &value)) state->seq = static_cast<uint32_t>(value);

    ArrayView recent = ArrayView::of(root.get("recent"));
    if (recent.valid) {
        for (uint64_t i = 0; i < recent.count && state->recent_count < g_max_recent;
             ++i) {
            const char* item = nullptr;
            uint32_t item_len = 0;
            if (!span_text(recent.at(i), &item, &item_len)) continue;
            if (!copy_text(state->recent[state->recent_count], &item_len, item,
                           item_len))
                continue;
            state->recent_len[state->recent_count] = item_len;
            ++state->recent_count;
        }
    }
}

void write_state(Writer& w, const State& state) {
    uint32_t fields = 1;
    if (state.last_len) ++fields;
    if (state.prev_len) ++fields;
    if (state.chain_len) ++fields;
    if (state.chain_raw_len) ++fields;
    if (state.undo_word_len) ++fields;
    if (state.undo_raw_len) ++fields;
    if (state.undo_prev_len) ++fields;
    if (state.undo_ms) ++fields;
    if (state.seq) ++fields;
    w.key("state");
    w.map(fields);
    w.key("recent");
    w.array(state.recent_count);
    for (uint32_t i = 0; i < state.recent_count; ++i)
        w.text(state.recent[i], state.recent_len[i]);
    if (state.last_len) {
        w.key("last");
        w.text(state.last, state.last_len);
    }
    if (state.prev_len) {
        w.key("prev");
        w.text(state.prev, state.prev_len);
    }
    if (state.chain_len) {
        w.key("chain");
        w.text(state.chain, state.chain_len);
    }
    if (state.chain_raw_len) {
        w.key("chain_raw");
        w.text(state.chain_raw, state.chain_raw_len);
    }
    if (state.undo_word_len) {
        w.key("undo_word");
        w.text(state.undo_word, state.undo_word_len);
    }
    if (state.undo_raw_len) {
        w.key("undo_raw");
        w.text(state.undo_raw, state.undo_raw_len);
    }
    if (state.undo_prev_len) {
        w.key("undo_prev");
        w.text(state.undo_prev, state.undo_prev_len);
    }
    if (state.undo_ms) {
        w.key("undo_ms");
        w.u64(state.undo_ms);
    }
    if (state.seq) {
        w.key("seq");
        w.u64(state.seq);
    }
}

void recent_push(State* state, const char* word, uint32_t len) {
    uint32_t existing = state->recent_count;
    for (uint32_t i = 0; i < state->recent_count; ++i) {
        if (state->recent_len[i] == len &&
            Frame::equal(state->recent[i], state->recent_len[i], word, len)) {
            existing = i;
            break;
        }
    }
    if (existing == state->recent_count) {
        if (state->recent_count < g_max_recent) ++state->recent_count;
        existing = state->recent_count - 1;
    }
    for (uint32_t i = existing; i > 0; --i) {
        for (uint32_t k = 0; k < kMaxText; ++k)
            state->recent[i][k] = state->recent[i - 1][k];
        state->recent_len[i] = state->recent_len[i - 1];
    }
    uint32_t copied = 0;
    copy_text(state->recent[0], &copied, word, len);
    state->recent_len[0] = copied;
}

void recent_remove(State* state, const char* word, uint32_t len) {
    for (uint32_t i = 0; i < state->recent_count; ++i) {
        if (state->recent_len[i] != len ||
            !Frame::equal(state->recent[i], state->recent_len[i], word, len))
            continue;
        for (uint32_t k = i; k + 1 < state->recent_count; ++k) {
            for (uint32_t j = 0; j < kMaxText; ++j)
                state->recent[k][j] = state->recent[k + 1][j];
            state->recent_len[k] = state->recent_len[k + 1];
        }
        --state->recent_count;
        return;
    }
}

/* ------------------------------------------------------------------ input */

uint32_t read_raw(const Frame& frame, char* out, uint32_t cap) {
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
        if (len >= cap) len = cap - 1;
        for (uint32_t k = 0; k < len; ++k) out[k] = text[k];
        out[len] = '\0';
        return len;
    }
    return 0;
}

uint32_t read_committed(const Frame& frame, char* out, uint32_t cap) {
    WorkspaceKey key = full_key("pime-candidates-format", "committed");
    const char* text = nullptr;
    uint32_t len = 0;
    if (!span_text(frame.workspace_value(key.text), &text, &len)) return 0;
    if (len >= cap) len = cap - 1;
    for (uint32_t k = 0; k < len; ++k) out[k] = text[k];
    out[len] = '\0';
    return len;
}

uint32_t read_action(const Frame& frame, const char** out) {
    WorkspaceKey key = full_key("pime-keymap-default", "action");
    uint32_t len = 0;
    if (!span_text(frame.workspace_value(key.text), out, &len)) return 0;
    return len;
}

void append_log(const State* state, const char* tag, const char* raw,
                uint32_t raw_len, const char* word, uint32_t word_len) {
    if (state->seq >= kMaxLog) return;
    char key[32];
    uint32_t key_len = 0;
    key_len = make_key("log/", nullptr, 0, nullptr, 0, key);
    key[key_len] = '\0';
    uint32_t n = format_uint(state->seq, key + key_len, sizeof(key) - key_len);
    key_len += n;

    char value[256];
    Buffer b(value, sizeof(value));
    const uint32_t stamp = static_cast<uint32_t>(host_time(1) / 1000ull);
    char number[16];
    b.append(tag);
    b.append('|');
    b.append(number, format_uint(stamp, number, sizeof(number)));
    b.append('|');
    b.append(raw, raw_len);
    b.append('|');
    b.append(word, word_len);
    store_set(key, key_len, b.data, b.len);
}

/* ------------------------------------------------------------------ merge */

/* Best candidate array written earlier in the chain. */
bool read_existing(const Frame& frame, Item* items, uint32_t* count) {
    static const char* const kSources[] = {"pime-special-candidates",
                                           "pime-pinyin-rank",
                                           "pime-rank-default",
                                           "pime-pinyin-translate"};
    for (const char* id : kSources) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (!array.valid || array.count == 0) continue;
        for (uint64_t i = 0; i < array.count && *count < kMaxItems; ++i) {
            MapView item = MapView::of(array.at(i));
            if (!item.valid) continue;
            const char* text = nullptr;
            uint32_t text_len = 0;
            if (!item.text("text", &text, &text_len)) continue;
            Item& out = items[(*count)++];
            if (!copy_text(out.text, &out.text_len, text, text_len)) {
                --(*count);
                continue;
            }
            const char* comment = nullptr;
            uint32_t comment_len = 0;
            if (item.text("comment", &comment, &comment_len))
                copy_text(out.comment, &out.comment_len, comment, comment_len);
            const char* source = nullptr;
            uint32_t source_len = 0;
            if (item.text("source", &source, &source_len))
                set_source(&out, source, source_len);
            uint64_t score = 0;
            item.uint("score", &score);
            out.score = static_cast<uint32_t>(score);
        }
        return true;
    }
    return false;
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

/* Learned words for the current reading, before the existing candidates. */
void learned_for_raw(const char* raw, uint32_t raw_len, Item* items,
                     uint32_t* count) {
    if (raw_len == 0) return;
    char prefix[kMaxKey];
    const uint32_t prefix_len = make_key("pair/", raw, raw_len, nullptr, 0, prefix);

    struct Found {
        char word[kMaxText];
        uint32_t len;
        uint32_t score;
    } found[kMaxItems];
    uint32_t found_count = 0;

    store_scan(prefix, prefix_len, kMaxItems, [&](const char* key,
                                                 uint32_t key_len,
                                                 const char* value,
                                                 uint32_t value_len) {
        if (key_len <= prefix_len + 1) return true;
        const char* word = key + prefix_len + 1;
        const uint32_t word_len = key_len - prefix_len - 1;
        if (word_len >= kMaxText) return true;
        Counter counter;
        parse_counter(value, value_len, &counter);
        const uint32_t score = decayed(counter.count, counter.day) * 1000u;
        if (score == 0) return true;
        if (found_count < kMaxItems) {
            Found& entry = found[found_count++];
            copy_text(entry.word, &entry.len, word, word_len);
            entry.score = score;
        }
        return true;
    });

    /* Most frequent first. */
    for (uint32_t i = 1; i < found_count; ++i) {
        const Found value = found[i];
        uint32_t j = i;
        while (j > 0 && found[j - 1].score < value.score) {
            found[j] = found[j - 1];
            --j;
        }
        found[j] = value;
    }

    for (uint32_t i = 0; i < found_count && *count < kMaxItems; ++i) {
        if (has_text(items, *count, found[i].word, found[i].len)) continue;
        Item& out = items[(*count)++];
        copy_text(out.text, &out.text_len, found[i].word, found[i].len);
        copy_text(out.comment, &out.comment_len, "自造", 6);
        set_source(&out, "user", 4);
        out.score = found[i].score;
    }
}

void bigram_next(const State& state, Item* items, uint32_t* count) {
    if (state.last_len == 0) return;
    char prefix[kMaxKey];
    const uint32_t prefix_len =
        make_key("next/", state.last, state.last_len, nullptr, 0, prefix);
    store_scan(prefix, prefix_len, kMaxItems, [&](const char* key,
                                                uint32_t key_len,
                                                const char* value,
                                                uint32_t value_len) {
        if (key_len <= prefix_len + 1) return true;
        const char* word = key + prefix_len + 1;
        const uint32_t word_len = key_len - prefix_len - 1;
        if (word_len >= kMaxText) return true;
        if (has_text(items, *count, word, word_len)) return true;
        Counter counter;
        parse_counter(value, value_len, &counter);
        const uint32_t score = decayed(counter.count, counter.day);
        if (score == 0) return true;
        if (*count >= kMaxItems) return false;
        Item& out = items[(*count)++];
        copy_text(out.text, &out.text_len, word, word_len);
        set_source(&out, "user-bigram", 11);
        out.score = score;
        return true;
    });

    for (uint32_t i = 1; i < *count; ++i) {
        const Item value = items[i];
        uint32_t j = i;
        while (j > 0 && items[j - 1].score < value.score) {
            items[j] = items[j - 1];
            --j;
        }
        items[j] = value;
    }
}

/* ------------------------------------------------------------------ output */

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const State& state, const Item* candidates,
                    uint32_t candidate_count, const Item* next,
                    uint32_t next_count) {
    Writer w(out, cap);
    w.map(4);
    write_state(w, state);

    w.key("workspace");
    w.map(4);
    w.key("candidates");
    w.array(candidate_count);
    for (uint32_t i = 0; i < candidate_count; ++i) {
        const Item& item = candidates[i];
        const bool has_comment = item.comment_len > 0;
        w.map(has_comment ? 4 : 3);
        w.key("text");
        w.text(item.text, item.text_len);
        w.key("score");
        w.u64(item.score);
        w.key("source");
        w.text(item.source, item.source_len);
        if (has_comment) {
            w.key("comment");
            w.text(item.comment, item.comment_len);
        }
    }
    w.key("next");
    w.array(next_count);
    for (uint32_t i = 0; i < next_count; ++i) {
        w.map(3);
        w.key("text");
        w.text(next[i].text, next[i].text_len);
        w.key("score");
        w.u64(next[i].score);
        w.key("source");
        w.text(next[i].source, next[i].source_len);
    }
    w.key("recent");
    w.array(state.recent_count);
    for (uint32_t i = 0; i < state.recent_count; ++i)
        w.text(state.recent[i], state.recent_len[i]);
    w.key("last");
    w.text(state.last, state.last_len);

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
    g_decay_days = 30;
    g_undo_window_ms = 10000;
    g_max_candidates = 8;
    g_max_recent = 8;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool boolean = true;
    if (settings.boolean("enabled", &boolean)) g_enabled = boolean;
    uint64_t value = 0;
    if (settings.uint("decay_days", &value) && value <= 3650)
        g_decay_days = static_cast<uint32_t>(value);
    if (settings.uint("undo_window_ms", &value) && value <= 60000)
        g_undo_window_ms = static_cast<uint32_t>(value);
    if (settings.uint("max_candidates", &value) && value >= 1 &&
        value <= kMaxItems)
        g_max_candidates = static_cast<uint32_t>(value);
    if (settings.uint("max_recent", &value) && value >= 1 && value <= kMaxRecent)
        g_max_recent = static_cast<uint32_t>(value);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    State state;
    load_state(frame, &state);

    char raw[kMaxKey];
    const uint32_t raw_len = read_raw(frame, raw, sizeof(raw));
    char committed[kMaxText];
    uint32_t committed_len = 0;
    if (g_enabled) committed_len = read_committed(frame, committed, sizeof(committed));
    const char* action = nullptr;
    const uint32_t action_len = read_action(frame, &action);
    const uint64_t now = host_time(1);

    if (committed_len > 0) {
        /* Undo record for the next backspace. */
        copy_text(state.undo_word, &state.undo_word_len, committed, committed_len);
        copy_text(state.undo_raw, &state.undo_raw_len, raw, raw_len);
        copy_text(state.undo_prev, &state.undo_prev_len, state.last, state.last_len);
        state.undo_ms = now;

        const uint32_t day = today();
        char key[kMaxKey];
        uint32_t key_len = make_key("word/", nullptr, 0, committed, committed_len, key);
        bump_counter(key, key_len, 1);
        if (raw_len > 0) {
            key_len = make_key("pair/", raw, raw_len, committed, committed_len, key);
            bump_counter(key, key_len, 1);
        }

        /* User bigram from the previously committed word. */
        if (state.last_len > 0 && !(state.last_len == committed_len &&
                                    Frame::equal(state.last, state.last_len,
                                                 committed, committed_len))) {
            key_len = make_key("next/", state.last, state.last_len, committed,
                               committed_len, key);
            bump_counter(key, key_len, 1);
        }

        /* Continuous word creation: single characters join into a word. */
        bool continuous = false;
        if (single_han(committed, committed_len) &&
            all_ascii_letters(raw, raw_len)) {
            const uint32_t before = state.chain_len;
            append_text(state.chain, &state.chain_len, committed, committed_len);
            append_text(state.chain_raw, &state.chain_raw_len, raw, raw_len);
            continuous = state.chain_len > before;
        } else {
            state.chain_len = 0;
            state.chain_raw_len = 0;
        }
        if (continuous && utf8_chars(state.chain, state.chain_len) >= 2) {
            key_len = make_key("word/", nullptr, 0, state.chain, state.chain_len, key);
            bump_counter(key, key_len, 1);
            key_len = make_key("pair/", state.chain_raw, state.chain_raw_len,
                               state.chain, state.chain_len, key);
            bump_counter(key, key_len, 1);
            append_log(&state, "new", state.chain_raw, state.chain_raw_len,
                       state.chain, state.chain_len);
            if (state.seq < kMaxLog) ++state.seq;
        }

        append_log(&state, "commit", raw, raw_len, committed, committed_len);
        if (state.seq < kMaxLog) ++state.seq;

        recent_push(&state, committed, committed_len);
        if (state.last_len > 0)
            copy_text(state.prev, &state.prev_len, state.last, state.last_len);
        copy_text(state.last, &state.last_len, committed, committed_len);
    } else if (text_equal(action, action_len, "backspace") &&
               state.undo_word_len > 0 &&
               now >= state.undo_ms &&
               now - state.undo_ms <= g_undo_window_ms) {
        char key[kMaxKey];
        uint32_t key_len = make_key("word/", nullptr, 0, state.undo_word,
                                    state.undo_word_len, key);
        decrement_counter(key, key_len);
        if (state.undo_raw_len > 0) {
            key_len = make_key("pair/", state.undo_raw, state.undo_raw_len,
                               state.undo_word, state.undo_word_len, key);
            decrement_counter(key, key_len);
        }
        if (state.undo_prev_len > 0) {
            key_len = make_key("next/", state.undo_prev, state.undo_prev_len,
                               state.undo_word, state.undo_word_len, key);
            decrement_counter(key, key_len);
        }
        append_log(&state, "undo", state.undo_raw, state.undo_raw_len,
                   state.undo_word, state.undo_word_len);
        if (state.seq < kMaxLog) ++state.seq;
        recent_remove(&state, state.undo_word, state.undo_word_len);
        copy_text(state.chain, &state.chain_len, state.undo_prev,
                  state.undo_prev_len);
        copy_text(state.chain_raw, &state.chain_raw_len, "", 0);
        copy_text(state.last, &state.last_len, state.undo_prev,
                  state.undo_prev_len);
        state.undo_word_len = 0;
        state.undo_raw_len = 0;
        state.undo_prev_len = 0;
        state.undo_ms = 0;
    }

    Item candidates[kMaxItems];
    uint32_t candidate_count = 0;
    uint32_t existing_count = 0;
    Item existing[kMaxItems];
    read_existing(frame, existing, &existing_count);

    uint32_t learned_limit = g_max_candidates;
    if (g_enabled && raw_len > 0) {
        Item learned[kMaxItems];
        uint32_t learned_count = 0;
        learned_for_raw(raw, raw_len, learned, &learned_count);
        for (uint32_t i = 0; i < learned_count && candidate_count < learned_limit;
             ++i)
            candidates[candidate_count++] = learned[i];
    }
    for (uint32_t i = 0; i < existing_count && candidate_count < learned_limit; ++i) {
        if (has_text(candidates, candidate_count, existing[i].text,
                     existing[i].text_len))
            continue;
        candidates[candidate_count++] = existing[i];
    }

    Item next[kMaxItems];
    uint32_t next_count = 0;
    if (g_enabled) bigram_next(state, next, &next_count);
    if (next_count > g_max_candidates) next_count = g_max_candidates;

    return respond(out, cap, out_len, state, candidates, candidate_count, next,
                   next_count);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    State state;
    return respond(out, cap, out_len, state, nullptr, 0, nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) {
    g_enabled = true;
    g_decay_days = 30;
    g_undo_window_ms = 10000;
    g_max_candidates = 8;
    g_max_recent = 8;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
