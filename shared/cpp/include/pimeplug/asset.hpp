/*
 * asset.read capability wrapper (WASM host functions).
 *
 * Envelope returned by the core:
 *   { status: uint, result: <value> }         on success
 *   { status: uint, error: {code, message} }  on failure
 *
 * Asset read returns the payload as a byte string, so the caller buffer must
 * be large enough for the envelope; the payload is moved to the front.
 */
#ifndef PIMEPLUG_ASSET_HPP
#define PIMEPLUG_ASSET_HPP

#include <pime/plugin_api.h>

#include "cbor.hpp"

namespace pimeplug {

namespace detail {

enum : uint8_t {
    kAssetList = 0,
    kAssetOpen = 1,
    kAssetSize = 2,
    kAssetRead = 3,
    kAssetMap = 4,
    kAssetClose = 5,
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

inline bool asset_call(uint8_t op, const uint8_t* in, uint32_t in_len,
                       uint8_t* out, uint32_t cap, uint32_t* out_len) {
    uint32_t written = 0;
    if (pime_host_asset(&op, in, in_len, out, cap, &written) != PIME_OK)
        return false;
    if (out_len) *out_len = written;
    return true;
}

} /* namespace detail */

struct AssetHandle {
    /* Handles are (generation << 32) | index; truncating to 32 bits makes the
     * core report the handle as stale. */
    uint64_t id = 0;
    bool valid() const { return id != 0; }
};

inline bool asset_open(const char* path, AssetHandle* out) {
    uint8_t request[512];
    Writer w(request, sizeof(request));
    w.map(1);
    w.key("path");
    w.text(path);
    if (!w.ok()) return false;

    uint8_t response[512];
    uint32_t response_len = 0;
    if (!detail::asset_call(detail::kAssetOpen, request, w.size(), response,
                            sizeof(response), &response_len))
        return false;
    Span result;
    if (!detail::envelope_ok(response, response_len, &result)) return false;
    MapView map = MapView::of(result);
    uint64_t handle = 0;
    if (!map.uint("handle", &handle) || handle == 0) return false;
    if (out) out->id = handle;
    return true;
}

inline bool asset_size(AssetHandle handle, uint32_t* size) {
    uint8_t request[128];
    Writer w(request, sizeof(request));
    w.map(1);
    w.key("handle");
    w.u64(handle.id);
    if (!w.ok()) return false;

    uint8_t response[256];
    uint32_t response_len = 0;
    if (!detail::asset_call(detail::kAssetSize, request, w.size(), response,
                            sizeof(response), &response_len))
        return false;
    Span result;
    if (!detail::envelope_ok(response, response_len, &result)) return false;
    MapView map = MapView::of(result);
    uint64_t value = 0;
    if (!map.uint("size", &value)) return false;
    if (size) *size = static_cast<uint32_t>(value);
    return true;
}

/* Reads `len` bytes at `offset` into `out`. `cap` must hold the envelope and
 * the payload (the payload is moved to the front before returning). */
inline bool asset_read(AssetHandle handle, uint32_t offset, uint32_t len,
                       uint8_t* out, uint32_t cap, uint32_t* out_len) {
    uint8_t request[196];
    Writer w(request, sizeof(request));
    w.map(3);
    w.key("handle");
    w.u64(handle.id);
    w.key("offset");
    w.u64(offset);
    w.key("len");
    w.u64(len);
    if (!w.ok()) return false;

    uint32_t response_len = 0;
    if (!detail::asset_call(detail::kAssetRead, request, w.size(), out, cap,
                            &response_len))
        return false;
    Span result;
    if (!detail::envelope_ok(out, response_len, &result)) return false;
    if (result.major() != 2) return false;
    const uint8_t* p = result.p;
    uint8_t major = 0;
    uint64_t n = 0;
    if (!read_head(&p, result.e, &major, &n)) return false;
    if (n > static_cast<uint64_t>(result.e - p)) return false;
    for (uint64_t i = 0; i < n; ++i) out[i] = p[i];
    if (out_len) *out_len = static_cast<uint32_t>(n);
    return true;
}

inline void asset_close(AssetHandle handle) {
    if (!handle.valid()) return;
    uint8_t request[128];
    Writer w(request, sizeof(request));
    w.map(1);
    w.key("handle");
    w.u64(handle.id);
    if (!w.ok()) return;
    uint8_t response[128];
    uint32_t response_len = 0;
    detail::asset_call(detail::kAssetClose, request, w.size(), response,
                       sizeof(response), &response_len);
}

} /* namespace pimeplug */

#endif /* PIMEPLUG_ASSET_HPP */
