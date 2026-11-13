/*
 * Helpers for the `ml.infer` capability ABI used by recognize plugins.
 *
 * Tensor shape (shared with core): { "dtype": str, "shape": [u32; 4],
 * "data": bytes }. Only u8 inputs and u32 outputs are used by the starter
 * models; larger models can pass any dtype through the same map, these helpers
 * just cover the two shapes the plugins actually build and read.
 */
#ifndef PIMEPLUG_ML_HPP
#define PIMEPLUG_ML_HPP

#include "cbor.hpp"

namespace pimeplug {

/* Writes { dtype: "u8", shape: [a,b,c,d], data: bytes }. Returns the encoded
 * size (may exceed `cap`; check `*ok`). */
inline uint32_t ml_encode_u8(const uint8_t* data, uint32_t data_len,
                             uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3,
                             uint8_t* buf, uint32_t cap, bool* ok) {
    Writer w(buf, cap);
    w.map(3);
    w.key("dtype");
    w.text("u8");
    w.key("shape");
    w.array(4);
    w.u64(d0);
    w.u64(d1);
    w.u64(d2);
    w.u64(d3);
    w.key("data");
    w.bytes(data, data_len);
    *ok = w.ok();
    return w.size();
}

/* Reads a tensor map's `data` as little-endian u32 ids. */
inline bool ml_ids(Span tensor, uint32_t* ids, uint32_t max, uint32_t* count) {
    MapView map = MapView::of(tensor);
    if (!map.valid) return false;
    const uint8_t* data = nullptr;
    uint32_t len = 0;
    if (!span_bytes(map.get("data"), &data, &len)) return false;
    uint32_t n = len / 4;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) {
        ids[i] = static_cast<uint32_t>(data[i * 4]) |
                 (static_cast<uint32_t>(data[i * 4 + 1]) << 8) |
                 (static_cast<uint32_t>(data[i * 4 + 2]) << 16) |
                 (static_cast<uint32_t>(data[i * 4 + 3]) << 24);
    }
    *count = n;
    return true;
}

}  // namespace pimeplug

#endif /* PIMEPLUG_ML_HPP */
