/*
 * Minimal definite-length CBOR (RFC 8949) reader/writer for pime plugins.
 *
 * Header-only, freestanding: no STL, no exceptions, no allocation. Every
 * accessor takes a value span produced by the core frame; nothing here owns
 * memory.
 */
#ifndef PIMEPLUG_CBOR_HPP
#define PIMEPLUG_CBOR_HPP

#include <stddef.h>
#include <stdint.h>

namespace pimeplug {

inline uint32_t cstr_len(const char* text) {
    uint32_t n = 0;
    while (text && text[n]) ++n;
    return n;
}

/* One complete encoded CBOR value, head included. */
struct Span {
    const uint8_t* p = nullptr;
    const uint8_t* e = nullptr;

    bool valid() const { return p && e && p < e; }
    uint32_t size() const { return valid() ? static_cast<uint32_t>(e - p) : 0; }
    uint8_t major() const { return valid() ? static_cast<uint8_t>(p[0] >> 5) : 0xff; }
};

inline bool read_head(const uint8_t** p, const uint8_t* e, uint8_t* major,
                      uint64_t* arg) {
    if (!p || !*p || *p >= e) return false;
    const uint8_t b = *(*p)++;
    const uint8_t info = (uint8_t)(b & 0x1f);
    *major = (uint8_t)(b >> 5);
    if (info < 24) {
        *arg = info;
        return true;
    }
    uint32_t extra = 0;
    switch (info) {
        case 24: extra = 1; break;
        case 25: extra = 2; break;
        case 26: extra = 4; break;
        case 27: extra = 8; break;
        default: return false; /* indefinite length is not accepted */
    }
    if (static_cast<size_t>(e - *p) < extra) return false;
    uint64_t value = 0;
    for (uint32_t i = 0; i < extra; ++i) value = (value << 8) | (*p)[i];
    *p += extra;
    *arg = value;
    return true;
}

inline bool skip_value(const uint8_t** p, const uint8_t* e) {
    const uint8_t* q = *p;
    uint8_t major = 0;
    uint64_t arg = 0;
    if (!read_head(&q, e, &major, &arg)) return false;
    switch (major) {
        case 0:  /* uint */
        case 1:  /* negint */
        case 7:  /* simple / bool / null */
            *p = q;
            return true;
        case 2:  /* bytes */
        case 3:  /* text */
            if (arg > static_cast<uint64_t>(e - q)) return false;
            *p = q + arg;
            return true;
        case 4: { /* array */
            for (uint64_t i = 0; i < arg; ++i)
                if (!skip_value(&q, e)) return false;
            *p = q;
            return true;
        }
        case 5: { /* map */
            for (uint64_t i = 0; i < arg; ++i) {
                if (!skip_value(&q, e)) return false;
                if (!skip_value(&q, e)) return false;
            }
            *p = q;
            return true;
        }
        case 6: /* tag */
            return skip_value(&q, e) ? (*p = q, true) : false;
        default:
            return false;
    }
}

inline bool value_span(const uint8_t* p, const uint8_t* e, Span* out) {
    if (!p || p >= e) return false;
    const uint8_t* q = p;
    if (!skip_value(&q, e)) return false;
    if (out) {
        out->p = p;
        out->e = q;
    }
    return true;
}

/* Reads a text/bytes payload. */
inline bool span_text(Span s, const char** out, uint32_t* len) {
    if (!s.valid() || s.major() != 3) return false;
    const uint8_t* p = s.p;
    uint8_t major = 0;
    uint64_t n = 0;
    if (!read_head(&p, s.e, &major, &n)) return false;
    if (n > static_cast<uint64_t>(s.e - p)) return false;
    if (out) *out = reinterpret_cast<const char*>(p);
    if (len) *len = static_cast<uint32_t>(n);
    return true;
}

/* Reads a CBOR byte-string payload. */
inline bool span_bytes(Span s, const uint8_t** out, uint32_t* len) {
    if (!s.valid() || s.major() != 2) return false;
    const uint8_t* p = s.p;
    uint8_t major = 0;
    uint64_t n = 0;
    if (!read_head(&p, s.e, &major, &n)) return false;
    if (n > static_cast<uint64_t>(s.e - p)) return false;
    if (out) *out = p;
    if (len) *len = static_cast<uint32_t>(n);
    return true;
}

inline bool span_uint(Span s, uint64_t* out) {    if (!s.valid() || s.major() != 0) return false;
    const uint8_t* p = s.p;
    uint8_t major = 0;
    uint64_t n = 0;
    if (!read_head(&p, s.e, &major, &n)) return false;
    if (out) *out = n;
    return true;
}

inline bool span_bool(Span s, bool* out) {
    if (!s.valid() || s.major() != 7) return false;
    const uint8_t* p = s.p;
    uint8_t major = 0;
    uint64_t n = 0;
    if (!read_head(&p, s.e, &major, &n)) return false;
    if (n != 20 && n != 21) return false;
    if (out) *out = (n == 21);
    return true;
}

struct MapView {
    const uint8_t* body = nullptr;
    const uint8_t* end = nullptr;
    uint64_t count = 0;
    bool valid = false;

