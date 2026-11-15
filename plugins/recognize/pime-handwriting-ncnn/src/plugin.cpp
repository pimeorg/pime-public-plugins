/*
 * pime-handwriting-ncnn: offline handwriting recognition through ml.infer.
 *
 * Events (plugin-defined, produced by the host's ink layer):
 *   touch     { x, y, group? }  accumulate a stroke point (group = character slot)
 *   touch.end                   run the model over the collected points
 *   handwriting.correct { wrong, right }
 *                               record a correction pair for later recognitions
 *
 * Enhancements over 1.0.0:
 *   - bounding-box normalisation: points are scaled into 0..255 per group;
 *   - multi-character grouping: points carry a `group` index; each group is run
 *     separately and the per-group top-1 characters form a concatenated
 *     candidate ("连写");
 *   - correction-sample learning: `fix/<wrong>\x1f<right>` counters make a
 *     corrected reading rank first the next time the model returns `wrong`.
 *
 * Output: plugin/pime-handwriting-ncnn/candidates = [{text, score, source, comment?}]
 * and `groups` (the number of character slots in the call).
 * Model output is a tensor of u32 label ids; assets/labels.txt maps ids to
 * characters. Missing backend -> PIME_UNSUPPORTED, missing labels -> NOT_FOUND.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/ml.hpp>
#include <pimeplug/text.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxPoints = 128;
constexpr uint32_t kMaxGroups = 4;
constexpr uint32_t kMaxCandidates = 8;
constexpr uint32_t kMaxText = 96;
constexpr uint32_t kMaxFixes = 4;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_labels[2048];
alignas(8) uint8_t g_tensor[2048];
alignas(8) uint8_t g_model_out[2048];
uint32_t g_labels_len = 0;
bool g_labels_loaded = false;

char g_combined[kMaxText];
uint32_t g_combined_len = 0;
char g_fix_texts[kMaxFixes][64];

struct Item {
    const char* text;
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

void load_state(const Frame& frame, uint8_t* points, uint32_t* points_len,
                uint8_t* groups, uint32_t* groups_len) {
    *points_len = 0;
    *groups_len = 0;
    MapView state = MapView::of(frame.root.get("state"));
    if (!state.valid) return;
    const uint8_t* data = nullptr;
    uint32_t len = 0;
    if (span_bytes(state.get("pts"), &data, &len)) {
        if (len > kMaxPoints * 2) len = kMaxPoints * 2;
        for (uint32_t i = 0; i < len; ++i) points[i] = data[i];
        *points_len = len;
    }
    if (span_bytes(state.get("groups"), &data, &len)) {
        if (len > kMaxPoints) len = kMaxPoints;
        for (uint32_t i = 0; i < len; ++i) groups[i] = data[i];
        *groups_len = len;
    }
}

uint8_t clamp_byte(uint64_t value) {
    return static_cast<uint8_t>(value > 255 ? 255 : value);
}

/* Scales the point cloud into 0..255 (per group). A degenerate box keeps the
 * original coordinates so a single tap still produces a valid tensor. */
void normalize(const uint8_t* points, uint32_t count, uint8_t* out) {
    if (count == 0) return;
    uint32_t min_x = 255;
    uint32_t max_x = 0;
    uint32_t min_y = 255;
    uint32_t max_y = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t x = points[i * 2];
        const uint32_t y = points[i * 2 + 1];
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
    }
    const uint32_t width = max_x - min_x;
    const uint32_t height = max_y - min_y;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t x = points[i * 2];
        const uint32_t y = points[i * 2 + 1];
        out[i * 2] = width == 0
                         ? static_cast<uint8_t>(x)
                         : static_cast<uint8_t>((x - min_x) * 255u / width);
        out[i * 2 + 1] = height == 0
                             ? static_cast<uint8_t>(y)
                             : static_cast<uint8_t>((y - min_y) * 255u / height);
    }
}

/* Collects group `slot` and normalises it. Returns the point count. */
uint32_t group_points(const uint8_t* points, uint32_t points_len,
                      const uint8_t* groups, uint32_t groups_len, uint8_t slot,
                      uint8_t* out) {
    const uint32_t count = points_len / 2;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count && kept < kMaxPoints; ++i) {
        const uint8_t group = i < groups_len ? groups[i] : 0;
        if (group != slot) continue;
        out[kept * 2] = points[i * 2];
        out[kept * 2 + 1] = points[i * 2 + 1];
        ++kept;
    }
    if (kept > 0) normalize(out, kept, out);
    return kept;
}

