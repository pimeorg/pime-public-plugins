/*
 * T9 / 14-key key grouping shared by pime-t9-parse and pime-t9-translate.
 *
 * Nine:    digits 2-9 hold the phone letters (abc, def, ...), `1` separates
 *          syllables.
 * Fourteen: keys A-N hold two adjacent letters each (qw er ty ui op / as df
 *          gh jk l / zx cv bn m), typed as lowercase a-n.
 *
 * Both use lowercase codes; `1` is only meaningful for the nine grouping.
 */
#ifndef PIMEPLUG_T9_HPP
#define PIMEPLUG_T9_HPP

#include <stdint.h>

#include "cbor.hpp"

namespace pimeplug {

enum T9Grouping {
    kT9Nine = 0,
    kT9Fourteen = 1,
};

inline const char* t9_fourteen_group(uint32_t index, uint32_t* len) {
    static const char kGroups[14][3] = {"qw", "er", "ty", "ui", "op", "as",
                                        "df", "gh", "jk", "l",  "zx", "cv",
                                        "bn", "m"};
    if (index >= 14) {
        if (len) *len = 0;
        return "";
    }
    if (len) *len = kGroups[index][1] ? 2u : 1u;
    return kGroups[index];
}

inline bool t9_grouping_from_text(const char* text, uint32_t len,
                                  T9Grouping* out) {
    if (!text) return false;
    if (len == 4 && Frame::equal(text, 4, "nine", 4)) {
        *out = kT9Nine;
        return true;
    }
    if (len == 8 && Frame::equal(text, 8, "fourteen", 8)) {
        *out = kT9Fourteen;
        return true;
    }
    return false;
}

/* Letter → key code, lowercase. Returns 0 for a non-letter. */
inline char t9_letter_code(T9Grouping grouping, char c) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c < 'a' || c > 'z') return 0;
    if (grouping == kT9Nine) {
        if (c <= 'c') return '2';
        if (c <= 'f') return '3';
        if (c <= 'i') return '4';
        if (c <= 'l') return '5';
        if (c <= 'o') return '6';
        if (c <= 's') return '7';
        if (c <= 'v') return '8';
        return '9';
    }
    for (uint32_t i = 0; i < 14; ++i) {
        uint32_t n = 0;
        const char* group = t9_fourteen_group(i, &n);
        for (uint32_t k = 0; k < n; ++k)
            if (group[k] == c) return static_cast<char>('a' + i);
    }
    return 0;
}

/* Whether `c` may be typed as a key code in this grouping. */
inline bool t9_is_code(T9Grouping grouping, char c) {
    if (grouping == kT9Nine) return c >= '2' && c <= '9';
    return c >= 'a' && c <= 'n';
}

/* Codes for a lowercase syllable ("zhong" → "94664"). Returns 0 on bad input. */
inline uint32_t t9_encode(T9Grouping grouping, const char* text, uint32_t len,
                          char* out) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; ++i) {
        const char code = t9_letter_code(grouping, text[i]);
        if (!code) return 0;
        out[n++] = code;
    }
    return n;
}

}  // namespace pimeplug

#endif /* PIMEPLUG_T9_HPP */