    static MapView of(Span s) {
        MapView m;
        if (!s.valid() || s.major() != 5) return m;
        const uint8_t* p = s.p;
        uint8_t major = 0;
        uint64_t n = 0;
        if (!read_head(&p, s.e, &major, &n)) return m;
        m.body = p;
        m.end = s.e;
        m.count = n;
        m.valid = true;
        return m;
    }

    Span get(const char* key, uint32_t key_len) const {
        Span none;
        if (!valid) return none;
        const uint8_t* p = body;
        for (uint64_t i = 0; i < count; ++i) {
            const uint8_t* key_start = p;
            uint8_t kmajor = 0;
            uint64_t klen = 0;
            if (!read_head(&p, end, &kmajor, &klen)) return none;
            if (kmajor != 3 || klen > static_cast<uint64_t>(end - p)) return none;
            const uint8_t* value_start = p + klen;
            bool match = (klen == key_len);
            for (uint32_t j = 0; match && j < key_len; ++j)
                if (p[j] != static_cast<uint8_t>(key[j])) match = false;
            p = value_start;
            Span value;
            if (!value_span(p, end, &value)) return none;
            if (match) return value;
            p = value.e;
            (void)key_start;
        }
        return none;
    }

    Span get(const char* key) const { return get(key, cstr_len(key)); }

    bool text(const char* key, const char** out, uint32_t* len) const {
        return span_text(get(key), out, len);
    }
    bool uint(const char* key, uint64_t* out) const {
        return span_uint(get(key), out);
    }
    bool boolean(const char* key, bool* out) const {
        return span_bool(get(key), out);
    }
};

struct ArrayView {
    const uint8_t* body = nullptr;
    const uint8_t* end = nullptr;
    uint64_t count = 0;
    bool valid = false;

    static ArrayView of(Span s) {
        ArrayView a;
        if (!s.valid() || s.major() != 4) return a;
        const uint8_t* p = s.p;
        uint8_t major = 0;
        uint64_t n = 0;
        if (!read_head(&p, s.e, &major, &n)) return a;
        a.body = p;
        a.end = s.e;
        a.count = n;
        a.valid = true;
        return a;
    }

    Span at(uint64_t index) const {
        Span none;
        if (!valid || index >= count) return none;
        const uint8_t* p = body;
        for (uint64_t i = 0; i < index; ++i)
            if (!skip_value(&p, end)) return none;
        Span value;
        if (!value_span(p, end, &value)) return none;
        return value;
    }

    /* Walks the array in one pass. `at()` rescans from the head for every
     * index, which turns a loop over the whole array quadratic; candidate
     * lists can hold hundreds of entries, so hot loops use this instead. */
    struct Cursor {
        const uint8_t* p = nullptr;
        const uint8_t* end = nullptr;
        uint64_t left = 0;

        bool next(Span* out) {
            if (left == 0) return false;
            Span value;
            if (!value_span(p, end, &value)) return false;
            p = value.e;
            --left;
            if (out) *out = value;
            return true;
        }

        MapView map() {
            Span value;
            if (!next(&value)) return MapView{};
            return MapView::of(value);
        }
    };

    Cursor cursor() const { return Cursor{body, end, valid ? count : 0}; }
};

/* Fixed-capacity writer. Overflow is sticky and reported by ok(). */
class Writer {
public:
    Writer(uint8_t* buf, uint32_t cap) : buf_(buf), cap_(cap) {}

