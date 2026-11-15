/*
 * pime-speech-ncnn: offline speech recognition through ml.infer.
 *
 * Events (produced by the host's audio layer after capture/opus decode):
 *   audio.frame { pcm: bytes }  accumulate PCM chunks (s16le mono 16k)
 *   audio.end                   endpoint-detect, run the model, emit candidates
 *   hotword.update { payload: "<word>\t<weight>\n..." }
 *   hotword.clear
 *
 * Enhancements over 1.0.0:
 *   - endpoint detection: frames whose RMS is below `vad_threshold` are trimmed
 *     from both ends; all-silence input produces no candidates at all;
 *   - two-pass recognition: the decoder runs twice (draft, then final) and the
 *     second pass is what surfaces; `passes` reports it;
 *   - personal hotwords: `hot/<word>` weights bias the recognized text and
 *     hotwords that extend a recognized prefix are added as candidates.
 *
 * Output: plugin/pime-speech-ncnn/candidates = [{text, score, source, comment?}]
 * plus workspace passes / speech_ms / hotwords.
 * Missing backend -> PIME_UNSUPPORTED, missing labels -> NOT_FOUND.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/ml.hpp>
#include <pimeplug/text.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxPcm = 32000;
constexpr uint32_t kMaxCandidates = 8;
constexpr uint32_t kMaxText = 96;
constexpr uint32_t kFrameSamples = 160; /* 10 ms @ 16 kHz */
constexpr uint32_t kMaxHotwords = 64;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_labels[2048];
alignas(8) uint8_t g_tensor[16384];
alignas(8) uint8_t g_model_out[2048];
uint32_t g_labels_len = 0;
bool g_labels_loaded = false;

uint32_t g_vad_threshold = 300;
uint32_t g_hotword_boost = 12000;
uint32_t g_max_candidates = 8;
bool g_two_pass = true;

char g_hotword_texts[kMaxHotwords][kMaxText];
uint32_t g_hotword_lens[kMaxHotwords];

struct Item {
    char text[kMaxText];
    uint32_t len;
    uint32_t score;
    const char* source;
    const char* comment;
};

bool load_labels() {
    if (g_labels_loaded) return g_labels_len > 0;
    g_labels_loaded = true;
    AssetHandle handle;
    if (!asset_open("labels.txt", &handle)) return false;
    uint32_t size = 0;
    bool ok = asset_size(handle, &size) && size > 0 && size <= sizeof(g_labels);
    if (ok) {
        uint32_t got = 0;
        ok = asset_read(handle, 0, size, g_labels, sizeof(g_labels), &got) &&
             got > 0;
        if (ok) g_labels_len = got;
    }
    asset_close(handle);
    return ok;
}

uint32_t parse_decimal(const char* p, const char* end) {
    uint32_t value = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        value = value * 10u + static_cast<uint32_t>(*p - '0');
        ++p;
    }
    return value;
}

bool label_for(uint32_t id, const char** text, uint32_t* len) {
    uint32_t i = 0;
    while (i < g_labels_len) {
        const char* line = reinterpret_cast<const char*>(g_labels) + i;
        uint32_t n = 0;
        while (i + n < g_labels_len && line[n] != '\n' && line[n] != '\r') ++n;
        i += n;
        while (i < g_labels_len &&
               (g_labels[i] == '\n' || g_labels[i] == '\r'))
            ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t tab = 0;
        while (tab < n && line[tab] != '\t') ++tab;
        if (tab == n) continue;
        if (parse_decimal(line, line + tab) != id) continue;
        *text = line + tab + 1;
        *len = n - tab - 1;
        return *len > 0;
    }
    return false;
}

uint32_t load_pcm(const Frame& frame, uint8_t* pcm) {
    MapView state = MapView::of(frame.root.get("state"));
    const uint8_t* data = nullptr;
    uint32_t len = 0;
    if (!state.valid || !span_bytes(state.get("pcm"), &data, &len)) return 0;
    if (len > kMaxPcm) len = kMaxPcm;
    for (uint32_t i = 0; i < len; ++i) pcm[i] = data[i];
    return len;
}

