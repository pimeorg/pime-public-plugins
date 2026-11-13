/*
 * pime-wubi-translate: Wubi code -> scored candidates.
 *
 * Reads plugin/pime-wubi-parse/code, looks up the packaged Wubi dictionary, and
 * writes plugin/pime-wubi-translate/candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/dict.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxKey = 8;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kExactBoost = 10000;

const char kParseId[] = "pime-wubi-parse";

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_dict[8192];
uint32_t g_dict_len = 0;
bool g_dict_loaded = false;
uint32_t g_max_candidates = 8;

struct Candidate {
    const char* text;
    uint32_t len;
    uint32_t score;
};

bool load_dict() {
    if (g_dict_loaded) return g_dict_len > 0;
    g_dict_loaded = true;
    AssetHandle handle;
    if (!asset_open("wubi.dict", &handle)) return false;
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

void add_candidate(Candidate* list, uint32_t* count, const DictEntry& entry,
                   uint32_t score) {
    const uint32_t limit = g_max_candidates < kMaxCandidates ? g_max_candidates
                                                             : kMaxCandidates;
    uint32_t position = *count;
    while (position > 0 && list[position - 1].score < score) --position;
    if (*count == limit && position == limit) return;
    const uint32_t last = (*count < limit) ? (*count)++ : (limit - 1);
    for (uint32_t i = last; i > position; --i) list[i] = list[i - 1];
    list[position].text = entry.text;
    list[position].len = entry.text_len;
    list[position].score = score;
}

uint32_t build_key(const Frame& frame, char* key) {
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

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_dict_loaded = false;
    g_dict_len = 0;
    g_max_candidates = 8;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (settings.valid) {
        uint64_t value = 0;
        if (settings.uint("max_candidates", &value) && value >= 1 && value <= 32)
            g_max_candidates = static_cast<uint32_t>(value);
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

    char key[kMaxKey];
    const uint32_t key_len = build_key(frame, key);

    Candidate candidates[kMaxCandidates];
    uint32_t count = 0;
    if (key_len > 0) {
        Dict dict;
        dict.load(reinterpret_cast<const char*>(g_dict), g_dict_len);
        dict.prefix(key, key_len, [&](const DictEntry& entry) {
            if (count >= g_max_candidates) return;
            const uint32_t score =
                entry.freq + (entry.key_len == key_len ? kExactBoost : 0);
            add_candidate(candidates, &count, entry, score);
        });
    }

    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        w.map(3);
        w.key("text");
        w.text(candidates[i].text, candidates[i].len);
        w.key("score");
        w.u64(candidates[i].score);
        w.key("source");
        w.text("dict");
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
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
