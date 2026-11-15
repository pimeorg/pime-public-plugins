/*
 * pime-pinyin-rank: personalises translated candidates.
 *
 * Re-scores the translate candidate array with:
 *   - short-word boost (1-2 characters),
 *   - long-word cohesion (single dictionary word of 3+ characters),
 *   - learned choice for the current reading ("pick/<raw>" in the KV store),
 *   - recency of the user's last use of the exact text ("last/<text>"),
 *   - a small per-domain term list selected by settings.profile.
 *
 * Learning happens on `candidate` events: the selected text is stored for the
 * composition and stamped with the wall clock. A backspace shortly after a
 * selection undoes that entry, which is the WeaveText "backspace undo" rule.
 *
 * State: { last_raw, last_text, last_ms } - the last learned selection, kept
 * only so the next backspace can revert it.
 * Workspace in: translate candidates, parse raw. Out: re-ranked candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/utf8.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kMaxRaw = 64;
constexpr uint32_t kMaxKey = 128;
constexpr uint64_t kUndoWindowMs = 5000;
constexpr uint64_t kRecentWindowMs = 7ull * 24 * 3600 * 1000;

alignas(8) uint8_t g_scratch[kScratchBytes];

uint32_t g_short_boost = 100;
uint32_t g_cohesion_boost = 1500;
uint32_t g_learn_boost = 20000;
uint32_t g_recent_boost = 4000;
uint32_t g_profile_boost = 3000;
uint32_t g_max_candidates = 16;
uint32_t g_profile = 0; /* 0 general, 1 chat, 2 code, 3 formal */
uint32_t g_page_size = 5;
uint32_t g_user_boost = 8000;
uint32_t g_hotword_boost = 12000;
uint32_t g_decay_days = 30;

/* Owned text for user words injected from the KV store (candidate.text is
 * otherwise borrowed from the frame). */
char g_user_texts[kMaxCandidates][kMaxKey];

const char* const kChatTerms[] = {
    "哈哈", "嗯嗯", "好的", "谢谢", "在吗", "拜拜",
    "晚安", "早安", "收到", "没事", "可以", nullptr,
};
const char* const kCodeTerms[] = {
    "函数", "变量", "接口", "编译", "提交", "分支",
    "合并", "回滚", "部署", "日志", "参数", "返回值",
    "数组", "对象", nullptr,
};
const char* const kFormalTerms[] = {
    "因此", "但是", "此外", "综上", "建议", "感谢",
    "尊敬", "通知", "会议", "报告", "请问", "贵司", nullptr,
};

struct Candidate {
    const char* text;
    uint32_t len;
    uint32_t score;
    const char* source;
    uint32_t source_len;
    uint32_t words;
    const char* comment;
};

struct LastPick {
    char raw[kMaxRaw];
    uint32_t raw_len;
    char text[kMaxKey];
    uint32_t text_len;
    uint64_t ms;
    bool valid;
    /* Selection mapping: the page shown when the user pressed space/digit. */
    uint32_t page;
    /* Continuous word creation: consecutive single-character commits. */
    char chain[kMaxKey];
    uint32_t chain_len;
    char chain_raw[kMaxRaw];
    uint32_t chain_raw_len;
};

bool text_is(const char* text, uint32_t len, const char* expected) {
    return text && len == cstr_len(expected) &&
           Frame::equal(text, len, expected, len);
}

uint32_t read_candidates(const Frame& frame, Candidate* out) {
    WorkspaceKey key = full_key("pime-pinyin-translate", "candidates");
    ArrayView array = ArrayView::of(frame.workspace_value(key.text));
    if (!array.valid) return 0;
    uint32_t count = 0;
    for (uint64_t i = 0; i < array.count && count < kMaxCandidates; ++i) {
        MapView item = MapView::of(array.at(i));
        if (!item.valid) continue;
        Candidate candidate{};
        if (!item.text("text", &candidate.text, &candidate.len)) continue;
        uint64_t score = 0;
        item.uint("score", &score);
        item.text("source", &candidate.source, &candidate.source_len);
        uint64_t words = 0;
        item.uint("words", &words);
        candidate.words = static_cast<uint32_t>(words);
        candidate.score = static_cast<uint32_t>(score);
        candidate.comment = nullptr;
        out[count++] = candidate;
    }
    return count;
}

