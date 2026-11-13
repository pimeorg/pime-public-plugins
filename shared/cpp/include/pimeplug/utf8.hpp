#ifndef PIMEPLUG_UTF8_HPP
#define PIMEPLUG_UTF8_HPP

#include <stdint.h>

namespace pimeplug {

/* Encoded length of a Unicode scalar value (1..4 bytes). */
inline uint32_t utf8_len(uint32_t cp) {
    if (cp < 0x80) return 1;
    if (cp < 0x800) return 2;
    if (cp < 0x10000) return 3;
    return 4;
}

/* Decodes one scalar value; returns 0 on malformed input and sets nothing. */
inline uint32_t utf8_decode(const char* s, uint32_t n, uint32_t* consumed) {
    if (!s || n == 0) return 0;
    const uint8_t b0 = static_cast<uint8_t>(s[0]);
    if (b0 < 0x80) {
        if (consumed) *consumed = 1;
        return b0;
    }
    uint32_t need = 0;
    uint32_t cp = 0;
    if ((b0 & 0xe0) == 0xc0) { need = 1; cp = b0 & 0x1f; }
    else if ((b0 & 0xf0) == 0xe0) { need = 2; cp = b0 & 0x0f; }
    else if ((b0 & 0xf8) == 0xf0) { need = 3; cp = b0 & 0x07; }
    else return 0;
    if (n < need + 1) return 0;
    for (uint32_t i = 1; i <= need; ++i) {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        if ((b & 0xc0) != 0x80) return 0;
        cp = (cp << 6) | (b & 0x3f);
    }
    if (consumed) *consumed = need + 1;
    return cp;
}

/* Encodes one scalar value; returns bytes written (0 on invalid input). */
inline uint32_t utf8_encode(uint32_t cp, char* out) {
    const uint32_t n = utf8_len(cp);
    if (!out) return 0;
    if (n == 1) {
        out[0] = static_cast<char>(cp);
    } else if (n == 2) {
        out[0] = static_cast<char>(0xc0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3f));
    } else if (n == 3) {
        out[0] = static_cast<char>(0xe0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out[2] = static_cast<char>(0x80 | (cp & 0x3f));
    } else {
        out[0] = static_cast<char>(0xf0 | (cp >> 18));
        out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out[3] = static_cast<char>(0x80 | (cp & 0x3f));
    }
    return n;
}

/* Counts Unicode scalar values. */
inline uint32_t utf8_count(const char* s, uint32_t n) {
    uint32_t count = 0;
    uint32_t i = 0;
    while (i < n) {
        uint32_t used = 0;
        const uint32_t cp = utf8_decode(s + i, n - i, &used);
        if (used == 0) {
            ++i;
            ++count;
            continue;
        }
        i += used;
        if (cp != 0) ++count;
    }
    return count;
}

} /* namespace pimeplug */

#endif /* PIMEPLUG_UTF8_HPP */
