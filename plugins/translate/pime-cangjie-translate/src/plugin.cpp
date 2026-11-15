/*
 * pime-cangjie-translate: Cangjie code -> scored candidates.
 *
 * Reads plugin/pime-cangjie-parse/code, looks up the packaged Cangjie table and
 * writes plugin/pime-cangjie-translate/candidates.
 *
 * Exact codes come first; shorter codes list the rest of the code as a comment
 * so the user can see what the completion will cost. The parser owns the code
 * length limit; this plugin only answers.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/asset.hpp>
#include <pimeplug/candidates.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxKey = 8;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kExactBoost = 10000;
constexpr uint32_t kCompletionCost = 200;
constexpr uint32_t kMaxDictBytes = 65536;

const char kParseId[] = "pime-cangjie-parse";

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_dict[kMaxDictBytes];
uint32_t g_dict_len = 0;
bool g_dict_loaded = false;

uint32_t g_max_candidates = 8;
uint32_t g_completion_limit = 24;
bool g_completion = true;

Candidate g_candidates[kMaxCandidates];
uint32_t g_count = 0;

bool load_dict() {
    if (g_dict_loaded) return g_dict_len > 0;
    g_dict_loaded = true;
    AssetHandle handle;
    if (!asset_open("cangjie.dict", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 && size + 64 <= sizeof(g_dict);
    if (ok) {
        uint32_t got = 0;
        ok = asset_read(handle, 0, size, g_dict, sizeof(g_dict), &got) && got > 0;
        if (ok) g_dict_len = got;
    }
    asset_close(handle);
    return ok;
}

uint32_t read_code(const Frame& frame, char* key) {
    uint32_t key_len = 0;
    WorkspaceKey code = full_key(kParseId, "code");
    const char* text = nullptr;
    uint32_t len = 0;
    if (span_text(frame.workspace_value(code.text), &text, &len)) {
        for (uint32_t i = 0; i < len && key_len < kMaxKey; ++i) {
            char c = text[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c >= 'a' && c <= 'z') key[key_len++] = c;
        }
    }
    return key_len;
}

void push_candidate(const char* text, uint32_t text_len, int32_t score,
                    const char* source, const char* comment,
                    uint32_t comment_len) {
    const uint32_t limit =
        g_max_candidates < kMaxCandidates ? g_max_candidates : kMaxCandidates;
    if (g_count >= limit) return;
    Candidate& c = g_candidates[g_count++];
    c.set_text(text, text_len);
    c.score = score;
    c.set_source(source);
    c.comment_len = 0;
    if (comment && comment_len) c.set_comment(comment, comment_len);
}

void sort_and_write(Writer& w) {
    sort_candidates(g_candidates, g_count);
    uint32_t written = 0;
    for (uint32_t i = 0; i < g_count; ++i) {
        bool duplicate = false;
        for (uint32_t j = 0; j < written; ++j) {
            if (g_candidates[j].text_len == g_candidates[i].text_len &&
                Frame::equal(g_candidates[j].text, g_candidates[j].text_len,
                             g_candidates[i].text, g_candidates[i].text_len)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        g_candidates[written++] = g_candidates[i];
    }
    write_candidates(w, g_candidates, written);
    g_count = 0;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_dict_loaded = false;
    g_dict_len = 0;
    g_max_candidates = 8;
    g_completion_limit = 24;
    g_completion = true;
    g_count = 0;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t value = 0;
    if (settings.uint("max_candidates", &value) && value >= 1 && value <= 32)
        g_max_candidates = static_cast<uint32_t>(value);
    if (settings.uint("completion_limit", &value) && value >= 0 && value <= 32)
        g_completion_limit = static_cast<uint32_t>(value);
    bool flag = true;
    if (settings.boolean("completion", &flag)) g_completion = flag;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    if (!load_dict()) return PIME_NOT_FOUND;

    char key[kMaxKey];
    const uint32_t key_len = read_code(frame, key);

    g_count = 0;
    if (key_len > 0) {
        Dict dict;
        dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);
        dict.match(key, key_len, [&](const DictEntry& entry) {
            push_candidate(entry.text, entry.text_len,
                           static_cast<int32_t>(entry.freq + kExactBoost),
                           "dict", nullptr, 0);
        });
        if (g_completion) {
            uint32_t completions = 0;
            dict.prefix(key, key_len, [&](const DictEntry& entry) {
                if (entry.key_len <= key_len ||
                    completions >= g_completion_limit)
                    return;
                const uint32_t rest = entry.key_len - key_len;
                const int32_t score =
                    static_cast<int32_t>(entry.freq) -
                    static_cast<int32_t>(kCompletionCost * rest);
                push_candidate(entry.text, entry.text_len, score, "completion",
                               entry.key + key_len, rest);
                ++completions;
            });
        }
    }

    const bool any = g_count > 0;
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    sort_and_write(w);
    w.key("handled");
    w.boolean(any);
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
    g_count = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