uint32_t read_raw(const Frame& frame, char* out, uint32_t cap) {
    WorkspaceKey raw = full_key("pime-pinyin-parse", "raw");
    const char* text = nullptr;
    uint32_t len = 0;
    if (!span_text(frame.workspace_value(raw.text), &text, &len)) {
        WorkspaceKey input = full_key("pime-pinyin-parse", "input");
        if (!span_text(frame.workspace_value(input.text), &text, &len))
            return 0;
    }
    if (len > cap) len = cap;
    for (uint32_t i = 0; i < len; ++i) out[i] = text[i];
    return len;
}

void load_state(const Frame& frame, LastPick* pick) {
    *pick = LastPick{};
    MapView state = MapView::of(frame.root.get("state"));
    if (!state.valid) return;
    uint64_t page = 0;
    if (state.uint("page", &page)) pick->page = static_cast<uint32_t>(page);
    {
        const char* chain = nullptr;
        uint32_t chain_len = 0;
        if (state.text("chain", &chain, &chain_len) && chain_len <= kMaxKey) {
            for (uint32_t i = 0; i < chain_len; ++i) pick->chain[i] = chain[i];
            pick->chain_len = chain_len;
        }
        const char* chain_raw = nullptr;
        uint32_t chain_raw_len = 0;
        if (state.text("chain_raw", &chain_raw, &chain_raw_len) &&
            chain_raw_len <= kMaxRaw) {
            for (uint32_t i = 0; i < chain_raw_len; ++i)
                pick->chain_raw[i] = chain_raw[i];
            pick->chain_raw_len = chain_raw_len;
        }
    }
    const char* raw = nullptr;
    uint32_t raw_len = 0;
    const char* text = nullptr;
    uint32_t text_len = 0;
    uint64_t ms = 0;
    if (!state.text("last_raw", &raw, &raw_len) ||
        !state.text("last_text", &text, &text_len) ||
        !state.uint("last_ms", &ms))
        return;
    if (raw_len > kMaxRaw || text_len > kMaxKey) return;
    for (uint32_t i = 0; i < raw_len; ++i) pick->raw[i] = raw[i];
    for (uint32_t i = 0; i < text_len; ++i) pick->text[i] = text[i];
    pick->raw_len = raw_len;
    pick->text_len = text_len;
    pick->ms = ms;
    pick->valid = true;
}

void make_key(const char* prefix, const char* text, uint32_t len, char* out,
              uint32_t* out_len) {
    uint32_t n = 0;
    while (prefix[n] && n < kMaxKey) {
        out[n] = prefix[n];
        ++n;
    }
    for (uint32_t i = 0; i < len && n < kMaxKey; ++i) out[n++] = text[i];
    *out_len = n;
}

bool term_in(const char* const* terms, const char* text, uint32_t len) {
    for (uint32_t i = 0; terms[i]; ++i) {
        if (text_is(text, len, terms[i])) return true;
    }
    return false;
}

const char* const* profile_terms() {
    switch (g_profile) {
        case 1: return kChatTerms;
        case 2: return kCodeTerms;
        case 3: return kFormalTerms;
        default: return nullptr;
    }
}

/* Learned choice: entries under "pick/<raw>" whose key starts with the current
 * raw. An exact key is the strongest signal; a longer stored reading only
 * means the user typed a prefix of something they chose before. */