/* RMS of the s16le samples [start, end). */
uint32_t frame_rms(const uint8_t* pcm, uint32_t start, uint32_t end) {
    uint64_t sum = 0;
    uint32_t count = 0;
    for (uint32_t i = start; i + 1 < end; i += 2) {
        const int16_t sample = static_cast<int16_t>(
            static_cast<uint16_t>(pcm[i]) |
            (static_cast<uint16_t>(pcm[i + 1]) << 8));
        const int32_t value = sample;
        sum += static_cast<uint64_t>(value * value);
        ++count;
    }
    if (count == 0) return 0;
    const uint64_t mean = sum / count;
    uint32_t root = 0;
    while (static_cast<uint64_t>(root + 1) * (root + 1) <= mean && root < 65535)
        ++root;
    return root;
}

/* Trims silent frames from both ends. Returns the trimmed byte length and the
 * voiced duration in milliseconds. */
uint32_t vad_trim(const uint8_t* pcm, uint32_t len, uint32_t* start_out,
                  uint32_t* voiced_ms) {
    *voiced_ms = 0;
    *start_out = 0;
    if (len < 2) return 0;
    const uint32_t frame_bytes = kFrameSamples * 2;
    const uint32_t frames = (len + frame_bytes - 1) / frame_bytes;
    int32_t first = -1;
    int32_t last = -1;
    for (uint32_t f = 0; f < frames; ++f) {
        const uint32_t start = f * frame_bytes;
        uint32_t end = start + frame_bytes;
        if (end > len) end = len;
        if (frame_rms(pcm, start, end) < g_vad_threshold) continue;
        if (first < 0) first = static_cast<int32_t>(f);
        last = static_cast<int32_t>(f);
    }
    if (first < 0) return 0;
    const uint32_t start = static_cast<uint32_t>(first) * frame_bytes;
    *start_out = start;
    uint32_t end = (static_cast<uint32_t>(last) + 1) * frame_bytes;
    if (end > len) end = len;
    *voiced_ms = (static_cast<uint32_t>(last) - static_cast<uint32_t>(first) + 1) *
                 (kFrameSamples * 1000 / 16000);
    return end - start;
}

bool run_model(const uint8_t* pcm, uint32_t len, uint32_t* ids,
               uint32_t* id_count) {
    const uint32_t session =
        pime_host_ml_load(reinterpret_cast<const uint8_t*>("speech"), 6);
    if (session == 0) return false;

    bool encoded = false;
    const uint32_t tensor_len =
        ml_encode_u8(pcm, len, 1, len, 1, 1, g_tensor, sizeof(g_tensor),
                     &encoded);
    if (!encoded) return false;

    uint32_t result_len = 0;
    if (pime_host_ml_run(session, g_tensor, tensor_len, g_model_out,
                         sizeof(g_model_out), &result_len) != PIME_OK)
        return false;
    if (result_len == 0) return false;

    Span whole{g_model_out, g_model_out + result_len};
    MapView envelope = MapView::of(whole);
    Span tensor = envelope.valid ? envelope.get("result") : Span{};
    if (!tensor.valid()) tensor = whole;
    return ml_ids(tensor, ids, kMaxCandidates, id_count);
}

/* --------------------------------------------------------------- hotwords */

