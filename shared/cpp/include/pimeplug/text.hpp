/*
 * Fixed-capacity text helpers for synchronous plugins: no allocation, no STL.
 */
#ifndef PIMEPLUG_TEXT_HPP
#define PIMEPLUG_TEXT_HPP

#include <stdint.h>

#include "cbor.hpp"
#include "utf8.hpp"

namespace pimeplug {

inline bool ascii_lower(char c) { return c >= 'a' && c <= 'z'; }
inline bool ascii_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
inline bool ascii_digit(char c) { return c >= '0' && c <= '9'; }

inline bool text_equal(const char* a, uint32_t an, const char* b) {
    return Frame::equal(a, an, b, cstr_len(b));
}

inline bool text_ends_with(const char* text, uint32_t len, const char* suffix) {
    const uint32_t n = cstr_len(suffix);
    if (!text || len < n) return false;
    return Frame::equal(text + (len - n), n, suffix, n);
}

/* A byte buffer with sticky overflow. */
struct Buffer {
    char* data;
    uint32_t cap;
    uint32_t len = 0;
    bool overflow = false;

    Buffer(char* d, uint32_t c) : data(d), cap(c) {}

    void clear() { len = 0; }
    bool empty() const { return len == 0; }
    void append(char c) {
        if (len >= cap) {
            overflow = true;
            return;
        }
        data[len++] = c;
    }
    void append(const char* text, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) append(text[i]);
    }
    void append(const char* text) { append(text, cstr_len(text)); }
    void append_cp(uint32_t cp) {
        char tmp[4];
        const uint32_t n = utf8_encode(cp, tmp);
        append(tmp, n);
    }
    void pop() {
        if (len > 0) --len;
    }
    void remove_last_codepoint() {
        if (len == 0) return;
        uint32_t i = len;
        while (i > 0 && (static_cast<uint8_t>(data[i - 1]) & 0xc0) == 0x80) --i;
        if (i > 0) --i;
        len = i;
    }
    void assign(const char* text, uint32_t n) {
        len = 0;
        append(text, n);
    }
    bool equals(const char* text) const {
        return text_equal(data, len, text);
    }
};

/* Parses a decimal uint; false on empty or non-digit. */
inline bool parse_uint(const char* s, uint32_t n, uint64_t* out) {
    if (!s || n == 0 || n > 20) return false;
    uint64_t value = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (!ascii_digit(s[i])) return false;
        value = value * 10 + static_cast<uint64_t>(s[i] - '0');
    }
    *out = value;
    return true;
}

inline uint32_t format_uint(uint64_t value, char* out, uint32_t cap) {
    char tmp[24];
    uint32_t n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value && n < sizeof(tmp));
    if (n > cap) return 0;
    for (uint32_t i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    return n;
}

inline uint32_t format_i64(int64_t value, char* out, uint32_t cap) {
    if (value < 0) {
        if (cap < 2) return 0;
        out[0] = '-';
        const uint32_t n = format_uint(static_cast<uint64_t>(-(value + 1)) + 1,
                                       out + 1, cap - 1);
        return n ? n + 1 : 0;
    }
    return format_uint(static_cast<uint64_t>(value), out, cap);
}

/* Fixed-point formatting for calculator results: up to 10 fractional digits,
 * trailing zeros removed. */
inline uint32_t format_fixed(double value, char* out, uint32_t cap,
                             uint32_t decimals = 10) {
    if (!(value == value)) return 0;  /* NaN */
    bool negative = false;
    if (value < 0) {
        negative = true;
        value = -value;
    }
    if (value > 1e15) return 0;
    uint64_t scale = 1;
    for (uint32_t i = 0; i < decimals; ++i) scale *= 10;
    const double scaled = value * static_cast<double>(scale) + 0.5;
    if (scaled > 1.8e19) return 0;
    uint64_t fixed = static_cast<uint64_t>(scaled);
    uint64_t integer = fixed / scale;
    uint64_t fraction = fixed % scale;

    char digits[64];
    uint32_t n = 0;
    if (negative && (integer || fraction)) digits[n++] = '-';
    char whole[24];
    const uint32_t wn = format_uint(integer, whole, sizeof(whole));
    for (uint32_t i = 0; i < wn; ++i) digits[n++] = whole[i];
    if (fraction) {
        char frac[16];
        uint32_t fn = decimals;
        while (fn > 0) {
            frac[fn - 1] = static_cast<char>('0' + fraction % 10);
            fraction /= 10;
            --fn;
        }
        uint32_t keep = decimals;
        while (keep > 0 && frac[keep - 1] == '0') --keep;
        if (n + 1 + keep > sizeof(digits)) return 0;
        digits[n++] = '.';
        for (uint32_t i = 0; i < keep; ++i) digits[n++] = frac[i];
    }
    if (n > cap) return 0;
    for (uint32_t i = 0; i < n; ++i) out[i] = digits[i];
    return n;
}

/* Days since 1970-01-01 from a civil date (Howard Hinnant's algorithm). */
inline int64_t days_from_civil(int y, uint32_t m, uint32_t d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const uint32_t yoe = static_cast<uint32_t>(y - era * 400);
    const uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline void civil_from_days(int64_t z, int* y, uint32_t* m, uint32_t* d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = static_cast<int>(yy + (*m <= 2));
}

/* Splits a wall-clock millisecond value into local civil fields. */
struct CivilTime {
    int year = 1970;
    uint32_t month = 1;
    uint32_t day = 1;
    uint32_t hour = 0;
    uint32_t minute = 0;
    uint32_t second = 0;
    uint32_t weekday = 4;  /* 0 = Sunday; 1970-01-01 was Thursday */
    int64_t local_ms = 0;
};

inline CivilTime civil_time(uint64_t wall_ms, int offset_minutes) {
    CivilTime out;
    const int64_t local = static_cast<int64_t>(wall_ms) +
                          static_cast<int64_t>(offset_minutes) * 60000;
    out.local_ms = local;
    int64_t days = local / 86400000;
    int64_t rest = local % 86400000;
    if (rest < 0) {
        rest += 86400000;
        --days;
    }
    civil_from_days(days, &out.year, &out.month, &out.day);
    out.hour = static_cast<uint32_t>(rest / 3600000);
    out.minute = static_cast<uint32_t>((rest / 60000) % 60);
    out.second = static_cast<uint32_t>((rest / 1000) % 60);
    int64_t wd = (days + 4) % 7;
    if (wd < 0) wd += 7;
    out.weekday = static_cast<uint32_t>(wd);
    return out;
}

/* utf8 byte length of the first `chars` codepoints. */
inline uint32_t utf8_prefix_bytes(const char* text, uint32_t len,
                                  uint32_t chars) {
    uint32_t i = 0;
    uint32_t seen = 0;
    while (i < len && seen < chars) {
        uint32_t used = 0;
        utf8_decode(text + i, len - i, &used);
        i += used ? used : 1;
        ++seen;
    }
    return i;
}

/* Byte offset of the last `chars` codepoints. */
inline uint32_t utf8_suffix_offset(const char* text, uint32_t len,
                                   uint32_t chars) {
    uint32_t i = len;
    uint32_t seen = 0;
    while (i > 0 && seen < chars) {
        --i;
        while (i > 0 && (static_cast<uint8_t>(text[i]) & 0xc0) == 0x80) --i;
        ++seen;
    }
    return i;
}

}  // namespace pimeplug

#endif /* PIMEPLUG_TEXT_HPP */