void apply_learning(const char* raw, uint32_t raw_len, Candidate* list,
                    uint32_t count) {
    if (raw_len == 0) return;
    char prefix[kMaxKey];
    uint32_t prefix_len = 0;
    make_key("pick/", raw, raw_len, prefix, &prefix_len);
    store_scan(prefix, prefix_len, 8, [&](const char* key, uint32_t key_len,
                              const char* value, uint32_t value_len) {
        const uint32_t boost =
            key_len == prefix_len ? g_learn_boost : g_learn_boost / 2;
        for (uint32_t i = 0; i < count; ++i) {
            if (list[i].len != value_len) continue;
            if (!Frame::equal(list[i].text, list[i].len, value, value_len))
                continue;
            list[i].score += boost;
            if (!list[i].comment) list[i].comment = "常用";
        }
        return true;
    });
}

void apply_recency(Candidate* list, uint32_t count, uint64_t now) {
    if (now == 0) return;
    for (uint32_t i = 0; i < count; ++i) {
        char key[kMaxKey];
        uint32_t key_len = 0;
        make_key("last/", list[i].text, list[i].len, key, &key_len);
        char value[32];
        uint32_t value_len = 0;
        bool found = false;
        if (!store_get(key, key_len, value, sizeof(value), &value_len, &found) ||
            !found)
            continue;
        uint64_t stamp = 0;
        if (!parse_uint(value, value_len, &stamp) || stamp > now) continue;
        /* Coarse age buckets keep the ordering stable between the call that
         * displayed the list and the candidate event that selects from it. */
        const uint64_t age = (now - stamp) / 3600000ull * 3600000ull;
        if (age >= kRecentWindowMs) continue;
        const uint32_t boost = static_cast<uint32_t>(
            static_cast<uint64_t>(g_recent_boost) *
            (kRecentWindowMs - age) / kRecentWindowMs);
        list[i].score += boost;
        if (!list[i].comment) list[i].comment = "最近";
    }
}

void apply_profile(Candidate* list, uint32_t count) {
    const char* const* terms = profile_terms();
    if (!terms) return;
    for (uint32_t i = 0; i < count; ++i) {
        if (!term_in(terms, list[i].text, list[i].len)) continue;
        list[i].score += g_profile_boost;
        if (!list[i].comment) list[i].comment = "领域";
    }
}

void apply_base_boosts(Candidate* list, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t chars = utf8_count(list[i].text, list[i].len);
        if (chars <= 2)
            list[i].score += g_short_boost;
        else if (list[i].words == 1)
            list[i].score += g_cohesion_boost;
    }
}

void sort_candidates(Candidate* list, uint32_t count) {
    for (uint32_t i = 1; i < count; ++i) {
        const Candidate value = list[i];
        uint32_t j = i;
        while (j > 0 && list[j - 1].score < value.score) {
            list[j] = list[j - 1];
            --j;
        }
        list[j] = value;
    }
}

void learn_selection(Candidate* list, uint32_t count, uint64_t index,
                     const char* raw, uint32_t raw_len, uint64_t now,
                     LastPick* pick) {
    if (index >= count || list[index].len == 0 || list[index].len > kMaxKey)
        return;
    if (raw_len > 0) {
        char key[kMaxKey];
        uint32_t key_len = 0;
        make_key("pick/", raw, raw_len, key, &key_len);
        store_set(key, key_len, list[index].text, list[index].len);
    }
    char key[kMaxKey];
    uint32_t key_len = 0;
    make_key("last/", list[index].text, list[index].len, key, &key_len);
    char stamp[24];
    const uint32_t stamp_len = format_uint(now, stamp, sizeof(stamp));
    store_set(key, key_len, stamp, stamp_len);

    for (uint32_t i = 0; i < raw_len; ++i) pick->raw[i] = raw[i];
    for (uint32_t i = 0; i < list[index].len; ++i) pick->text[i] = list[index].text[i];
    pick->raw_len = raw_len;
    pick->text_len = list[index].len;
    pick->ms = now;
    pick->valid = true;
}