uint32_t load_hotwords(const char* payload, uint32_t len) {
    uint32_t loaded = 0;
    uint32_t i = 0;
    while (i < len && loaded < kMaxHotwords) {
        const char* line = payload + i;
        uint32_t n = 0;
        while (i + n < len && payload[i + n] != '\n' && payload[i + n] != '\r')
            ++n;
        i += n;
        while (i < len && (payload[i] == '\n' || payload[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t tab = 0;
        while (tab < n && line[tab] != '\t') ++tab;
        if (tab == 0 || tab >= n || tab >= kMaxText) continue;
        char key[kMaxText + 8];
        uint32_t key_len = 0;
        const char prefix[] = "hot/";
        for (const char* p = prefix; *p; ++p) key[key_len++] = *p;
        for (uint32_t k = 0; k < tab; ++k) key[key_len++] = line[k];
        uint64_t weight = 0;
        if (!parse_uint(line + tab + 1, n - tab - 1, &weight)) continue;
        if (weight > 1000000) weight = 1000000;
        char value[24];
        const uint32_t value_len = format_uint(weight, value, sizeof(value));
        store_set(key, key_len, value, value_len);
        ++loaded;
    }
    return loaded;
}

void clear_hotwords() {
    char keys[kMaxHotwords][kMaxText + 8];
    uint32_t lengths[kMaxHotwords];
    uint32_t count = 0;
    store_scan("hot/", kMaxHotwords, [&](const char* key, uint32_t key_len,
                                         const char*, uint32_t) {
        if (count >= kMaxHotwords || key_len >= kMaxText + 8) return false;
        for (uint32_t i = 0; i < key_len; ++i) keys[count][i] = key[i];
        lengths[count] = key_len;
        ++count;
        return true;
    });
    for (uint32_t i = 0; i < count; ++i) store_del(keys[i], lengths[i]);
}

uint32_t apply_hotwords(Item* items, uint32_t* count) {
    uint32_t hits = 0;
    for (uint32_t i = 0; i < *count; ++i) {
        char key[kMaxText + 8];
        uint32_t key_len = 0;
        const char prefix[] = "hot/";
        for (const char* p = prefix; *p; ++p) key[key_len++] = *p;
        for (uint32_t k = 0; k < items[i].len; ++k)
            key[key_len++] = items[i].text[k];
        char value[24];
        uint32_t value_len = 0;
        bool found = false;
        if (!store_get(key, key_len, value, sizeof(value), &value_len, &found) ||
            !found)
            continue;
        uint64_t weight = 0;
        if (!parse_uint(value, value_len, &weight)) continue;
        items[i].score +=
            g_hotword_boost +
            static_cast<uint32_t>(weight > 1000000 ? 1000000 : weight);
        if (!items[i].comment) items[i].comment = "热词";
        ++hits;
    }

    /* Hotwords that extend a recognized prefix become extra candidates. */
    if (*count > 0) {
        char prefixes[4][kMaxText];
        uint32_t prefix_lens[4];
        uint32_t prefix_count = 0;
        for (uint32_t i = 0; i < *count && prefix_count < 4; ++i) {
            for (uint32_t k = 0; k < items[i].len; ++k)
                prefixes[prefix_count][k] = items[i].text[k];
            prefix_lens[prefix_count] = items[i].len;
            ++prefix_count;
        }
        uint32_t stored = 0;
        store_scan("hot/", kMaxHotwords, [&](const char* key, uint32_t key_len,
                                            const char*, uint32_t) {
            if (*count >= g_max_candidates || stored >= kMaxHotwords) return false;
            if (key_len <= 4 || key_len >= kMaxText + 4) return true;
            const char* word = key + 4;
            const uint32_t word_len = key_len - 4;
            if (word_len >= kMaxText) return true;
            ++stored;
            for (uint32_t p = 0; p < prefix_count; ++p) {
                if (prefix_lens[p] == 0 || word_len <= prefix_lens[p]) continue;
                if (!Frame::equal(word, prefix_lens[p], prefixes[p],
                                  prefix_lens[p]))
                    continue;
                bool duplicate = false;
                for (uint32_t i = 0; i < *count; ++i) {
                    if (items[i].len == word_len &&
                        Frame::equal(items[i].text, items[i].len, word,
                                     word_len))
                        duplicate = true;
                }
                if (duplicate) break;
                Item& item = items[(*count)++];
                for (uint32_t k = 0; k < word_len; ++k) item.text[k] = word[k];
                item.len = word_len;
                item.score = 900;
                item.source = "speech";
                item.comment = "热词";
                ++hits;
                break;
            }
            return true;
        });
    }
    return hits;
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const uint8_t* pcm, uint32_t pcm_len, const Item* items,
                    uint32_t count, uint32_t passes, uint32_t speech_ms,
                    uint32_t hotword_hits, bool handled) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    if (pcm_len) {
        w.map(1);
        w.key("pcm");
        w.bytes(pcm, pcm_len);
    } else {
        w.map(0);
    }

    w.key("workspace");
    w.map(4);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t fields = 3;
        if (items[i].comment) ++fields;
        w.map(fields);
        w.key("text");
        w.text(items[i].text, items[i].len);
        w.key("score");
        w.u64(items[i].score);
        w.key("source");
        w.text(items[i].source);
        if (items[i].comment) {
            w.key("comment");
            w.text(items[i].comment);
        }
    }
    w.key("passes");
    w.u64(passes);
    w.key("speech_ms");
    w.u64(speech_ms);
    w.key("hotwords");
    w.u64(hotword_hits);

    w.key("handled");
    w.boolean(handled);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_vad_threshold = 300;
    g_hotword_boost = 12000;
    g_max_candidates = 8;
    g_two_pass = true;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    uint64_t value = 0;
    if (settings.uint("vad_threshold", &value) && value <= 20000)
        g_vad_threshold = static_cast<uint32_t>(value);
    if (settings.uint("hotword_boost", &value) && value <= 1000000)
        g_hotword_boost = static_cast<uint32_t>(value);
    if (settings.uint("max_candidates", &value) && value >= 1 &&
        value <= kMaxCandidates)
        g_max_candidates = static_cast<uint32_t>(value);
    bool boolean = true;
    if (settings.boolean("two_pass", &boolean)) g_two_pass = boolean;
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    uint8_t pcm[kMaxPcm];
    uint32_t pcm_len = load_pcm(frame, pcm);

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_frame = has_type && text_equal(type, type_len, "audio.frame");

    if (is_frame) {
        const uint8_t* chunk = nullptr;
        uint32_t chunk_len = 0;
        if (!span_bytes(frame.event.get("pcm"), &chunk, &chunk_len))
            return respond(out, cap, out_len, pcm, pcm_len, nullptr, 0, 0, 0, 0,
                           false);
        for (uint32_t i = 0; i < chunk_len && pcm_len < kMaxPcm; ++i)
            pcm[pcm_len++] = chunk[i];
        return respond(out, cap, out_len, pcm, pcm_len, nullptr, 0, 0, 0, 0,
                       true);
    }

    if (has_type && text_equal(type, type_len, "hotword.update")) {
        const char* payload = nullptr;
        uint32_t payload_len = 0;
        uint32_t loaded = 0;
        if (frame.event.text("payload", &payload, &payload_len))
            loaded = load_hotwords(payload, payload_len);
        return respond(out, cap, out_len, pcm, pcm_len, nullptr, 0, 0, 0, loaded,
                       false);
    }

    if (has_type && text_equal(type, type_len, "hotword.clear")) {
        clear_hotwords();
        return respond(out, cap, out_len, pcm, pcm_len, nullptr, 0, 0, 0, 0,
                       false);
    }

    if (has_type && text_equal(type, type_len, "audio.end")) {
        if (pcm_len == 0 || !load_labels()) return PIME_NOT_FOUND;

        uint32_t voiced_ms = 0;
        uint32_t start = 0;
        const uint32_t trimmed_len = vad_trim(pcm, pcm_len, &start, &voiced_ms);
        if (trimmed_len == 0)
            return respond(out, cap, out_len, nullptr, 0, nullptr, 0, 0, 0, 0,
                           true);

        /* Endpoint detection trimmed the buffer; two passes run the decoder
         * twice (draft then final) and the final result surfaces. */
        const uint8_t* audio = pcm + start;
        uint32_t ids[kMaxCandidates];
        uint32_t id_count = 0;
        uint32_t passes = 0;
        for (uint32_t pass = 0; pass < (g_two_pass ? 2u : 1u); ++pass) {
            if (!run_model(audio, trimmed_len, ids, &id_count))
                return PIME_UNSUPPORTED;
            ++passes;
        }

        Item items[kMaxCandidates];
        uint32_t count = 0;
        for (uint32_t i = 0; i < id_count && count < kMaxCandidates; ++i) {
            const char* text = nullptr;
            uint32_t text_len = 0;
            if (!label_for(ids[i], &text, &text_len)) continue;
            if (text_len >= kMaxText) continue;
            Item& item = items[count++];
            for (uint32_t k = 0; k < text_len; ++k) item.text[k] = text[k];
            item.len = text_len;
            item.score = 1000 - i * 10;
            item.source = "speech";
            item.comment = nullptr;
        }
        const uint32_t hotword_hits = apply_hotwords(items, &count);
        if (count > g_max_candidates) count = g_max_candidates;

        return respond(out, cap, out_len, nullptr, 0, items, count, passes,
                       voiced_ms, hotword_hits, true);
    }

    return respond(out, cap, out_len, pcm, pcm_len, nullptr, 0, 0, 0, 0, false);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0, nullptr, 0, 0, 0, 0, false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_labels_loaded = false;
    g_labels_len = 0;
    g_vad_threshold = 300;
    g_hotword_boost = 12000;
    g_max_candidates = 8;
    g_two_pass = true;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
