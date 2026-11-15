/*
 * pime-rank-default: input-method-neutral candidate ranking.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxCandidates = 32;
alignas(8) uint8_t g_scratch[kScratchBytes];

uint32_t g_min_score = 0;
uint32_t g_max_candidates = 16;

struct Candidate {
    const char* text;
    uint32_t len;
    uint32_t score;
    const char* source;
    uint32_t source_len;
};

const char* const kSources[] = {"pime-pinyin-translate", "pime-wubi-translate",
                                "pime-cangjie-translate", "pime-t9-translate",
                                "pime-english-translate"};

uint32_t read_candidates(const Frame& frame, Candidate* out) {
    for (const char* id : kSources) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (!array.valid || array.count == 0) continue;
        uint32_t count = 0;
        for (uint64_t i = 0; i < array.count && count < kMaxCandidates; ++i) {
            MapView item = MapView::of(array.at(i));
            if (!item.valid) continue;
            Candidate candidate{};
            if (!item.text("text", &candidate.text, &candidate.len)) continue;
            uint64_t score = 0;
            item.uint("score", &score);
            if (score < g_min_score) continue;
            candidate.score = static_cast<uint32_t>(score);
            item.text("source", &candidate.source, &candidate.source_len);
            out[count++] = candidate;
        }
        return count;
    }
    return 0;
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

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Candidate* list, uint32_t count) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        w.map(list[i].source ? 3 : 2);
        w.key("text");
        w.text(list[i].text, list[i].len);
        w.key("score");
        w.u64(list[i].score);
        if (list[i].source) {
            w.key("source");
            w.text(list[i].source, list[i].source_len);
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
    g_min_score = 0;
    g_max_candidates = 16;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t value = 0;
    if (settings.uint("min_score", &value) && value <= 1000000)
        g_min_score = static_cast<uint32_t>(value);
    if (settings.uint("max_candidates", &value) && value >= 1 && value <= 32)
        g_max_candidates = static_cast<uint32_t>(value);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;
    Candidate candidates[kMaxCandidates];
    uint32_t count = read_candidates(frame, candidates);
    sort_candidates(candidates, count);
    if (count > g_max_candidates) count = g_max_candidates;
    return respond(out, cap, out_len, candidates, count);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0);
}

PIME_EXPORT void pime_shutdown(void) {
    g_min_score = 0;
    g_max_candidates = 16;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