/* Backspace right after a selection removes the learned entry. */
void undo_selection(const LastPick& pick, uint64_t now, LastPick* cleared) {
    if (!pick.valid || now < pick.ms || now - pick.ms > kUndoWindowMs) return;
    char key[kMaxKey];
    uint32_t key_len = 0;
    make_key("pick/", pick.raw, pick.raw_len, key, &key_len);
    char value[kMaxKey];
    uint32_t value_len = 0;
    bool found = false;
    if (!store_get(key, key_len, value, sizeof(value), &value_len, &found) ||
            !found)
        return;
    if (value_len != pick.text_len ||
        !Frame::equal(value, value_len, pick.text, pick.text_len))
        return;
    store_del(key, key_len);
    make_key("last/", pick.text, pick.text_len, key, &key_len);
    store_del(key, key_len);
    *cleared = LastPick{};
}


/* ---------------------------------------------------------- user dictionary */

uint32_t today() {
    return static_cast<uint32_t>((host_time(1) / 86400000ull) & 0xffffffffu);
}

void parse_counter(const char* value, uint32_t len, uint32_t* count,
                   uint32_t* day) {
    uint64_t c = 0;
    uint32_t i = 0;
    while (i < len && ascii_digit(value[i])) {
        c = c * 10 + static_cast<uint64_t>(value[i] - '0');
        ++i;
    }
    uint64_t d = 0;
    if (i < len && value[i] == ':') {
        ++i;
        while (i < len && ascii_digit(value[i])) {
            d = d * 10 + static_cast<uint64_t>(value[i] - '0');
            ++i;
        }
    }
    *count = c > 1000000 ? 1000000 : static_cast<uint32_t>(c);
    *day = static_cast<uint32_t>(d);
}

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

uint32_t make_pair_key(const char* prefix, const char* raw, uint32_t raw_len,
                       const char* text, uint32_t text_len, char* out) {
    uint32_t n = 0;
    for (const char* p = prefix; *p && n < kMaxKey - 2; ++p) out[n++] = *p;
    for (uint32_t i = 0; i < raw_len && n < kMaxKey - 2; ++i) out[n++] = raw[i];
    if (text) {
        if (n < kMaxKey - 2) out[n++] = '\x1f';
        for (uint32_t i = 0; i < text_len && n < kMaxKey - 1; ++i)
            out[n++] = text[i];
    }
    out[n] = '\0';
    return n;
}

void decrement_counter(const char* key, uint32_t key_len) {
    char value[32];
    uint32_t value_len = 0;
    bool found = false;
    if (!store_get(key, key_len, value, sizeof(value), &value_len, &found) ||
        !found)
        return;
    uint32_t count = 0;
    uint32_t day = 0;
    parse_counter(value, value_len, &count, &day);
    count = decayed(count, day);
    if (count > 0) --count;
    char out[32];
    uint32_t n = format_uint(count, out, sizeof(out));
    out[n++] = ':';
    n += format_uint(today(), out + n, sizeof(out) - n);
    store_set(key, key_len, out, n);
}

void bump_counter(const char* key, uint32_t key_len, uint32_t add) {
    char value[32];
    uint32_t value_len = 0;
    bool found = false;
    uint32_t count = 0;
    uint32_t day = 0;
    if (store_get(key, key_len, value, sizeof(value), &value_len, &found) &&
        found)
        parse_counter(value, value_len, &count, &day);
    count = (found ? decayed(count, day) : 0) + add;
    if (count > 1000000) count = 1000000;
    char out[32];
    uint32_t n = format_uint(count, out, sizeof(out));
    out[n++] = ':';
    n += format_uint(today(), out + n, sizeof(out) - n);
    store_set(key, key_len, out, n);
}

bool single_han(const char* text, uint32_t len) {
    return utf8_count(text, len) == 1 &&
           static_cast<uint8_t>(text[0]) >= 0xe0;
}

