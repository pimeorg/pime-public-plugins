/*
 * Candidate arrays in the workspace.
 *
 * Shape written by translate/rank/predict plugins:
 *   [ { text: tstr, score: uint, ?source: tstr, ?comment: tstr, ?label: tstr } ]
 */
#ifndef PIMEPLUG_CANDIDATES_HPP
#define PIMEPLUG_CANDIDATES_HPP

#include "cbor.hpp"
#include "text.hpp"
#include "workspace.hpp"

namespace pimeplug {

constexpr uint32_t kCandidateText = 192;
constexpr uint32_t kCandidateComment = 96;
constexpr uint32_t kCandidateSource = 32;

struct Candidate {
    char text[kCandidateText];
    uint32_t text_len = 0;
    int32_t score = 0;
    char source[kCandidateSource];
    uint32_t source_len = 0;
    char comment[kCandidateComment];
    uint32_t comment_len = 0;
    char label[16];
    uint32_t label_len = 0;

    void set_text(const char* value, uint32_t len) {
        if (len > sizeof(text)) len = sizeof(text);
        for (uint32_t i = 0; i < len; ++i) text[i] = value[i];
        text_len = len;
    }
    void set_source(const char* value) {
        source_len = cstr_len(value);
        if (source_len > sizeof(source)) source_len = sizeof(source);
        for (uint32_t i = 0; i < source_len; ++i) source[i] = value[i];
    }
    void set_comment(const char* value, uint32_t len) {
        if (len > sizeof(comment)) len = sizeof(comment);
        for (uint32_t i = 0; i < len; ++i) comment[i] = value[i];
        comment_len = len;
    }
    void set_label(const char* value, uint32_t len) {
        if (len > sizeof(label)) len = sizeof(label);
        for (uint32_t i = 0; i < len; ++i) label[i] = value[i];
        label_len = len;
    }
};

/* Reads contract candidates written by `plugin_id`/`key`. */
inline uint32_t read_candidates(const Frame& frame, const char* plugin_id,
                                const char* key, Candidate* out, uint32_t max,
                                bool* ok = nullptr) {
    WorkspaceKey full = full_key(plugin_id, key);
    ArrayView array = ArrayView::of(frame.workspace_value(full.text));
    if (!array.valid) {
        if (ok) *ok = false;
        return 0;
    }
    uint32_t count = 0;
    for (uint64_t i = 0; i < array.count && count < max; ++i) {
        MapView item = MapView::of(array.at(i));
        if (!item.valid) continue;
        Candidate& c = out[count];
        const char* text = nullptr;
        uint32_t text_len = 0;
        if (!item.text("text", &text, &text_len)) continue;
        c.set_text(text, text_len);
        uint64_t score = 0;
        item.uint("score", &score);
        c.score = static_cast<int32_t>(score > 0x7fffffff ? 0x7fffffff : score);
        const char* source = nullptr;
        uint32_t source_len = 0;
        if (item.text("source", &source, &source_len) && source_len)
            c.set_source(source);
        const char* comment = nullptr;
        uint32_t comment_len = 0;
        if (item.text("comment", &comment, &comment_len) && comment_len)
            c.set_comment(comment, comment_len);
        const char* label = nullptr;
        uint32_t label_len = 0;
        if (item.text("label", &label, &label_len) && label_len)
            c.set_label(label, label_len);
        ++count;
    }
    if (ok) *ok = true;
    return count;
}

/* Writes the "candidates" workspace array (short key). */
inline void write_candidates(Writer& w, const Candidate* list, uint32_t count) {
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        const Candidate& c = list[i];
        uint32_t fields = 2;
        if (c.source_len) ++fields;
        if (c.comment_len) ++fields;
        if (c.label_len) ++fields;
        w.map(fields);
        w.key("text");
        w.text(c.text, c.text_len);
        w.key("score");
        w.u64(c.score < 0 ? 0 : static_cast<uint64_t>(c.score));
        if (c.source_len) {
            w.key("source");
            w.text(c.source, c.source_len);
        }
        if (c.comment_len) {
            w.key("comment");
            w.text(c.comment, c.comment_len);
        }
        if (c.label_len) {
            w.key("label");
            w.text(c.label, c.label_len);
        }
    }
}

inline void sort_candidates(Candidate* list, uint32_t count) {
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

}  // namespace pimeplug

#endif /* PIMEPLUG_CANDIDATES_HPP */