    bool ok() const { return ok_; }
    uint32_t size() const { return len_; }
    uint8_t* data() { return buf_; }
    const uint8_t* data() const { return buf_; }
    uint32_t capacity() const { return cap_; }

    bool fits(uint32_t extra) const {
        return ok_ && static_cast<uint64_t>(len_) + extra <= cap_;
    }

    void map(uint64_t n) { head(5, n); }
    void array(uint64_t n) { head(4, n); }
    void text(const char* s) { text(s, cstr_len(s)); }
    void text(const char* s, uint32_t n) {
        if (!s && n) return fail();
        head(3, n);
        for (uint32_t i = 0; i < n; ++i) put_raw(static_cast<uint8_t>(s[i]));
    }
    void bytes(const uint8_t* d, uint32_t n) {
        head(2, n);
        for (uint32_t i = 0; i < n; ++i) put_raw(d[i]);
    }
    void key(const char* k) { text(k); }
    void key(const char* k, uint32_t n) { text(k, n); }
    void u64(uint64_t v) { head(0, v); }
    void i64(int64_t v) {
        if (v >= 0) {
            head(0, static_cast<uint64_t>(v));
        } else {
            head(1, static_cast<uint64_t>(-(v + 1)));
        }
    }
    void boolean(bool b) { head(7, b ? 21 : 20); }
    void null() { head(7, 22); }
    void raw(const uint8_t* d, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) put_raw(d[i]);
    }

private:
    void fail() { ok_ = false; }

    void put_raw(uint8_t b) {
        if (len_ >= cap_) {
            ok_ = false;
            ++len_;
            return;
        }
        buf_[len_++] = b;
    }

    void head(uint8_t major, uint64_t v) {
        const uint8_t m = static_cast<uint8_t>(major << 5);
        if (v < 24) {
            put_raw(static_cast<uint8_t>(m | v));
        } else if (v <= 0xff) {
            put_raw(static_cast<uint8_t>(m | 24));
            put_raw(static_cast<uint8_t>(v));
        } else if (v <= 0xffff) {
            put_raw(static_cast<uint8_t>(m | 25));
            put_raw(static_cast<uint8_t>(v >> 8));
            put_raw(static_cast<uint8_t>(v));
        } else if (v <= 0xffffffffull) {
            put_raw(static_cast<uint8_t>(m | 26));
            for (int shift = 24; shift >= 0; shift -= 8)
                put_raw(static_cast<uint8_t>(v >> shift));
        } else {
            put_raw(static_cast<uint8_t>(m | 27));
            for (int shift = 56; shift >= 0; shift -= 8)
                put_raw(static_cast<uint8_t>(v >> shift));
        }
    }

    uint8_t* buf_;
    uint32_t cap_;
    uint32_t len_ = 0;
    bool ok_ = true;
};

/* Common frame accessors shared by all plugins. */
struct Frame {
    MapView root;
    MapView event;
    MapView context;
    MapView config;
    MapView settings;
    MapView workspace;

    bool parse(const uint8_t* data, uint32_t len) {
        Span whole{data, data + len};
        root = MapView::of(whole);
        if (!root.valid) return false;
        event = MapView::of(root.get("event"));
        context = MapView::of(root.get("context"));
        config = MapView::of(root.get("config"));
        settings = MapView::of(config.get("settings"));
        workspace = MapView::of(root.get("workspace"));
        return true;
    }

    bool is_event(const char* type) const {
        const char* value = nullptr;
        uint32_t len = 0;
        return event.text("type", &value, &len) && len == cstr_len(type) &&
               equal(value, len, type, len);
    }

    bool event_text(const char** out, uint32_t* len) const {
        return event.text("text", out, len);
    }

    bool setting_bool(const char* key, bool* out) const {
        return settings.boolean(key, out);
    }
    bool setting_uint(const char* key, uint64_t* out) const {
        return settings.uint(key, out);
    }

    /* Reads a workspace value by full key, e.g. plugin/x/y. */
    Span workspace_value(const char* key) const { return workspace.get(key); }

    static bool equal(const char* a, uint32_t an, const char* b, uint32_t bn) {
        if (an != bn) return false;
        for (uint32_t i = 0; i < an; ++i)
            if (a[i] != b[i]) return false;
        return true;
    }
};

} /* namespace pimeplug */

#endif /* PIMEPLUG_CBOR_HPP */