bool ascii_letters(const char* text, uint32_t len) {
    if (len == 0) return false;
    for (uint32_t i = 0; i < len; ++i) {
        const char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

/* Records a commit: the (raw -> text) pair, plus the continuous word made of
 * consecutive single-character commits. */
void user_learn(const char* raw, uint32_t raw_len, const char* text,
                uint32_t text_len, LastPick* state) {
    if (raw_len == 0 || text_len == 0 || text_len >= kMaxKey ||
        raw_len >= kMaxRaw)
        return;
    char key[kMaxKey];
    uint32_t key_len = make_pair_key("usr/", raw, raw_len, text, text_len, key);
    bump_counter(key, key_len, 1);

    if (single_han(text, text_len) && ascii_letters(raw, raw_len) &&
        state->chain_len + text_len < kMaxKey &&
        state->chain_raw_len + raw_len < kMaxRaw) {
        for (uint32_t i = 0; i < text_len; ++i)
            state->chain[state->chain_len + i] = text[i];
        state->chain_len += text_len;
        for (uint32_t i = 0; i < raw_len; ++i)
            state->chain_raw[state->chain_raw_len + i] = raw[i];
        state->chain_raw_len += raw_len;
        if (utf8_count(state->chain, state->chain_len) >= 2) {
            key_len = make_pair_key("usr/", state->chain_raw,
                                    state->chain_raw_len, state->chain,
                                    state->chain_len, key);
            bump_counter(key, key_len, 1);
        }
    } else {
        state->chain_len = 0;
        state->chain_raw_len = 0;
    }
}

/* Injects learned words for this reading and boosts ones already present. */
uint32_t apply_user_words(const char* raw, uint32_t raw_len, Candidate* list,
                          uint32_t* count, uint32_t max) {
    if (raw_len == 0) return 0;
    char prefix[kMaxKey];
    const uint32_t prefix_len =
        make_pair_key("usr/", raw, raw_len, nullptr, 0, prefix);
    uint32_t injected = 0;
    uint32_t hits = 0;
    store_scan(prefix, prefix_len, kMaxCandidates,
               [&](const char* key, uint32_t key_len, const char* value,
                   uint32_t value_len) {
                   if (key_len <= prefix_len + 1) return true;
                   const char* word = key + prefix_len + 1;
                   const uint32_t word_len = key_len - prefix_len - 1;
                   if (word_len >= kMaxKey) return true;
                   uint32_t c = 0;
                   uint32_t d = 0;
                   parse_counter(value, value_len, &c, &d);
                   c = decayed(c, d);
                   if (c == 0) return true;
                   const uint32_t boost = g_user_boost + c * 500;
                   for (uint32_t i = 0; i < *count; ++i) {
                       if (list[i].len != word_len ||
                           !Frame::equal(list[i].text, list[i].len, word,
                                         word_len))
                           continue;
                       list[i].score += boost;
                       if (!list[i].comment) list[i].comment = "自造";
                       ++hits;
                       return true;
                   }
                   if (*count >= max || injected >= kMaxCandidates) return false;
                   char* slot = g_user_texts[injected];
                   for (uint32_t i = 0; i < word_len; ++i) slot[i] = word[i];
                   Candidate& candidate = list[(*count)++];
                   candidate.text = slot;
                   candidate.len = word_len;
                   candidate.score = boost;
                   candidate.source = "user";
                   candidate.source_len = 4;
                   candidate.words = 1;
                   candidate.comment = "自造";
                   ++injected;
                   ++hits;
                   return true;
               });
    return hits;
}

uint32_t apply_hotwords(Candidate* list, uint32_t count) {
    uint32_t hits = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (list[i].len == 0 || list[i].len >= kMaxKey) continue;
        char key[kMaxKey];
        uint32_t key_len = 0;
        const char prefix[] = "hot/";
        for (const char* p = prefix; *p; ++p) key[key_len++] = *p;
        for (uint32_t k = 0; k < list[i].len; ++k)
            key[key_len++] = list[i].text[k];
        char value[24];
        uint32_t value_len = 0;
        bool found = false;
        if (!store_get(key, key_len, value, sizeof(value), &value_len, &found) ||
            !found)
            continue;
        uint64_t weight = 0;
        if (!parse_uint(value, value_len, &weight)) continue;
        list[i].score +=
            g_hotword_boost +
            static_cast<uint32_t>(weight > 1000000 ? 1000000 : weight);
        if (!list[i].comment) list[i].comment = "热词";
        ++hits;
    }
    return hits;
}

/* Format prefers merged views (shape/shortcuts/hans-hant/special) over this
 * plugin's array; when one is present the displayed index no longer addresses
 * this list, so learning must stay silent. */
bool has_preferred_candidates(const Frame& frame) {
    static const char* const kIds[] = {
        "pime-shape-translate", "pime-shortcuts", "pime-hans-hant",
        "pime-special-candidates",
    };
    for (const char* id : kIds) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (array.valid && array.count > 0) return true;
    }
    return false;
}

