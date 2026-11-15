/*
 * pime-candidates-format: candidate display, paging, selection and commit.
 *
 * Reads the first non-empty candidate array written earlier in the chain and
 * turns it into host commands. Paging state survives calls, so PageUp/PageDown
 * (and the arrow aliases) move through a long list while digits select inside
 * the visible page. Comments (annotation) and labels (selection key) are
 * emitted per item; a candidate may supply `comment` or `pinyin` for the
 * annotation. Writes "committed" so learn/user-dict plugins can observe the
 * selection in the same chain call.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxShown = 32;

alignas(8) uint8_t g_scratch[kScratchBytes];

uint32_t g_page_size = 5;
bool g_commit_on_enter = true;
bool g_commit_raw = true;
bool g_show_labels = true;
bool g_show_comments = true;

/* Special candidates come first: they are a merged view of the ranked list
 * (dates keep the ordinary first candidate), so preferring them never hides
 * anything the other arrays hold. */
const char* const kCandidateSources[] = {
    "pime-shape-translate",
    "pime-shortcuts",
    "pime-hans-hant",
    "pime-special-candidates",
    "pime-pinyin-rank",
    "pime-rank-default",
    "pime-pinyin-translate",
    "pime-wubi-translate",
    "pime-cangjie-translate",
    "pime-t9-translate",
    "pime-english-translate",
    "pime-emoji-suggest",
    "pime-clip-code",
    "pime-predict-context",
    "pime-handwriting-ncnn",
    "pime-speech-ncnn",
};

const char* const kRawSources[] = {
    "pime-pinyin-parse", "pime-wubi-parse", "pime-cangjie-parse",
    "pime-t9-parse",     "pime-english-parse",
};
const char* const kRawKeys[] = {"raw", "code", "code", "raw", "raw"};
constexpr uint32_t kRawSourceCount = 5;

bool same(const char* text, uint32_t len, const char* expected) {
    return text && len == cstr_len(expected) &&
           Frame::equal(text, len, expected, len);
}

ArrayView find_candidates(const Frame& frame) {
    for (const char* id : kCandidateSources) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (array.valid && array.count > 0) return array;
    }
    return ArrayView{};
}

bool raw_text(const Frame& frame, const char** text, uint32_t* len) {
    for (uint32_t i = 0; i < kRawSourceCount; ++i) {
        WorkspaceKey key = full_key(kRawSources[i], kRawKeys[i]);
        if (span_text(frame.workspace_value(key.text), text, len)) return true;
    }
    return false;
}

bool candidate_field(ArrayView candidates, uint64_t index, const char* key,
                     const char** text, uint32_t* len) {
    if (!candidates.valid || index >= candidates.count) return false;
    MapView item = MapView::of(candidates.at(index));
    return item.valid && item.text(key, text, len);
}

bool candidate_text(ArrayView candidates, uint64_t index, const char** text,
                    uint32_t* len) {
    return candidate_field(candidates, index, "text", text, len);
}

uint32_t read_page(const Frame& frame) {
    MapView state = MapView::of(frame.root.get("state"));
    uint64_t value = 0;
    if (state.valid && state.uint("page", &value))
        return static_cast<uint32_t>(value);
    return 0;
}

struct Reply {
    bool handled = false;
    uint8_t command = 0; /* 0 none, 1 show, 2 hide, 3 commit */
    ArrayView candidates{};
    uint32_t page = 0;
    const char* committed = nullptr;
    uint32_t committed_len = 0;
};

