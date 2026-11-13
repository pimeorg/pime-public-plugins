/*
 * pime-handwriting-ncnn: offline handwriting recognition through ml.infer.
 *
 * Events (plugin-defined, produced by the host's ink layer):
 *   touch     { x, y }   (0..255)  accumulate a stroke
 *   touch.end                      run the model over the collected points
 *
 * Output: plugin/pime-handwriting-ncnn/candidates = [{text, score, source}].
 * Model output is a tensor of u32 label ids; assets/labels.txt maps ids to
 * characters. Missing backend -> PIME_UNSUPPORTED, missing labels -> NOT_FOUND.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/ml.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxPoints = 128;
constexpr uint32_t kMaxCandidates = 8;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_labels[2048];
alignas(8) uint8_t g_tensor[2048];
alignas(8) uint8_t g_model_out[2048];
uint32_t g_labels_len = 0;
bool g_labels_loaded = false;

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

uint32_t load_points(const Frame& frame, uint8_t* points) {
    MapView state = MapView::of(frame.root.get("state"));
    const uint8_t* data = nullptr;
    uint32_t len = 0;
    if (!state.valid || !span_bytes(state.get("pts"), &data, &len)) return 0;
    const uint32_t limit = kMaxPoints * 2;
    if (len > limit) len = limit;
    for (uint32_t i = 0; i < len; ++i) points[i] = data[i];
    return len;
}

uint8_t clamp_byte(uint64_t value) {
    return static_cast<uint8_t>(value > 255 ? 255 : value);
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const uint8_t* points, uint32_t points_len,
                    const uint32_t* ids, uint32_t count, bool handled) {
    const char* texts[kMaxCandidates];
    uint32_t texts_len[kMaxCandidates];
    uint32_t found = 0;
    for (uint32_t i = 0; i < count && found < kMaxCandidates; ++i) {
        if (label_for(ids[i], &texts[found], &texts_len[found])) ++found;
    }

    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(1);
    w.key("pts");
    w.bytes(points, points_len);

    w.key("workspace");
    w.map(1);
    w.key("candidates");
    w.array(found);
    for (uint32_t i = 0; i < found; ++i) {
        w.map(3);
        w.key("text");
        w.text(texts[i], texts_len[i]);
        w.key("score");
        w.u64(1000u - i * 10u);
        w.key("source");
        w.text("ml");
    }

    w.key("handled");
    w.boolean(handled);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t*, uint32_t) { return PIME_OK; }

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    uint8_t points[kMaxPoints * 2];
    uint32_t points_len = load_points(frame, points);

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_touch = has_type && type_len == 5 &&
                          Frame::equal(type, 5, "touch", 5);

    if (is_touch) {
        uint64_t x = 0;
        uint64_t y = 0;
        if (!frame.event.uint("x", &x) || !frame.event.uint("y", &y))
            return respond(out, cap, out_len, points, points_len, nullptr, 0,
                           false);
        if (points_len + 2 <= kMaxPoints * 2) {
            points[points_len++] = clamp_byte(x);
            points[points_len++] = clamp_byte(y);
        }
        return respond(out, cap, out_len, points, points_len, nullptr, 0, true);
    }

    if (has_type && type_len == 9 && Frame::equal(type, 9, "touch.end", 9)) {
        if (points_len == 0 || !load_labels()) return PIME_NOT_FOUND;
        const uint32_t session =
            pime_host_ml_load(reinterpret_cast<const uint8_t*>("handwriting"),
                              11);
        if (session == 0) return PIME_UNSUPPORTED;

        bool encoded = false;
        const uint32_t tensor_len = ml_encode_u8(
            points, points_len, 1, kMaxPoints, 2, 1, g_tensor, sizeof(g_tensor),
            &encoded);
        if (!encoded) return PIME_BUFFER_TOO_SMALL;

        uint32_t result_len = 0;
        const pime_status status =
            pime_host_ml_run(session, g_tensor, tensor_len, g_model_out,
                             sizeof(g_model_out), &result_len);
        if (status != PIME_OK) return status;
        if (result_len == 0) return PIME_CORRUPT;

        Span whole{g_model_out, g_model_out + result_len};
        MapView envelope = MapView::of(whole);
        Span tensor = envelope.valid ? envelope.get("result") : Span{};
        if (!tensor.valid()) tensor = whole;

        uint32_t ids[kMaxCandidates];
        uint32_t count = 0;
        if (!ml_ids(tensor, ids, kMaxCandidates, &count)) return PIME_CORRUPT;

        return respond(out, cap, out_len, points, 0, ids, count, true);
    }

    return respond(out, cap, out_len, points, points_len, nullptr, 0, false);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0, nullptr, 0, false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_labels_loaded = false;
    g_labels_len = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