bool is_arrow_action(const char* action, uint32_t len) {
    return text_is(action, len, "left") || text_is(action, len, "right");
}

/* Mirrors pime-candidates-format's paging rules so space/digit commits address
 * the same slot the user saw. */
/* Returns the page that addresses the *displayed* list for this call (the
 * value space/digit must use), then applies format's reset rule so the next
 * composition starts on page 0. */
uint32_t update_page(LastPick* state, const char* action, uint32_t action_len,
                     bool is_key, bool is_text, uint32_t total) {
    const uint32_t page_size = g_page_size == 0 ? 1 : g_page_size;
    const uint32_t last = total == 0 ? 0 : (total - 1) / page_size;
    if (state->page > last) state->page = last;
    bool paging = false;
    if (is_key) {
        if (text_is(action, action_len, "pagedown") ||
            text_is(action, action_len, "down")) {
            if (state->page < last) ++state->page;
            paging = true;
        } else if (text_is(action, action_len, "pageup") ||
                   text_is(action, action_len, "up")) {
            if (state->page > 0) --state->page;
            paging = true;
        } else if (text_is(action, action_len, "home")) {
            state->page = 0;
            paging = true;
        } else if (text_is(action, action_len, "end")) {
            state->page = last;
            paging = true;
        }
    }
    const uint32_t effective = state->page;
    if ((is_key && !paging && !is_arrow_action(action, action_len)) || is_text)
        state->page = 0;
    return effective;
}


/* --------------------------------------------------------------- hotwords */

/* Payload format: one "<word>\t<weight>" per line. Only the words are stored;
 * the cloud signature was already verified by pime-cloud-hotwords. */
