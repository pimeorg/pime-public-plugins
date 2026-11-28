/*
 * pime-pinyin-parse: full-pinyin segmentation with typo correction.
 *
 * State: { buf: text } — the letters typed so far, max 96 bytes.
 * Workspace out:
 *   raw        corrected letter stream consumed by translate
 *   input      original letter stream (what the user typed)
 *   syllables  display segmentation of `raw`
 *   marks      correction marks [{type, start, end}] (offsets into `input`)
 *
 * Correction follows the WeaveText rule: a correction is kept only when the
 * plain reading is not fully syllable-segmentable and the corrected reading
 * is. Adjacent swaps, one missing letter and one extra letter are tried;
 * tap-neighbour letters (event.neighbour) are tried first. Abbreviations such
 * as "hbg" have no fully segmentable correction and are left untouched.
 *
 * Workspace in: keymap action, shuangpin syllable/consumed.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>
#include <pimeplug/pinyin.hpp>
#include <pimeplug/text.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxBuffer = 96;
constexpr uint32_t kMaxSegments = 32;
constexpr uint32_t kMaxMarks = 8;
constexpr uint32_t kCorrectLimit = 24;

alignas(8) uint8_t g_scratch[kScratchBytes];

bool g_auto_correct = true;
bool g_tap_correct = true;

struct Segment {
    uint32_t start;
    uint32_t end;
    uint8_t type; /* 0 normal, 1 corrected, 2 incomplete/abbreviation */
};

struct Correction {
    char text[kMaxBuffer];
    uint32_t len = 0;
    uint8_t type = 0; /* 1 swap, 2 delete, 3 insert, 4 neighbour */
    uint32_t start = 0;
    uint32_t end = 0;
    bool valid = false;
};

uint32_t segment(const char* buffer, uint32_t len, Segment* out,
                 uint32_t max_segments, bool* complete) {
    uint32_t count = 0;
    uint32_t i = 0;
    bool all = true;
    while (i < len && count < max_segments) {
        if (buffer[i] == '\'') {
            ++i;
            continue;
        }
        uint32_t best = 0;
        uint32_t limit = len - i;
        if (limit > 6) limit = 6;
        for (uint32_t n = limit; n >= 1; --n) {
            if (is_syllable(buffer + i, n)) {
                best = n;
                break;
            }
        }
        uint8_t type = 0;
        if (best == 0) {
            const uint32_t rest = len - i;
            best = (rest <= 6 && is_syllable_prefix(buffer + i, rest)) ? rest : 1;
            type = 2;
            all = false;
        }
        out[count].start = i;
        out[count].end = i + best;
        out[count].type = type;
        ++count;
        i += best;
    }
    if (complete) *complete = all && i >= len;
    return count;
}

bool fully_segmented(const char* buffer, uint32_t len) {
    Segment parts[kMaxSegments];
    bool complete = false;
    segment(buffer, len, parts, kMaxSegments, &complete);
    return complete;
}

uint32_t count_letters(const char* buffer, uint32_t len) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; ++i)
        if (ascii_alpha(buffer[i])) ++n;
    return n;
}

/* Acceptable = every segment but the last is a complete syllable and the last
 * is a prefix (or the string is fully segmented). "xianr" is acceptable
 * (xian + trailing initial); "zhogn" is not. */
bool acceptable(const char* buffer, uint32_t len) {
    bool reachable[kMaxBuffer + 1] = {};
    reachable[0] = true;
    for (uint32_t i = 0; i <= len; ++i) {
        if (!reachable[i]) continue;
        uint32_t limit = len - i;
        if (limit > 6) limit = 6;
        for (uint32_t n = 1; n <= limit; ++n) {
            if (is_syllable(buffer + i, n)) reachable[i + n] = true;
        }
    }
    if (reachable[len]) return true;
    for (uint32_t i = len; i > 0; --i) {
        if (!reachable[i - 1]) continue;
        const uint32_t rest = len - (i - 1);
        if (rest <= 6 && is_syllable_prefix(buffer + (i - 1), rest)) return true;
    }
    return false;
}

