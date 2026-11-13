/*
 * Reader for the plugin dictionary text format:
 *
 *   # comment
 *   <key>\t<text>\t<frequency>
 *
 * One entry per line, UTF-8, tab separated. The data is kept as a borrowed
 * view over a buffer the plugin owns; nothing is allocated here. Lookups are
 * linear, which is adequate for the starter dictionaries in data/; larger
 * dictionaries should ship a pre-indexed binary form.
 */
#ifndef PIMEPLUG_DICT_HPP
#define PIMEPLUG_DICT_HPP

#include <stdint.h>

#include "cbor.hpp"

namespace pimeplug {

struct DictEntry {
    const char* key = nullptr;
    uint32_t key_len = 0;
    const char* text = nullptr;
    uint32_t text_len = 0;
    uint32_t freq = 0;
    bool valid() const { return key && text && text_len > 0; }
};

inline bool dict_parse_freq(const char* s, uint32_t n, uint32_t* out) {
    if (!s || n == 0) return false;
    uint32_t value = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') return false;
        value = value * 10u + static_cast<uint32_t>(c - '0');
    }
    *out = value;
    return true;
}

class Dict {
public:
    bool load(const char* data, uint32_t len) {
        data_ = data;
        len_ = len;
        return data != nullptr && len > 0;
    }
    bool loaded() const { return data_ && len_; }

    /* Visits every well-formed entry. */
    template <class F>
    void each(F&& fn) const {
        uint32_t i = 0;
        while (i < len_) {
            const char* line = data_ + i;
            uint32_t n = 0;
            while (i + n < len_ && data_[i + n] != '\n' && data_[i + n] != '\r')
                ++n;
            i += n;
            while (i < len_ && (data_[i] == '\n' || data_[i] == '\r')) ++i;
            if (n == 0 || line[0] == '#') continue;

            DictEntry entry;
            uint32_t field = 0;
            uint32_t start = 0;
            for (uint32_t j = 0; j <= n; ++j) {
                const bool sep = (j == n) || (line[j] == '\t');
                if (!sep) continue;
                const char* part = line + start;
                const uint32_t part_len = j - start;
                if (field == 0) {
                    entry.key = part;
                    entry.key_len = part_len;
                } else if (field == 1) {
                    entry.text = part;
                    entry.text_len = part_len;
                } else if (field == 2) {
                    dict_parse_freq(part, part_len, &entry.freq);
                    break;
                }
                ++field;
                start = j + 1;
            }
            if (entry.valid()) fn(entry);
        }
    }

    /* Visits entries whose key equals `key`. */
    template <class F>
    void match(const char* key, uint32_t key_len, F&& fn) const {
        each([&](const DictEntry& e) {
            if (e.key_len == key_len &&
                Frame::equal(e.key, e.key_len, key, key_len))
                fn(e);
        });
    }

    /* Visits entries whose key starts with `prefix`. */
    template <class F>
    void prefix(const char* prefix, uint32_t prefix_len, F&& fn) const {
        each([&](const DictEntry& e) {
            if (e.key_len >= prefix_len &&
                Frame::equal(e.key, prefix_len, prefix, prefix_len))
                fn(e);
        });
    }

private:
    const char* data_ = nullptr;
    uint32_t len_ = 0;
};

} /* namespace pimeplug */

#endif /* PIMEPLUG_DICT_HPP */
