/*
 * Workspace helpers.
 *
 * Input workspace keys are full keys ("plugin/<alias>/<key>"); output writes
 * are short keys and the core prefixes them with the plugin alias.
 */
#ifndef PIMEPLUG_WORKSPACE_HPP
#define PIMEPLUG_WORKSPACE_HPP

#include "cbor.hpp"

namespace pimeplug {

/* Builds a "workspace" map for the output frame. The entry count must match
 * the number of write calls; this is a fixed-frame writer, not a container. */
class WorkspaceWrites {
public:
    WorkspaceWrites(Writer& w, uint32_t entries) : w_(w) { w_.map(entries); }

    void text(const char* key, const char* value, uint32_t value_len) {
        w_.key(key);
        w_.text(value, value_len);
    }
    void text(const char* key, const char* value) {
        text(key, value, cstr_len(value));
    }
    void u64(const char* key, uint64_t value) {
        w_.key(key);
        w_.u64(value);
    }
    void boolean(const char* key, bool value) {
        w_.key(key);
        w_.boolean(value);
    }
    /* Opens an array value; caller writes exactly `count` items. */
    void array(const char* key, uint32_t count) {
        w_.key(key);
        w_.array(count);
    }

    Writer& writer() { return w_; }
    bool ok() const { return w_.ok(); }

private:
    Writer& w_;
};

/* Full-key helpers so consumers do not spell the prefix by hand. */
struct WorkspaceKey {
    char text[128];
    uint32_t len = 0;
    bool valid = false;
};

inline WorkspaceKey full_key(const char* plugin_id, const char* key) {
    WorkspaceKey out;
    const char prefix[] = "plugin/";
    uint32_t n = 0;
    for (uint32_t i = 0; prefix[i]; ++i) out.text[n++] = prefix[i];
    for (uint32_t i = 0; plugin_id[i] && n < sizeof(out.text) - 1; ++i)
        out.text[n++] = plugin_id[i];
    if (n < sizeof(out.text) - 1) out.text[n++] = '/';
    for (uint32_t i = 0; key[i] && n < sizeof(out.text) - 1; ++i)
        out.text[n++] = key[i];
    out.text[n] = '\0';
    out.len = n;
    out.valid = n < sizeof(out.text);
    return out;
}

} /* namespace pimeplug */

#endif /* PIMEPLUG_WORKSPACE_HPP */
