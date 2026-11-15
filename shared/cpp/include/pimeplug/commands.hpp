/*
 * Output command helpers. The command names and shapes follow
 * pime-core/schema/cbor/command.cddl, not the abbreviated names in the prose
 * specification.
 */
#ifndef PIMEPLUG_COMMANDS_HPP
#define PIMEPLUG_COMMANDS_HPP

#include "cbor.hpp"

namespace pimeplug {

inline void cmd_commit(Writer& w, const char* text, uint32_t len) {
    w.map(2);
    w.key("type");
    w.text("commit");
    w.key("text");
    w.text(text, len);
}

inline void cmd_set_composition(Writer& w, const char* text, uint32_t len,
                                uint32_t cursor) {
    w.map(3);
    w.key("type");
    w.text("set_composition");
    w.key("text");
    w.text(text, len);
    w.key("cursor");
    w.u64(cursor);
}

inline void cmd_hide_candidates(Writer& w) {
    w.map(1);
    w.key("type");
    w.text("hide_candidates");
}

inline void cmd_notify(Writer& w, int32_t code) {
    w.map(2);
    w.key("type");
    w.text("notify");
    w.key("code");
    w.i64(code);
}

inline void cmd_beep(Writer& w) {
    w.map(1);
    w.key("type");
    w.text("beep");
}

inline void cmd_clear_ink(Writer& w) {
    w.map(1);
    w.key("type");
    w.text("clear_ink");
}

/* show_candidates: open the item array, then emit one entry per candidate. */
inline void cmd_show_candidates_begin(Writer& w, uint32_t count) {
    w.map(2);
    w.key("type");
    w.text("show_candidates");
    w.key("items");
    w.array(count);
}

/* Candidate entry with an independent comment (annotation) and label (the
 * selection key shown next to the item). Either may be absent. */
inline void cmd_candidate_item_ex(Writer& w, const char* text, uint32_t text_len,
                                  const char* comment, uint32_t comment_len,
                                  const char* label, uint32_t label_len) {
    uint32_t fields = 1;
    if (comment && comment_len) ++fields;
    if (label && label_len) ++fields;
    w.map(fields);
    w.key("text");
    w.text(text, text_len);
    if (comment && comment_len) {
        w.key("comment");
        w.text(comment, comment_len);
    }
    if (label && label_len) {
        w.key("label");
        w.text(label, label_len);
    }
}

inline void cmd_candidate_item(Writer& w, const char* text, uint32_t text_len,
                               const char* comment, uint32_t comment_len) {
    /* Historic shape: the comment doubles as the label. */
    cmd_candidate_item_ex(w, text, text_len, comment, comment_len, comment,
                          comment_len);
}

} /* namespace pimeplug */

#endif /* PIMEPLUG_COMMANDS_HPP */