uint32_t load_hotwords(const char* payload, uint32_t len) {
    uint32_t loaded = 0;
    uint32_t i = 0;
    while (i < len && loaded < 512) {
        const char* line = payload + i;
        uint32_t n = 0;
        while (i + n < len && payload[i + n] != '\n' && payload[i + n] != '\r')
            ++n;
        i += n;
        while (i < len && (payload[i] == '\n' || payload[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t tab = 0;
        while (tab < n && line[tab] != '\t') ++tab;
        if (tab == 0 || tab >= n || tab >= kMaxKey) continue;
        char key[kMaxKey];
        uint32_t key_len = 0;
        const char prefix[] = "hot/";
        for (const char* p = prefix; *p; ++p) key[key_len++] = *p;
        for (uint32_t k = 0; k < tab; ++k) key[key_len++] = line[k];
        const char* weight_text = line + tab + 1;
        const uint32_t weight_len = n - tab - 1;
        uint64_t weight = 0;
        if (!parse_uint(weight_text, weight_len, &weight)) continue;
        if (weight > 1000000) weight = 1000000;
        char value[24];
        const uint32_t value_len = format_uint(weight, value, sizeof(value));
        store_set(key, key_len, value, value_len);
        ++loaded;
    }
    return loaded;
}

void clear_hotwords() {
    char keys[128][kMaxKey];
    uint32_t lengths[128];
    uint32_t count = 0;
    store_scan("hot/", 128, [&](const char* key, uint32_t key_len, const char*,
                                uint32_t) {
        if (count >= 128 || key_len >= kMaxKey) return false;
        for (uint32_t i = 0; i < key_len; ++i) keys[count][i] = key[i];
        lengths[count] = key_len;
        ++count;
        return true;
    });
    for (uint32_t i = 0; i < count; ++i) store_del(keys[i], lengths[i]);
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Candidate* list, uint32_t count,
                    const LastPick* pick, uint32_t learned,
                    uint32_t hotwords) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    {
        uint32_t fields = 0;
        if (pick && pick->valid) fields += 3;
        if (pick && pick->page) ++fields;
        if (pick && pick->chain_len) ++fields;
        if (pick && pick->chain_raw_len) ++fields;
        w.map(fields);
        if (pick && pick->valid) {
            w.key("last_raw");
            w.text(pick->raw, pick->raw_len);
            w.key("last_text");
            w.text(pick->text, pick->text_len);
            w.key("last_ms");
            w.u64(pick->ms);
        }
        if (pick && pick->page) {
            w.key("page");
            w.u64(pick->page);
        }
        if (pick && pick->chain_len) {
            w.key("chain");
            w.text(pick->chain, pick->chain_len);
        }
        if (pick && pick->chain_raw_len) {
            w.key("chain_raw");
            w.text(pick->chain_raw, pick->chain_raw_len);
        }
    }
    w.key("workspace");
    w.map(3);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t fields = 2;
        if (list[i].source) ++fields;
        if (list[i].words) ++fields;
        if (list[i].comment) ++fields;
        w.map(fields);
        w.key("text");
        w.text(list[i].text, list[i].len);
        w.key("score");
        w.u64(list[i].score);
        if (list[i].source) {
            w.key("source");
            w.text(list[i].source, list[i].source_len);
        }
        if (list[i].words) {
            w.key("words");
            w.u64(list[i].words);
        }
        if (list[i].comment) {
            w.key("comment");
            w.text(list[i].comment);
        }
    }
    w.key("learned");
    w.u64(learned);
    w.key("hotwords");
    w.u64(hotwords);
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
    g_short_boost = 100;
    g_cohesion_boost = 1500;
    g_learn_boost = 20000;
    g_recent_boost = 4000;
    g_profile_boost = 3000;
    g_max_candidates = 16;
    g_profile = 0;
    g_page_size = 5;
    g_user_boost = 8000;
    g_hotword_boost = 12000;
    g_decay_days = 30;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t value = 0;
    if (settings.uint("short_boost", &value) && value <= 100000)
        g_short_boost = static_cast<uint32_t>(value);
    if (settings.uint("cohesion_boost", &value) && value <= 100000)
        g_cohesion_boost = static_cast<uint32_t>(value);
    if (settings.uint("learn_boost", &value) && value <= 1000000)
        g_learn_boost = static_cast<uint32_t>(value);
    if (settings.uint("recent_boost", &value) && value <= 1000000)
        g_recent_boost = static_cast<uint32_t>(value);
    if (settings.uint("profile_boost", &value) && value <= 1000000)
        g_profile_boost = static_cast<uint32_t>(value);
    if (settings.uint("max_candidates", &value) && value >= 1 && value <= 32)
        g_max_candidates = static_cast<uint32_t>(value);
    if (settings.uint("page_size", &value) && value >= 1 && value <= 9)
        g_page_size = static_cast<uint32_t>(value);
    if (settings.uint("user_boost", &value) && value <= 1000000)
        g_user_boost = static_cast<uint32_t>(value);
    if (settings.uint("hotword_boost", &value) && value <= 1000000)
        g_hotword_boost = static_cast<uint32_t>(value);
    if (settings.uint("decay_days", &value) && value <= 3650)
        g_decay_days = static_cast<uint32_t>(value);
    const char* profile = nullptr;
    uint32_t profile_len = 0;
    if (settings.text("profile", &profile, &profile_len)) {
        if (text_is(profile, profile_len, "chat"))
            g_profile = 1;
        else if (text_is(profile, profile_len, "code"))
            g_profile = 2;
        else if (text_is(profile, profile_len, "formal"))
            g_profile = 3;
        else
            g_profile = 0;
    }
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    LastPick pick;
    load_state(frame, &pick);

    char raw[kMaxRaw];
    const uint32_t raw_len = read_raw(frame, raw, sizeof(raw));
    const uint64_t now = host_time(1);

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_candidate =
        has_type && text_is(type, type_len, "candidate");

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"), &action,
              &action_len);

    Candidate candidates[kMaxCandidates];
    uint32_t count = read_candidates(frame, candidates);

    if (!is_candidate && text_is(action, action_len, "backspace")) {
        /* Backspace undo covers both the per-reading pick and the user
         * dictionary entry written by the same commit. */
        if (pick.valid && now >= pick.ms && now - pick.ms <= kUndoWindowMs &&
            pick.raw_len > 0 && pick.text_len > 0) {
            char key[kMaxKey];
            const uint32_t key_len =
                make_pair_key("usr/", pick.raw, pick.raw_len, pick.text,
                              pick.text_len, key);
            decrement_counter(key, key_len);
        }
        undo_selection(pick, now, &pick);
    }

    const bool is_text = has_type && text_is(type, type_len, "text");
    const bool is_key = has_type && text_is(type, type_len, "key");

    /* Hotword payloads arrive from the host/cloud service (pime-cloud-hotwords
     * verifies the signature; the host forwards the payload here). */
    uint32_t loaded_hotwords = 0;
    if (has_type && text_is(type, type_len, "hotword.update")) {
        const char* payload = nullptr;
        uint32_t payload_len = 0;
        if (frame.event.text("payload", &payload, &payload_len))
            loaded_hotwords = load_hotwords(payload, payload_len);
    } else if (has_type && text_is(type, type_len, "hotword.clear")) {
        clear_hotwords();
    }

    /* The displayed list may be a merged view from an earlier plugin; then the
     * index/space mapping below does not address this array. */
    const bool preferred = has_preferred_candidates(frame);
    const uint32_t effective_page =
        update_page(&pick, action, action_len, is_key, is_text, count);

    bool commit = false;
    uint64_t index = 0;
    if (!preferred) {
        if (is_key && text_is(action, action_len, "space")) {
            commit = count > 0;
            index = static_cast<uint64_t>(effective_page) * g_page_size;
        } else if (is_key && text_is(action, action_len, "enter")) {
            commit = count > 0;
            index = static_cast<uint64_t>(effective_page) * g_page_size;
        } else if (is_key && text_is(action, action_len, "digit")) {
            uint64_t digit = 0;
            if (span_uint(frame.workspace_value("plugin/pime-keymap-default/digit"),
                          &digit) &&
                digit >= 1 && digit <= 9) {
                index =
                    static_cast<uint64_t>(effective_page) * g_page_size + digit - 1;
                commit = true;
            }
        } else if (is_candidate && frame.event.uint("index", &index)) {
            commit = true;
        }
    }

    /* Rank first, learn second (the index addresses the list displayed by the
     * previous call, so the order must survive until learning runs). */
    apply_base_boosts(candidates, count);
    apply_learning(raw, raw_len, candidates, count);
    const uint32_t learned = apply_user_words(raw, raw_len, candidates, &count,
                                              g_max_candidates);
    const uint32_t hotword_hits = apply_hotwords(candidates, count);
    apply_recency(candidates, count, now);
    apply_profile(candidates, count);
    sort_candidates(candidates, count);
    if (count > g_max_candidates) count = g_max_candidates;

    if (commit && index < count) {
        learn_selection(candidates, count, index, raw, raw_len, now, &pick);
        user_learn(raw, raw_len, candidates[index].text, candidates[index].len,
                   &pick);
        pick.page = 0;
    }
    return respond(out, cap, out_len, candidates, count, &pick, learned,
                   hotword_hits);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0, nullptr, 0, 0);
}

PIME_EXPORT void pime_shutdown(void) {
    g_short_boost = 100;
    g_cohesion_boost = 1500;
    g_learn_boost = 20000;
    g_recent_boost = 4000;
    g_profile_boost = 3000;
    g_max_candidates = 16;
    g_profile = 0;
    g_page_size = 5;
    g_user_boost = 8000;
    g_hotword_boost = 12000;
    g_decay_days = 30;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
