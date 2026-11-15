/*
 * pime-dict-packs: professional dictionary pack enable/disable service.
 *
 * WeaveText parity (core/weave-engine/dictpacks.json): 14 packs that can be
 * switched on demand. Defaults come from the shipped asset; per-pack state
 * lives in the plugin KV so it survives restarts.
 *
 * Events:
 *   { type: "dictpack", id: "medical", enabled: true|false }
 *       toggles one pack and reports it in `changed` / `enabled`.
 *   any other event
 *       only reports the current state.
 *
 * Workspace out: active (text[] of enabled pack ids), count, changed, enabled.
 */
#include <pime/plugin_api.h>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 8192;
constexpr uint32_t kMaxPacks = 32;
constexpr uint32_t kMaxId = 32;
constexpr uint32_t kMaxAsset = 4096;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_asset[kMaxAsset];

uint32_t g_asset_len = 0;
bool g_loaded = false;
bool g_enabled = true;
char g_disabled[128] = {0};
uint32_t g_disabled_len = 0;

struct Pack {
    char id[kMaxId];
    uint32_t id_len = 0;
    bool enabled = false;
};

/* Splits the asset into pack rows. */
uint32_t load_packs(Pack* packs) {
    if (!g_loaded) {
        g_loaded = true;
        AssetHandle handle;
        if (asset_open("packs.tsv", &handle)) {
            uint32_t size = 0;
            bool ok = asset_size(handle, &size) && size > 0 &&
                      size + 64 <= sizeof(g_asset);
            uint32_t got = 0;
            if (ok)
                ok = asset_read(handle, 0, size, g_asset, sizeof(g_asset), &got) &&
                     got > 0;
            asset_close(handle);
            if (ok) g_asset_len = got;
        }
    }
    if (g_asset_len == 0) return 0;

    const char* data = reinterpret_cast<const char*>(g_asset);
    uint32_t count = 0;
    uint32_t i = 0;
    while (i < g_asset_len && count < kMaxPacks) {
        const char* line = data + i;
        uint32_t n = 0;
        while (i + n < g_asset_len && data[i + n] != '\n' && data[i + n] != '\r')
            ++n;
        i += n;
        while (i < g_asset_len && (data[i] == '\n' || data[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        uint32_t field = 0;
        uint32_t start = 0;
        Pack pack;
        for (uint32_t j = 0; j <= n; ++j) {
            const bool sep = (j == n) || (line[j] == '\t');
            if (!sep) continue;
            const uint32_t len = j - start;
            if (field == 0) {
                if (len == 0 || len >= kMaxId) break;
                for (uint32_t k = 0; k < len; ++k) pack.id[k] = line[start + k];
                pack.id[len] = '\0';
                pack.id_len = len;
            } else if (field == 2) {
                pack.enabled = len == 1 && line[start] == '1';
            }
            ++field;
            start = j + 1;
        }
        if (pack.id_len == 0) continue;
        packs[count++] = pack;
    }
    return count;
}

bool in_list(const char* list, uint32_t list_len, const char* id,
             uint32_t id_len) {
    uint32_t i = 0;
    while (i < list_len) {
        uint32_t end = i;
        while (end < list_len && list[end] != ',') ++end;
        if (end - i == id_len && Frame::equal(list + i, end - i, id, id_len))
            return true;
        i = end + 1;
    }
    return false;
}

bool pack_state(const Pack& pack) {
    if (in_list(g_disabled, g_disabled_len, pack.id, pack.id_len)) return false;
    char key[kMaxId + 8];
    uint32_t key_len = 0;
    const char prefix[] = "pack/";
    for (const char* p = prefix; *p; ++p) key[key_len++] = *p;
    for (uint32_t i = 0; i < pack.id_len; ++i) key[key_len++] = pack.id[i];
    char value[4];
    uint32_t value_len = 0;
    bool found = false;
    if (store_get(key, key_len, value, sizeof(value), &value_len, &found) &&
        found && value_len == 1)
        return value[0] == '1';
    return pack.enabled;
}

void pack_set(const Pack& pack, bool enabled) {
    char key[kMaxId + 8];
    uint32_t key_len = 0;
    const char prefix[] = "pack/";
    for (const char* p = prefix; *p; ++p) key[key_len++] = *p;
    for (uint32_t i = 0; i < pack.id_len; ++i) key[key_len++] = pack.id[i];
    const char on[] = "1";
    const char off[] = "0";
    store_set(key, key_len, enabled ? on : off, 1);
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Pack* packs, uint32_t count, const char* changed,
                    uint32_t changed_len, bool changed_state) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(4);
    w.key("active");
    uint32_t enabled = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (packs[i].enabled) ++enabled;
    w.array(enabled);
    for (uint32_t i = 0; i < count; ++i) {
        if (!packs[i].enabled) continue;
        w.text(packs[i].id, packs[i].id_len);
    }
    w.key("count");
    w.u64(enabled);
    w.key("changed");
    w.text(changed, changed_len);
    w.key("enabled");
    w.boolean(changed_state);
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
    g_disabled_len = 0;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    const char* disabled = nullptr;
    uint32_t disabled_len = 0;
    if (settings.text("disabled", &disabled, &disabled_len)) {
        if (disabled_len >= sizeof(g_disabled)) disabled_len = sizeof(g_disabled) - 1;
        for (uint32_t i = 0; i < disabled_len; ++i) g_disabled[i] = disabled[i];
        g_disabled[disabled_len] = '\0';
        g_disabled_len = disabled_len;
    }
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    Pack packs[kMaxPacks];
    const uint32_t count = load_packs(packs);
    for (uint32_t i = 0; i < count; ++i) packs[i].enabled = pack_state(packs[i]);

    const char* changed = nullptr;
    uint32_t changed_len = 0;
    bool changed_state = false;

    const char* type = nullptr;
    uint32_t type_len = 0;
    if (g_enabled && frame.event.text("type", &type, &type_len) &&
        text_equal(type, type_len, "dictpack")) {
        const char* id = nullptr;
        uint32_t id_len = 0;
        bool enabled = true;
        if (frame.event.text("id", &id, &id_len) &&
            frame.event.boolean("enabled", &enabled) && id_len < kMaxId) {
            for (uint32_t i = 0; i < count; ++i) {
                if (packs[i].id_len != id_len ||
                    !Frame::equal(packs[i].id, packs[i].id_len, id, id_len))
                    continue;
                pack_set(packs[i], enabled);
                packs[i].enabled = enabled;
                changed = id;
                changed_len = id_len;
                changed_state = enabled;
                break;
            }
        }
    }

    return respond(out, cap, out_len, packs, count, changed, changed_len,
                   changed_state);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Pack packs[kMaxPacks];
    const uint32_t count = load_packs(packs);
    for (uint32_t i = 0; i < count; ++i) packs[i].enabled = pack_state(packs[i]);
    return respond(out, cap, out_len, packs, count, nullptr, 0, false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_loaded = false;
    g_asset_len = 0;
    g_enabled = true;
    g_disabled_len = 0;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