void write_items(Writer& w, const Reply& reply) {
    const uint32_t total = static_cast<uint32_t>(reply.candidates.count);
    uint32_t start = reply.page * g_page_size;
    if (start > total) start = total;
    uint32_t shown = total - start;
    if (shown > g_page_size) shown = g_page_size;
    if (shown > kMaxShown) shown = kMaxShown;

    cmd_show_candidates_begin(w, shown);
    for (uint32_t i = 0; i < shown; ++i) {
        const uint64_t index = start + i;
        const char* text = nullptr;
        uint32_t text_len = 0;
        if (!candidate_text(reply.candidates, index, &text, &text_len)) {
            text = "";
            text_len = 0;
        }
        const char* comment = nullptr;
        uint32_t comment_len = 0;
        if (g_show_comments &&
            !candidate_field(reply.candidates, index, "comment", &comment,
                             &comment_len) &&
            !candidate_field(reply.candidates, index, "pinyin", &comment,
                             &comment_len)) {
            comment = nullptr;
            comment_len = 0;
        }
        char label[2] = {0, 0};
        uint32_t label_len = 0;
        if (g_show_labels && i < 9) {
            label[0] = static_cast<char>('1' + i);
            label_len = 1;
        }
        cmd_candidate_item_ex(w, text, text_len, comment, comment_len, label,
                              label_len);
    }
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Reply& reply) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    if (reply.page > 0) {
        w.map(1);
        w.key("page");
        w.u64(reply.page);
    } else {
        w.map(0);
    }

    w.key("workspace");
    if (reply.committed) {
        w.map(1);
        w.key("committed");
        w.text(reply.committed, reply.committed_len);
    } else {
        w.map(0);
    }

    w.key("handled");
    w.boolean(reply.handled);

    w.key("commands");
    if (reply.command == 0) {
        w.array(0);
    } else {
        w.array(1);
        if (reply.command == 1) {
            write_items(w, reply);
        } else if (reply.command == 2) {
            cmd_hide_candidates(w);
        } else {
            cmd_commit(w, reply.committed, reply.committed_len);
        }
    }

    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_page_size = 5;
    g_commit_on_enter = true;
    g_commit_raw = true;
    g_show_labels = true;
    g_show_comments = true;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t size = 0;
    if (settings.uint("page_size", &size) && size >= 1 && size <= 9)
        g_page_size = static_cast<uint32_t>(size);
    bool value = true;
    if (settings.boolean("commit_on_enter", &value)) g_commit_on_enter = value;
    if (settings.boolean("commit_raw", &value)) g_commit_raw = value;
    if (settings.boolean("show_labels", &value)) g_show_labels = value;
    if (settings.boolean("show_comments", &value)) g_show_comments = value;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    const ArrayView candidates = find_candidates(frame);
    const uint32_t total = static_cast<uint32_t>(candidates.count);

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"), &action,
              &action_len);

    const char* type = nullptr;
    uint32_t type_len = 0;
    frame.event.text("type", &type, &type_len);
    const bool is_key = same(type, type_len, "key");
    const bool is_text = same(type, type_len, "text");
    const bool is_candidate = same(type, type_len, "candidate");

    /* `reset` cancels the composition: drop the paging state and close the
     * candidate bar. */
    if (same(type, type_len, "reset")) {
        Reply reply;
        reply.handled = true;
        reply.command = 2;
        return respond(out, cap, out_len, reply);
    }

    uint32_t page_size = g_page_size;
    uint64_t context_page = 0;
    if (frame.context.uint("page_size", &context_page) && context_page > 0)
        page_size = context_page > kMaxShown ? kMaxShown
                                             : static_cast<uint32_t>(context_page);

    uint32_t page = read_page(frame);
    const uint32_t last_page = total == 0 ? 0 : (total - 1) / page_size;
    if (page > last_page) page = last_page;

    Reply reply;
    reply.candidates = candidates;
    reply.page = page;

    if (is_key && same(action, action_len, "escape")) {
        reply.handled = true;
        reply.command = 2;
        reply.page = 0;
        return respond(out, cap, out_len, reply);
    }

    bool paging = false;
    if (is_key) {
        if (same(action, action_len, "pagedown") ||
            same(action, action_len, "down")) {
            if (page < last_page) ++page;
            paging = true;
        } else if (same(action, action_len, "pageup") ||
                   same(action, action_len, "up")) {
            if (page > 0) --page;
            paging = true;
        } else if (same(action, action_len, "home")) {
            page = 0;
            paging = true;
        } else if (same(action, action_len, "end")) {
            page = last_page;
            paging = true;
        }
    }
    reply.page = page;

    const bool is_arrow = same(action, action_len, "left") ||
                          same(action, action_len, "right");
    if ((is_key && !paging && !is_arrow) || is_text) reply.page = 0;

    const char* raw = nullptr;
    uint32_t raw_len = 0;
    const bool has_composition = raw_text(frame, &raw, &raw_len) && raw_len > 0;

    bool commit = false;
    bool raw_fallback = false;
    uint64_t index = 0;
    if (is_key && same(action, action_len, "space")) {
        commit = total > 0;
        raw_fallback = !commit;
    } else if (is_key && same(action, action_len, "enter")) {
        commit = g_commit_on_enter && total > 0;
        raw_fallback = g_commit_on_enter && !commit;
    } else if (is_key && same(action, action_len, "digit")) {
        /* v-mode keeps digits literal: "v1234" is a number/expression, not a
         * candidate selection. The digit reached the parser as text. */
        const bool v_mode =
            has_composition && raw_len > 0 &&
            (raw[0] == 'v' || raw[0] == 'V');
        uint64_t digit = 0;
        if (!v_mode &&
            span_uint(frame.workspace_value("plugin/pime-keymap-default/digit"),
                      &digit) &&
            digit >= 1 && digit <= 9) {
            const uint32_t slot =
                page * page_size + static_cast<uint32_t>(digit) - 1;
            if (slot < total) {
                index = slot;
                commit = true;
            }
        }
    } else if (is_candidate && frame.event.uint("index", &index)) {
        commit = index < total;
    }

    const char* text = nullptr;
    uint32_t text_len = 0;
    if (commit && candidate_text(candidates, index, &text, &text_len)) {
        reply.handled = true;
        reply.command = 3;
        reply.committed = text;
        reply.committed_len = text_len;
        reply.page = 0;
        return respond(out, cap, out_len, reply);
    }

    /* Raw commit: the composition is already the text the user wants (for
     * example an English word the dictionaries did not cover). */
    if (raw_fallback && g_commit_raw && has_composition) {
        reply.handled = true;
        reply.command = 3;
        reply.committed = raw;
        reply.committed_len = raw_len;
        reply.page = 0;
        return respond(out, cap, out_len, reply);
    }

    if (total > 0 && has_composition) {
        reply.handled = true;
        reply.command = 1;
        return respond(out, cap, out_len, reply);
    }
    return respond(out, cap, out_len, reply);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Reply reply;
    return respond(out, cap, out_len, reply);
}

PIME_EXPORT void pime_shutdown(void) {
    g_page_size = 5;
    g_commit_on_enter = true;
    g_commit_raw = true;
    g_show_labels = true;
    g_show_comments = true;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
