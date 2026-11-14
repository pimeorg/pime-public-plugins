/*
 * Capability wrappers used by the stateful plugins: KV store, wall clock and
 * logging. Shapes follow pime-core/schema/cbor/host.cddl; the envelope is
 * { status: uint, result: ... } / { status: uint, error: ... }.
 */
#ifndef PIMEPLUG_HOST_HPP
#define PIMEPLUG_HOST_HPP

#include <pime/plugin_api.h>

#include "cbor.hpp"

namespace pimeplug {

namespace host_detail {

enum : uint8_t {
    kStoreGet = 0,
    kStoreSet = 1,
    kStoreDel = 2,
    kStoreBatch = 3,
    kStoreScan = 4,
    kStoreQuota = 5,
    kStoreFlush = 6,
};

inline bool envelope_ok(const uint8_t* data, uint32_t len, Span* result) {
    Frame frame;
    if (!frame.parse(data, len)) return false;
    uint64_t status = 1;
    if (!frame.root.uint("status", &status)) return false;
    if (status != 0) return false;
    if (result) *result = frame.root.get("result");
    return true;
}

inline bool call(uint8_t op, const uint8_t* in, uint32_t in_len, uint8_t* out,
                 uint32_t cap, uint32_t* out_len) {
    uint32_t written = 0;
    if (pime_host_store(&op, in, in_len, out, cap, &written) != PIME_OK)
        return false;
    if (out_len) *out_len = written;
    return true;
}

}  // namespace host_detail

/* Reads a text value. Returns false when absent (found=false) or on error. */
inline bool store_get(const char* key, char* out, uint32_t cap,
                      uint32_t* out_len, bool* found = nullptr) {
    uint8_t request[256];
    Writer w(request, sizeof(request));
    w.map(1);
    w.key("key");
    w.text(key);
    if (!w.ok()) return false;

    uint8_t response[1024];
    uint32_t response_len = 0;
    if (!host_detail::call(host_detail::kStoreGet, request, w.size(), response,
                           sizeof(response), &response_len))
        return false;
    Span result;
    if (!host_detail::envelope_ok(response, response_len, &result)) return false;
    MapView map = MapView::of(result);
    bool has = false;
    if (!map.boolean("found", &has)) return false;
    if (found) *found = has;
    if (!has) {
        if (out_len) *out_len = 0;
        return false;
    }
    const char* value = nullptr;
    uint32_t len = 0;
    if (!map.text("value", &value, &len)) return false;
    if (len > cap) len = cap;
    for (uint32_t i = 0; i < len; ++i) out[i] = value[i];
    if (out_len) *out_len = len;
    return true;
}

inline bool store_set(const char* key, const char* value, uint32_t len) {
    uint8_t request[1024];
    Writer w(request, sizeof(request));
    w.map(2);
    w.key("key");
    w.text(key);
    w.key("value");
    w.text(value, len);
    if (!w.ok()) return false;
    uint8_t response[256];
    uint32_t response_len = 0;
    return host_detail::call(host_detail::kStoreSet, request, w.size(), response,
                             sizeof(response), &response_len);
}

inline bool store_del(const char* key) {
    uint8_t request[256];
    Writer w(request, sizeof(request));
    w.map(1);
    w.key("key");
    w.text(key);
    if (!w.ok()) return false;
    uint8_t response[256];
    uint32_t response_len = 0;
    return host_detail::call(host_detail::kStoreDel, request, w.size(), response,
                             sizeof(response), &response_len);
}

/* Visits {k, v} pairs under `prefix`. The callback returns false to stop. */
template <class F>
void store_scan(const char* prefix, uint32_t limit, F&& fn) {
    uint8_t request[256];
    Writer w(request, sizeof(request));
    w.map(2);
    w.key("prefix");
    w.text(prefix);
    w.key("limit");
    w.u64(limit);
    if (!w.ok()) return;
    uint8_t response[16384];
    uint32_t response_len = 0;
    if (!host_detail::call(host_detail::kStoreScan, request, w.size(), response,
                           sizeof(response), &response_len))
        return;
    Span result;
    if (!host_detail::envelope_ok(response, response_len, &result)) return;
    ArrayView array = ArrayView::of(result);
    if (!array.valid) return;
    for (uint64_t i = 0; i < array.count; ++i) {
        MapView item = MapView::of(array.at(i));
        const char* key = nullptr;
        const char* value = nullptr;
        uint32_t key_len = 0;
        uint32_t value_len = 0;
        if (!item.text("k", &key, &key_len)) continue;
        if (!item.text("v", &value, &value_len)) continue;
        if (!fn(key, key_len, value, value_len)) return;
    }
}

/* kind: 0 monotonic ms, 1 wall ms, 2 timezone offset minutes. */
inline uint64_t host_time(uint32_t kind) { return pime_host_time(kind); }

inline void host_log(const char* tag, const char* message, uint32_t level = 3) {
    pime_host_log(level, reinterpret_cast<const uint8_t*>(tag), cstr_len(tag),
                  reinterpret_cast<const uint8_t*>(message), cstr_len(message));
}

}  // namespace pimeplug

#endif /* PIMEPLUG_HOST_HPP */