uint32_t common_prefix(const char* a, const char* b, uint32_t len) {
    uint32_t n = 0;
    while (n < len && a[n] == b[n]) ++n;
    return n;
}

/* Preference among equally long common prefixes: a neighbour correction is
 * the most trustworthy, then a swap, then a missing letter, then an extra
 * one (deleting a letter can too easily "fix" a different valid word). */
uint8_t rank_of(uint8_t type) {
    switch (type) {
        case 4: return 0;
        case 1: return 1;
        case 3: return 2;
        case 2: return 3;
        default: return 4;
    }
}

void consider(const char* buffer, uint32_t len, const Correction& candidate,
              Correction* best, uint32_t* best_prefix) {
    const uint32_t prefix = common_prefix(candidate.text, buffer, len);
    bool take = false;
    if (!best->valid) {
        take = true;
    } else if (prefix > *best_prefix) {
        take = true;
    } else if (prefix == *best_prefix &&
               rank_of(candidate.type) < rank_of(best->type)) {
        take = true;
    }
    if (!take) return;
    *best = candidate;
    *best_prefix = prefix;
}

bool make_edit(const char* buffer, uint32_t len, uint32_t position,
               uint8_t kind, char insert, Correction* out,
               bool allow_abbrev = false) {
    Buffer b(out->text, sizeof(out->text));
    for (uint32_t i = 0; i < len; ++i) {
        if (kind == 2 && i == position) continue;
        if (kind == 1 && i == position) {
            b.append(buffer[position + 1]);
            b.append(buffer[position]);
            ++i;
            continue;
        }
        if (kind == 3 && i == position) b.append(insert);
        b.append(buffer[i]);
    }
    if (kind == 3 && position == len) b.append(insert);
    if (b.len == 0 || b.len > kMaxBuffer) return false;
    /* Only a complete reading beats the typed text; a merely "acceptable"
     * reading (trailing initial) can be produced by almost any swap. */
    if (!fully_segmented(b.data, b.len) &&
        !(allow_abbrev && acceptable(b.data, b.len)))
        return false;
    out->len = b.len;
    out->type = kind;
    out->start = position;
    out->end = kind == 3 ? position : position + (kind == 2 ? 1 : 2);
    out->valid = true;
    return true;
}

bool correct(const char* buffer, uint32_t len, const char* neighbour,
             uint32_t neighbour_len, Correction* out) {
    if (len < 3 || len > kCorrectLimit) return false;
    if (count_letters(buffer, len) < 3) return false;
    if (fully_segmented(buffer, len)) return false;

    Correction best;
    uint32_t best_prefix = 0;

    /* Tap-neighbour: the finger landed next to the intended key. Works even
     * when the plain reading is a valid abbreviation lattice, as long as the
     * neighbour turns the tail into a complete spelling. */
    if (g_tap_correct && neighbour && neighbour_len == 1) {
        const char alt = lower_ascii(neighbour[0]);
        for (uint32_t i = 0; i < len; ++i) {
            if (!ascii_alpha(buffer[i]) || buffer[i] == alt) continue;
            if (i > 0 && buffer[i - 1] == '\'') continue;
            Correction candidate;
            Buffer b(candidate.text, sizeof(candidate.text));
            for (uint32_t k = 0; k < len; ++k)
                b.append(k == i ? alt : buffer[k]);
            if (b.len == 0 || !fully_segmented(b.data, b.len)) continue;
            candidate.len = b.len;
            candidate.type = 4;
            candidate.start = i;
            candidate.end = i + 1;
            candidate.valid = true;
            consider(buffer, len, candidate, &best, &best_prefix);
        }
    }

    /* Vowel-less strings are pure abbreviations ("zsh", "hbg", "dhrs"), not
     * typos. A trailing initial can also make an already-readable string
     * "acceptable" without being complete; both stay untouched. */
    bool has_vowel = false;
    for (uint32_t i = 0; i < len; ++i) {
        const char c = lower_ascii(buffer[i]);
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'v')
            has_vowel = true;
    }
    if (!has_vowel || acceptable(buffer, len)) {
        if (!best.valid) return false;
        *out = best;
        return true;
    }

    /* A doubled letter is the most common extra-key typo; delete one copy
     * before considering an insertion elsewhere. */
    for (uint32_t i = 0; i + 1 < len; ++i) {
        if (!ascii_alpha(buffer[i]) || buffer[i] != buffer[i + 1]) continue;
        Correction candidate;
        /* Deleting one copy is the strongest typo signal, so an abbreviation
         * lattice (xxiex -> xiex = 谢谢) is acceptable here as well. */
        if (make_edit(buffer, len, i, 2, 0, &candidate, true)) {
            *out = candidate;
            return true;
        }
    }

    /* Adjacent swap. */
    for (uint32_t i = 0; i + 1 < len; ++i) {
        if (!ascii_alpha(buffer[i]) || !ascii_alpha(buffer[i + 1])) continue;
        if (buffer[i] == buffer[i + 1]) continue;
        Correction candidate;
        if (make_edit(buffer, len, i, 1, 0, &candidate))
            consider(buffer, len, candidate, &best, &best_prefix);
    }
    /* One extra letter. */
    for (uint32_t i = 0; i < len; ++i) {
        if (!ascii_alpha(buffer[i])) continue;
        Correction candidate;
        if (make_edit(buffer, len, i, 2, 0, &candidate))
            consider(buffer, len, candidate, &best, &best_prefix);
    }
    /* One missing letter. */
    const char kVowels[] = {'a', 'e', 'i', 'o', 'u', 'n', 'g'};
    for (uint32_t i = 0; i <= len; ++i) {
        for (uint32_t k = 0; k < sizeof(kVowels); ++k) {
            Correction candidate;
            if (!make_edit(buffer, len, i, 3, kVowels[k], &candidate)) continue;
            consider(buffer, len, candidate, &best, &best_prefix);
        }
    }

    if (!best.valid) return false;
    *out = best;
    return true;
}

