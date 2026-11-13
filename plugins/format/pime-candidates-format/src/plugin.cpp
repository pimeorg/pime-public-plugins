/*
 * pime-candidates-format: candidate display, selection and commit.
 *
 * Reads the first non-empty candidate array written earlier in the chain and
 * turns it into host commands. Writes "committed" so learn/user-dict plugins
 * can observe the selection in the same chain call.
 */
#include <pime/plugin_api.h>

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

const char* const kCandidateSources[] = {
    "pime-pinyin-rank",
    "pime-rank-default",
    "pime-pinyin-translate",
    "pime-wubi-translate",
    "pime-predict-context",
    "pime-handwriting-ncnn",
    "pime-speech-ncnn",
};

const char* const kRawSources[] = {
    "pime-pinyin-parse",
    "pime-wubi-parse",
    "pime-cangjie-parse",
};

const char* const kRawKeys[] = {"raw", "code", "code"};

ArrayView find_candidates(const Frame& frame) {
    for (const char* id : kCandidateSources) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (array.valid && array.count > 0) return array;
    }
    return ArrayView{};
}

bool raw_text(const Frame& frame, const char** text, uint32_t* len) {
    for (uint32_t i = 0; i < 3; ++i) {
        WorkspaceKey key = full_key(kRawSources[i], kRawKeys[i]);
        if (span_text(frame.workspace_value(key.text), text, len)) return true;
    }
    return false;
}

bool candidate_text(ArrayView candidates, uint64_t index, const char** text,
                    uint32_t* len) {
    if (!candidates.valid || index >= candidates.count) return false;
    MapView item = MapView::of(candidates.at(index));
    return item.valid && item.text("text", text, len);
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    bool handled, int command, ArrayView candidates,
                    uint32_t page, const char* committed,
                    uint32_t committed_len) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);

    w.key("workspace");
    if (committed) {
        w.map(1);
        w.key("committed");
        w.text(committed, committed_len);
    } else {
        w.map(0);
    }

    w.key("handled");
    w.boolean(handled);

    w.key("commands");
    if (command == 0) {
        w.array(0);
    } else {
        w.array(1);
        if (command == 1) {
            const uint64_t shown = candidates.count < page
                                       ? candidates.count
                                       : page;
            cmd_show_candidates_begin(w, static_cast<uint32_t>(shown));
            for (uint64_t i = 0; i < shown; ++i) {
                const char* text = nullptr;
                uint32_t len = 0;
                if (!candidate_text(candidates, i, &text, &len)) {
                    text = "";
                    len = 0;
                }
                cmd_candidate_item(w, text, len, nullptr, 0);
            }
        } else if (command == 2) {
            cmd_hide_candidates(w);
        } else {
            cmd_commit(w, committed, committed_len);
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
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t size = 0;
    if (settings.uint("page_size", &size) && size >= 1 && size <= 9)
        g_page_size = static_cast<uint32_t>(size);
    bool enter = true;
    if (settings.boolean("commit_on_enter", &enter)) g_commit_on_enter = enter;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    const ArrayView candidates = find_candidates(frame);

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"), &action,
              &action_len);

    const char* type = nullptr;
    uint32_t type_len = 0;
    frame.event.text("type", &type, &type_len);

    const bool is_key = Frame::equal(type, type_len, "key", 3);
    const bool is_candidate = Frame::equal(type, type_len, "candidate", 9);

    uint32_t page = g_page_size;
    uint64_t context_page = 0;
    if (frame.context.uint("page_size", &context_page) && context_page > 0)
        page = context_page > kMaxShown ? kMaxShown
                                        : static_cast<uint32_t>(context_page);

    if (is_key && Frame::equal(action, action_len, "escape", 6))
        return respond(out, cap, out_len, true, 2, candidates, page, nullptr, 0);

    bool commit = false;
    uint64_t index = 0;
    if (is_key && Frame::equal(action, action_len, "space", 5)) {
        commit = candidates.count > 0;
    } else if (is_key && Frame::equal(action, action_len, "enter", 5)) {
        commit = g_commit_on_enter && candidates.count > 0;
    } else if (is_candidate && frame.event.uint("index", &index)) {
        commit = index < candidates.count;
    }

    const char* text = nullptr;
    uint32_t text_len = 0;
    if (commit) {
        if (!candidate_text(candidates, index, &text, &text_len))
            return respond(out, cap, out_len, false, 0, candidates, page,
                           nullptr, 0);
        return respond(out, cap, out_len, true, 3, candidates, page, text,
                       text_len);
    }

    const char* raw = nullptr;
    uint32_t raw_len = 0;
    const bool has_composition = raw_text(frame, &raw, &raw_len) && raw_len > 0;
    if (candidates.count == 0 || !has_composition)
        return respond(out, cap, out_len, false, 0, candidates, page, nullptr, 0);
    return respond(out, cap, out_len, true, 1, candidates, page, nullptr, 0);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, false, 0, ArrayView{}, g_page_size,
                   nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) {
    g_page_size = 5;
    g_commit_on_enter = true;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
