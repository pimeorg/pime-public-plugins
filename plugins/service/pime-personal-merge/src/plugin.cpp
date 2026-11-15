/*
 * pime-personal-merge: Lamport-versioned personal data merge.
 *
 * WeaveText parity (core/weave-engine/src/personal.rs): personal entries carry
 * a Lamport counter; merging keeps the entry with the larger counter and never
 * loses the local value on an equal counter (deterministic tie-break on the
 * value bytes, so two devices converge).
 *
 * Events:
 *   { type: "personal.merge",
 *     entries: [ { key: tstr, value: tstr, lamport: uint } ] }
 * The host/cloud service feeds downloaded entries here; this plugin never
 * uploads anything.
 *
 * Storage: entry/<key> = "<lamport>\x1f<value>"
 * Workspace out: merged (uint), conflicts (uint), total (uint).
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxKey = 128;
constexpr uint32_t kMaxValue = 512;
constexpr uint32_t kMaxEntries = 64;

alignas(8) uint8_t g_scratch[kScratchBytes];

bool g_enabled = true;
uint32_t g_max_entries = 32;

struct Result {
    uint32_t merged = 0;
    uint32_t conflicts = 0;
    uint32_t total = 0;
};

uint32_t make_key(const char* name, uint32_t name_len, char* out) {
    const char prefix[] = "entry/";
    uint32_t n = 0;
    for (const char* p = prefix; *p; ++p) out[n++] = *p;
    for (uint32_t i = 0; i < name_len && n < kMaxKey - 1; ++i)
        out[n++] = name[i];
    out[n] = '\0';
    return n;
}

bool read_entry(const char* key, uint32_t key_len, uint64_t* lamport,
                char* value, uint32_t* value_len, bool* found) {
    char buffer[kMaxValue + 32];
    uint32_t buffer_len = 0;
    *found = false;
    if (!store_get(key, key_len, buffer, sizeof(buffer), &buffer_len, found) ||
        !*found)
        return false;
    uint32_t sep = 0;
    while (sep < buffer_len && buffer[sep] != '\x1f') ++sep;
    uint64_t number = 0;
    if (!parse_uint(buffer, sep, &number)) return false;
    const uint32_t copy = buffer_len - sep - 1;
    if (sep >= buffer_len || copy >= kMaxValue) return false;
    for (uint32_t i = 0; i < copy; ++i) value[i] = buffer[sep + 1 + i];
    value[copy] = '\0';
    *value_len = copy;
    *lamport = number;
    return true;
}

void write_entry(const char* key, uint32_t key_len, uint64_t lamport,
                 const char* value, uint32_t value_len) {
    char buffer[kMaxValue + 32];
    uint32_t n = format_uint(lamport, buffer, sizeof(buffer));
    buffer[n++] = '\x1f';
    for (uint32_t i = 0; i < value_len && n < sizeof(buffer); ++i)
        buffer[n++] = value[i];
    store_set(key, key_len, buffer, n);
}

int compare_values(const char* left, uint32_t left_len, const char* right,
                   uint32_t right_len) {
    const uint32_t n = left_len < right_len ? left_len : right_len;
    for (uint32_t i = 0; i < n; ++i) {
        if (left[i] != right[i])
            return static_cast<uint8_t>(left[i]) <
                           static_cast<uint8_t>(right[i])
                       ? -1
                       : 1;
    }
    if (left_len == right_len) return 0;
    return left_len < right_len ? -1 : 1;
}

uint32_t count_entries() {
    uint32_t total = 0;
    store_scan("entry/", 512, [&](const char*, uint32_t, const char*,
                                  uint32_t) {
        ++total;
        return total < 512;
    });
    return total;
}

void merge_entry(const char* key, uint32_t key_len, uint64_t lamport,
                 const char* value, uint32_t value_len, Result* result) {
    char local[kMaxValue];
    uint32_t local_len = 0;
    uint64_t local_lamport = 0;
    bool found = false;
    char store_key[kMaxKey];
    const uint32_t store_key_len = make_key(key, key_len, store_key);
    if (!read_entry(store_key, store_key_len, &local_lamport, local, &local_len,
                    &found)) {
        write_entry(store_key, store_key_len, lamport, value, value_len);
        ++result->merged;
        return;
    }
    if (lamport > local_lamport) {
        write_entry(store_key, store_key_len, lamport, value, value_len);
        ++result->merged;
        return;
    }
    if (lamport < local_lamport) return;

    /* Same Lamport counter: keep the deterministic maximum and note the
     * conflict so the caller can surface it. */
    const int order = compare_values(value, value_len, local, local_len);
    if (order == 0) return;
    ++result->conflicts;
    if (order > 0) {
        write_entry(store_key, store_key_len, lamport, value, value_len);
        ++result->merged;
    }
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Result& result) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(3);
    w.key("merged");
    w.u64(result.merged);
    w.key("conflicts");
    w.u64(result.conflicts);
    w.key("total");
    w.u64(result.total);
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
    g_enabled = true;
    g_max_entries = 32;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    uint64_t max = 0;
    if (settings.uint("max_entries", &max) && max >= 1 && max <= kMaxEntries)
        g_max_entries = static_cast<uint32_t>(max);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    Result result;
    const char* type = nullptr;
    uint32_t type_len = 0;
    if (g_enabled && frame.event.text("type", &type, &type_len) &&
        text_equal(type, type_len, "personal.merge")) {
        ArrayView entries = ArrayView::of(frame.event.get("entries"));
        if (entries.valid) {
            uint32_t handled = 0;
            for (uint64_t i = 0; i < entries.count && handled < g_max_entries;
                 ++i) {
                MapView item = MapView::of(entries.at(i));
                const char* key = nullptr;
                const char* value = nullptr;
                uint32_t key_len = 0;
                uint32_t value_len = 0;
                uint64_t lamport = 0;
                if (!item.text("key", &key, &key_len)) continue;
                if (!item.text("value", &value, &value_len)) continue;
                if (!item.uint("lamport", &lamport)) continue;
                if (key_len == 0 || key_len >= kMaxKey || value_len >= kMaxValue)
                    continue;
                merge_entry(key, key_len, lamport, value, value_len, &result);
                ++handled;
            }
        }
    }
    result.total = count_entries();
    return respond(out, cap, out_len, result);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Result result;
    result.total = count_entries();
    return respond(out, cap, out_len, result);
}

PIME_EXPORT void pime_shutdown(void) {
    g_enabled = true;
    g_max_entries = 32;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