uint32_t load_buffer(const Frame& frame, char* buffer) {
    MapView state = MapView::of(frame.root.get("state"));
    const char* text = nullptr;
    uint32_t len = 0;
    if (!state.valid || !state.text("buf", &text, &len)) return 0;
    if (len > kMaxBuffer) len = kMaxBuffer;
    for (uint32_t i = 0; i < len; ++i) buffer[i] = text[i];
    return len;
}

bool append(char* buffer, uint32_t* len, const char* text, uint32_t text_len) {
    bool changed = false;
    for (uint32_t i = 0; i < text_len && *len < kMaxBuffer; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const bool letter = (c >= 'a' && c <= 'z') || c == '\'';
        /* v-mode: after a leading `v` the composition is a number or an
         * arithmetic expression, so digits and operators belong to it. The
         * special-candidate translator consumes the buffer verbatim. */
        const bool v_body = *len > 0 && buffer[0] == 'v' &&
                            ((c >= '0' && c <= '9') ||
                             (c == '+' || c == '-' || c == '*' || c == '/' ||
                              c == '(' || c == ')' || c == '%' || c == '^' ||
                              c == '.' || c == '='));
        if (letter || v_body) {
            buffer[(*len)++] = c;
            changed = true;
        }
    }
    return changed;
}

bool text_is(const char* text, uint32_t len, const char* expected) {
    return text && len == cstr_len(expected) &&
           Frame::equal(text, len, expected, len);
}

