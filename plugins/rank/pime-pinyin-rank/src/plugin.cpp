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

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Candidate* list, uint32_t count,
                    const LastPick* pick) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    if (pick && pick->valid) {
        w.map(3);
        w.key("last_raw");
        w.text(pick->raw, pick->raw_len);
        w.key("last_text");
        w.text(pick->text, pick->text_len);
        w.key("last_ms");
        w.u64(pick->ms);
    } else {
        w.map(0);
    }
    w.key("workspace");
    w.map(1);
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

    if (!is_candidate && text_is(action, action_len, "backspace"))
        undo_selection(pick, now, &pick);

    /* Rank first, learn second. The index on a candidate event addresses the
     * list that was displayed (this plugin's previous output), so the array
     * must keep that order in the same call; the new pick only affects the
     * next composition. */
    apply_base_boosts(candidates, count);
    apply_learning(raw, raw_len, candidates, count);
    apply_recency(candidates, count, now);
    apply_profile(candidates, count);
    sort_candidates(candidates, count);
    if (count > g_max_candidates) count = g_max_candidates;

    if (is_candidate) {
        uint64_t index = 0;
        if (frame.event.uint("index", &index))
            learn_selection(candidates, count, index, raw, raw_len, now, &pick);
    }
    return respond(out, cap, out_len, candidates, count, &pick);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0, nullptr);
}

PIME_EXPORT void pime_shutdown(void) {
    g_short_boost = 100;
    g_cohesion_boost = 1500;
    g_learn_boost = 20000;
    g_recent_boost = 4000;
    g_profile_boost = 3000;
    g_max_candidates = 16;
    g_profile = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