bool run_model(const uint8_t* points, uint32_t count, uint32_t* ids,
               uint32_t* id_count) {
    const uint32_t session =
        pime_host_ml_load(reinterpret_cast<const uint8_t*>("handwriting"), 11);
    if (session == 0) return false;

    bool encoded = false;
    const uint32_t tensor_len =
        ml_encode_u8(points, count * 2, 1, kMaxPoints, 2, 1, g_tensor,
                     sizeof(g_tensor), &encoded);
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

void bump_fix(const char* wrong, uint32_t wrong_len, const char* right,
              uint32_t right_len) {
    char key[192];
    uint32_t n = 0;
    const char prefix[] = "fix/";
    for (const char* p = prefix; *p; ++p) key[n++] = *p;
    for (uint32_t i = 0; i < wrong_len && n < sizeof(key) - 2; ++i)
        key[n++] = wrong[i];
    key[n++] = '\x1f';
    for (uint32_t i = 0; i < right_len && n < sizeof(key) - 1; ++i)
        key[n++] = right[i];

    char value[24];
    uint32_t value_len = 0;
    bool found = false;
    uint64_t count = 0;
    if (store_get(key, n, value, sizeof(value), &value_len, &found) && found)
        parse_uint(value, value_len, &count);
    ++count;
    const uint32_t out_len = format_uint(count, value, sizeof(value));
    store_set(key, n, value, out_len);
}

/* Corrected readings for `text` go in front so the fix wins next time. */
uint32_t collect_fixes(const char* text, uint32_t text_len, Item* items,
                       uint32_t* count) {
    if (text_len == 0 || *count >= kMaxCandidates) return 0;
    char prefix[192];
    uint32_t n = 0;
    const char lead[] = "fix/";
    for (const char* p = lead; *p; ++p) prefix[n++] = *p;
    for (uint32_t i = 0; i < text_len && n < sizeof(prefix) - 2; ++i)
        prefix[n++] = text[i];
    prefix[n++] = '\x1f';

    uint32_t added = 0;
    store_scan(prefix, n, kMaxFixes,
               [&](const char* key, uint32_t key_len, const char*,
                   uint32_t) {
                   if (key_len <= n || added >= kMaxFixes) return true;
                   const char* word = key + n;
                   const uint32_t word_len = key_len - n;
                   if (word_len == 0 || word_len >= 64) return true;
                   char* slot = g_fix_texts[added];
                   for (uint32_t i = 0; i < word_len; ++i) slot[i] = word[i];
                   Item& item = items[(*count)++];
                   item.text = slot;
                   item.len = word_len;
                   item.score = 1300;
                   item.source = "ml";
                   item.comment = "纠正";
                   ++added;
                   return *count < kMaxCandidates;
               });
    return added;
}

bool has_item(const Item* items, uint32_t count, const char* text,
              uint32_t len) {
    for (uint32_t i = 0; i < count; ++i) {
        if (items[i].len == len &&
            Frame::equal(items[i].text, items[i].len, text, len))
            return true;
    }
    return false;
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const uint8_t* points, uint32_t points_len,
                    const uint8_t* groups, uint32_t groups_len,
                    const Item* items, uint32_t count, uint32_t group_count,
                    bool handled) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    uint32_t fields = 0;
    if (points_len) ++fields;
    if (groups_len) ++fields;
    w.map(fields);
    if (points_len) {
        w.key("pts");
        w.bytes(points, points_len);
    }
    if (groups_len) {
        w.key("groups");
        w.bytes(groups, groups_len);
    }

    w.key("workspace");
    w.map(2);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t item_fields = 3;
        if (items[i].comment) ++item_fields;
        w.map(item_fields);
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
    w.key("groups");
    w.u64(group_count);

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
    uint8_t groups[kMaxPoints];
    uint32_t points_len = 0;
    uint32_t groups_len = 0;
    load_state(frame, points, &points_len, groups, &groups_len);

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_touch = has_type && text_equal(type, type_len, "touch");

    if (is_touch) {
        uint64_t x = 0;
        uint64_t y = 0;
        if (!frame.event.uint("x", &x) || !frame.event.uint("y", &y))
            return respond(out, cap, out_len, points, points_len, groups,
                           groups_len, nullptr, 0, 0, false);
        uint64_t group = 0;
        if (!frame.event.uint("group", &group)) group = 0;
        if (points_len + 2 <= kMaxPoints * 2 && groups_len < kMaxPoints) {
            points[points_len++] = clamp_byte(x);
            points[points_len++] = clamp_byte(y);
            groups[groups_len++] =
                static_cast<uint8_t>(group < 255 ? group : 255);
        }
        return respond(out, cap, out_len, points, points_len, groups,
                       groups_len, nullptr, 0, 0, true);
    }

    if (has_type && text_equal(type, type_len, "handwriting.correct")) {
        const char* wrong = nullptr;
        const char* right = nullptr;
        uint32_t wrong_len = 0;
        uint32_t right_len = 0;
        if (frame.event.text("wrong", &wrong, &wrong_len) &&
            frame.event.text("right", &right, &right_len) && wrong_len > 0 &&
            right_len > 0)
            bump_fix(wrong, wrong_len, right, right_len);
        return respond(out, cap, out_len, points, points_len, groups,
                       groups_len, nullptr, 0, 0, false);
    }

    if (has_type && text_equal(type, type_len, "touch.end")) {
        if (points_len == 0 || !load_labels()) return PIME_NOT_FOUND;

        /* Distinct character slots, in first-appearance order. */
        uint8_t slots[kMaxGroups];
        uint32_t slot_count = 0;
        const uint32_t point_count = points_len / 2;
        for (uint32_t i = 0; i < point_count; ++i) {
            const uint8_t group = i < groups_len ? groups[i] : 0;
            bool seen = false;
            for (uint32_t k = 0; k < slot_count; ++k)
                if (slots[k] == group) seen = true;
            if (!seen && slot_count < kMaxGroups) slots[slot_count++] = group;
        }
        if (slot_count == 0) return PIME_CORRUPT;

        Item items[kMaxCandidates];
        uint32_t count = 0;
        uint8_t buffer[kMaxPoints * 2];
        g_combined_len = 0;

        for (uint32_t s = 0; s < slot_count; ++s) {
            const uint32_t kept =
                group_points(points, points_len, groups, groups_len, slots[s],
                             buffer);
            if (kept == 0) continue;
            uint32_t ids[kMaxCandidates];
            uint32_t id_count = 0;
            if (!run_model(buffer, kept, ids, &id_count)) return PIME_UNSUPPORTED;
            const uint32_t take = slot_count == 1 ? 2 : 1;
            for (uint32_t i = 0; i < id_count && i < take; ++i) {
                const char* text = nullptr;
                uint32_t text_len = 0;
                if (!label_for(ids[i], &text, &text_len)) continue;
                if (i == 0 && g_combined_len + text_len < sizeof(g_combined)) {
                    for (uint32_t k = 0; k < text_len; ++k)
                        g_combined[g_combined_len + k] = text[k];
                    g_combined_len += text_len;
                }
                if (count >= kMaxCandidates) continue;
                if (has_item(items, count, text, text_len)) continue;
                Item& item = items[count++];
                item.text = text;
                item.len = text_len;
                item.score = 1000 - i * 10;
                item.source = "ml";
                item.comment = nullptr;
            }
        }

        if (slot_count > 1 && g_combined_len > 0) {
            /* The concatenated reading leads the list. */
            if (count >= kMaxCandidates) --count;
            for (uint32_t i = count; i > 0; --i) items[i] = items[i - 1];
            Item& item = items[0];
            item.text = g_combined;
            item.len = g_combined_len;
            item.score = 1200;
            item.source = "ml";
            item.comment = "连写";
            ++count;
        }

        /* Correction samples go in front of the model reading. */
        if (count > 0) {
            const char* top = items[0].text;
            const uint32_t top_len = items[0].len;
            Item fixes[kMaxFixes];
            uint32_t fix_count = 0;
            collect_fixes(top, top_len, fixes, &fix_count);
            if (fix_count > 0) {
                for (uint32_t i = count; i > 0; --i)
                    if (i + fix_count - 1 < kMaxCandidates)
                        items[i + fix_count - 1] = items[i - 1];
                if (count + fix_count > kMaxCandidates)
                    count = kMaxCandidates - fix_count;
                for (uint32_t i = 0; i < fix_count; ++i) items[i] = fixes[i];
                count += fix_count;
            }
        }

        return respond(out, cap, out_len, points, 0, groups, 0, items, count,
                       slot_count, true);
    }

    return respond(out, cap, out_len, points, points_len, groups, groups_len,
                   nullptr, 0, 0, false);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0, nullptr, 0, nullptr, 0, 0,
                   false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_labels_loaded = false;
    g_labels_len = 0;
    g_combined_len = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