void mark_type_name(uint8_t type, const char** name) {
    switch (type) {
        case 1: *name = "swap"; break;
        case 2: *name = "delete"; break;
        case 3: *name = "insert"; break;
        case 4: *name = "replace"; break;
        default: *name = "none"; break;
    }
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const char* buffer, uint32_t len, const char* original,
                    uint32_t original_len, const Correction* correction,
                    bool handled, bool commit_phase) {
    Segment segments[kMaxSegments];
    bool complete = false;
    uint32_t segment_count =
        segment(buffer, len, segments, kMaxSegments, &complete);
    /* v-mode (v123, v1+2) is not pinyin: show one plain segment instead of
     * pretending every digit is an incomplete syllable. */
    bool plain = true;
    for (uint32_t i = 0; i < len; ++i) {
        if (!ascii_alpha(buffer[i]) && buffer[i] != '\'') {
            plain = false;
            break;
        }
    }
    if (!plain) {
        segment_count = len > 0 ? 1 : 0;
        segments[0].start = 0;
        segments[0].end = len;
        segments[0].type = 0;
    }

    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(1);
    w.key("buf");
    /* State keeps exactly what was typed, not the corrected spelling, so the
     * next key is appended to the original and backspace stays predictable.
     * On a commit the composition is gone; workspace still carries the raw
     * text so translate and format can resolve the selection. */
    w.text(original, commit_phase ? 0 : original_len);

    w.key("workspace");
    w.map(7);
    w.key("raw");
    w.text(buffer, len);
    w.key("input");
    w.text(original, original_len);
    /* The corrected spelling is offered to translate, never substituted into
     * the preedit: deleting letters under the user's eyes is surprising, and
     * the literal reading (xxie -> 谢谢) is often the intended one. */
    w.key("corrected");
    if (correction && correction->valid)
        w.text(correction->text, correction->len);
    else
        w.text("", 0);
    w.key("syllables");
    w.array(len > 0 ? segment_count : 0);
    for (uint32_t i = 0; i < segment_count && len > 0; ++i)
        w.text(buffer + segments[i].start, segments[i].end - segments[i].start);
    w.key("marks");
    if (correction && correction->valid) {
        w.array(1);
        w.map(3);
        w.key("type");
        const char* name = nullptr;
        mark_type_name(correction->type, &name);
        w.text(name);
        w.key("start");
        w.u64(correction->start);
        w.key("end");
        w.u64(correction->end);
    } else {
        w.array(0);
    }
    /* True only when the raw letters split into complete syllables. Mixed
     * input uses this to tell "goo"/"hel" (English-looking) from "ni"/"nihao"
     * (real pinyin); v-mode digits and abbreviations are not pinyin either. */
    w.key("complete");
    w.boolean(plain && complete);

    /* The classic abbreviation: one complete syllable plus the next
     * syllable's initial (`benj`, `key`, `zhongg`). Longer inputs have
     * already spelled another complete syllable (`linux` = li+nu+x), so
     * they are not abbreviations. Mixed input keeps Chinese ahead of English
     * for this shape. */
    const bool abbreviation = plain && !complete && segment_count == 2 &&
                              segments[0].type == 0;
    w.key("abbreviation");
    w.boolean(abbreviation);

    w.key("handled");
    w.boolean(handled);

    w.key("commands");
    if (commit_phase) {
        w.array(0);
    } else if (len > 0) {
        w.array(1);
        w.map(4);
        w.key("type");
        w.text("set_composition");
        w.key("text");
        w.text(buffer, len);
        w.key("cursor");
        w.u64(len);
        w.key("segments");
        w.array(segment_count);
        for (uint32_t i = 0; i < segment_count; ++i) {
            w.map(3);
            w.key("start");
            w.u64(segments[i].start);
            w.key("end");
            w.u64(segments[i].end);
            w.key("type");
            w.u64(segments[i].type);
        }
    } else if (handled) {
        /* The composition is gone: finish it in the editor and drop the bar. */
        w.array(2);
        cmd_set_composition(w, "", 0, 0);
        cmd_hide_candidates(w);
    } else {
        w.array(0);
    }

    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_auto_correct = true;
    g_tap_correct = true;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("auto_correct", &value)) g_auto_correct = value;
    if (settings.boolean("tap_correct", &value)) g_tap_correct = value;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    char buffer[kMaxBuffer];
    uint32_t len = load_buffer(frame, buffer);
    bool handled = false;

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_key = has_type && text_is(type, type_len, "key");
    const bool is_text = has_type && text_is(type, type_len, "text");
    const bool is_candidate =
        has_type && text_is(type, type_len, "candidate");

    /* `reset` cancels the composition: drop the buffer and tell the host to
     * finish the preedit and close the candidate bar. */
    if (has_type && text_is(type, type_len, "reset"))
        return respond(out, cap, out_len, buffer, 0, buffer, 0, nullptr, true,
                       false);

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"),
              &action, &action_len);

    bool shuangpin_consumed = false;
    span_bool(frame.workspace_value("plugin/pime-shuangpin-standard/consumed"),
              &shuangpin_consumed);

    bool commit_phase = false;
    if (is_candidate) {
        commit_phase = true;
    } else if (is_key || is_text) {
        if (text_is(action, action_len, "backspace")) {
            if (len > 0) {
                --len;
                handled = true;
            }
        } else if (text_is(action, action_len, "escape")) {
            len = 0;
            handled = true;
        } else if (text_is(action, action_len, "space") ||
                   text_is(action, action_len, "enter")) {
            commit_phase = true;
        } else if (shuangpin_consumed) {
            handled = true;
            const char* syllable = nullptr;
            uint32_t syllable_len = 0;
            if (span_text(
                    frame.workspace_value("plugin/pime-shuangpin-standard/syllable"),
                    &syllable, &syllable_len))
                append(buffer, &len, syllable, syllable_len);
        } else {
            const char* text = nullptr;
            uint32_t text_len = 0;
            if (frame.event_text(&text, &text_len))
                handled = append(buffer, &len, text, text_len);
        }
    }

    /* Correction is only advertised through `corrected`/`marks`; the preedit
     * and `raw` keep exactly what was typed (state already does), so backspace
     * and the visible composition stay predictable. */
    Correction correction;
    const char* display = buffer;
    uint32_t display_len = len;
    const char* neighbour = nullptr;
    uint32_t neighbour_len = 0;
    frame.event.text("neighbour", &neighbour, &neighbour_len);

    /* When the core starts a composition it may pass the whole typed text
     * without state. Once state exists it is authoritative; otherwise a
     * multi-letter `text` event is the composition itself. */
    MapView state_view = MapView::of(frame.root.get("state"));
    const bool has_state_buf = state_view.valid && state_view.get("buf").valid();
    if (!has_state_buf && (is_text || is_key) && !commit_phase) {
        const char* incoming = nullptr;
        uint32_t incoming_len = 0;
        if (frame.event_text(&incoming, &incoming_len) && incoming_len > 0 &&
            incoming_len <= kMaxBuffer) {
            /* A whole composition may also be a v-mode number/expression
             * ("v123", "v1+2"), whose body is not made of letters. */
            bool pure = true;
            const bool v_body = lower_ascii(incoming[0]) == 'v';
            for (uint32_t i = 0; i < incoming_len; ++i) {
                const char c = lower_ascii(incoming[i]);
                const bool ok =
                    (ascii_alpha(c) || c == '\'') ||
                    (v_body && i > 0 &&
                     ((c >= '0' && c <= '9') || c == '+' || c == '-' ||
                      c == '*' || c == '/' || c == '%' || c == '^' ||
                      c == '(' || c == ')' || c == '.' || c == '='));
                if (!ok) {
                    pure = false;
                    break;
                }
            }
            if (pure && (action_len == 0 || shuangpin_consumed)) {
                Buffer b(buffer, kMaxBuffer);
                for (uint32_t i = 0; i < incoming_len; ++i)
                    b.append(lower_ascii(incoming[i]));
                len = b.len;
            }
        }
    }

    /* u-mode ("umumu") and the glyph filter ("`mu") are component codes, not
     * pinyin; spelling correction would "fix" the leading u into ou. */
    const bool shape_mode = len >= 2 && (buffer[0] == 'u' || buffer[0] == '`');
    if (g_auto_correct && len > 0 && !shape_mode)
        correct(buffer, len, neighbour, neighbour_len, &correction);

    (void)display;
    (void)display_len;
    return respond(out, cap, out_len, buffer, len, buffer, len,
                   correction.valid ? &correction : nullptr, handled,
                   commit_phase);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t* in, uint32_t in_len,
                                      uint8_t* out, uint32_t cap,
                                      uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;
    char buffer[kMaxBuffer];
    const uint32_t len = load_buffer(frame, buffer);
    return respond(out, cap, out_len, buffer, len, buffer, len, nullptr, false,
                   false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_auto_correct = true;
    g_tap_correct = true;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
