/*
 * pime-model-manager: model catalog service for handwriting / speech / LM.
 *
 * WeaveText parity (core/weave-engine/models/catalog.json): a catalog of model
 * entries with engine, path and declared hash, plus per-model enable state and
 * verification status.
 *
 * The plugin never loads weights: the core verifies the signature and hash when
 * a plugin actually asks for `ml.infer`; this service tracks the catalog and
 * records whether the hash supplied by the host matches the declared one.
 *
 * Events:
 *   { type: "model.enable", id, enabled }      toggle one entry
 *   { type: "model.verify", id, hash }         compare a host-computed hash
 *   any other event                            report the catalog
 *
 * Workspace out: models (array of {id, engine, hash, enabled, status}),
 *                count, changed, status.
 */
#include <pime/plugin_api.h>

#include <pimeplug/memory.hpp>

#include <pimeplug/asset.hpp>
#include <pimeplug/cbor.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 8192;
constexpr uint32_t kMaxModels = 16;
constexpr uint32_t kMaxId = 32;
constexpr uint32_t kMaxEngine = 24;
constexpr uint32_t kMaxPath = 96;
constexpr uint32_t kMaxHash = 80;
constexpr uint32_t kMaxAsset = 8192;

alignas(8) uint8_t g_scratch[kScratchBytes];
alignas(8) uint8_t g_asset[kMaxAsset];

uint32_t g_asset_len = 0;
bool g_loaded = false;
bool g_enabled = true;
char g_disabled[128] = {0};
uint32_t g_disabled_len = 0;

struct Model {
    char id[kMaxId];
    uint32_t id_len = 0;
    char engine[kMaxEngine];
    uint32_t engine_len = 0;
    char path[kMaxPath];
    uint32_t path_len = 0;
    char hash[kMaxHash];
    uint32_t hash_len = 0;
    bool enabled = false;
    bool verified = false;
    bool mismatch = false;
};

void copy_field(char* out, uint32_t cap, uint32_t* out_len, const char* text,
                uint32_t len) {
    if (len >= cap) len = cap - 1;
    for (uint32_t i = 0; i < len; ++i) out[i] = text[i];
    out[len] = '\0';
    *out_len = len;
}

uint32_t load_models(Model* models) {
    if (!g_loaded) {
        g_loaded = true;
        AssetHandle handle;
        if (asset_open("models.tsv", &handle)) {
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
    while (i < g_asset_len && count < kMaxModels) {
        const char* line = data + i;
        uint32_t n = 0;
        while (i + n < g_asset_len && data[i + n] != '\n' && data[i + n] != '\r')
            ++n;
        i += n;
        while (i < g_asset_len && (data[i] == '\n' || data[i] == '\r')) ++i;
        if (n == 0 || line[0] == '#') continue;
        Model model;
        uint32_t field = 0;
        uint32_t start = 0;
        for (uint32_t j = 0; j <= n; ++j) {
            const bool sep = (j == n) || (line[j] == '\t');
            if (!sep) continue;
            const uint32_t len = j - start;
            if (field == 0)
                copy_field(model.id, kMaxId, &model.id_len, line + start, len);
            else if (field == 1)
                copy_field(model.engine, kMaxEngine, &model.engine_len,
                           line + start, len);
            else if (field == 2)
                copy_field(model.path, kMaxPath, &model.path_len, line + start,
                           len);
            else if (field == 3)
                copy_field(model.hash, kMaxHash, &model.hash_len, line + start,
                           len);
            else if (field == 4)
                model.enabled = len == 1 && line[start] == '1';
            ++field;
            start = j + 1;
        }
        if (model.id_len == 0) continue;
        models[count++] = model;
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

uint32_t store_key(const char* prefix, const char* id, uint32_t id_len,
                   char* out) {
    uint32_t n = 0;
    for (const char* p = prefix; *p; ++p) out[n++] = *p;
    for (uint32_t i = 0; i < id_len && n < kMaxId + 16; ++i) out[n++] = id[i];
    out[n] = '\0';
    return n;
}

void load_state(Model* model) {
    if (in_list(g_disabled, g_disabled_len, model->id, model->id_len)) {
        model->enabled = false;
        return;
    }
    char key[kMaxId + 16];
    const uint32_t key_len = store_key("model/", model->id, model->id_len, key);
    char value[8];
    uint32_t value_len = 0;
    bool found = false;
    if (store_get(key, key_len, value, sizeof(value), &value_len, &found) &&
        found && value_len == 1)
        model->enabled = value[0] == '1';

    const uint32_t status_len =
        store_key("status/", model->id, model->id_len, key);
    if (store_get(key, status_len, value, sizeof(value), &value_len, &found) &&
        found) {
        model->verified = true;
        model->mismatch =
            value_len == 8 && Frame::equal(value, 8, "mismatch", 8);
    }
}

void save_state(const Model& model) {
    char key[kMaxId + 16];
    uint32_t key_len = store_key("model/", model.id, model.id_len, key);
    const char on[] = "1";
    const char off[] = "0";
    store_set(key, key_len, model.enabled ? on : off, 1);
    if (model.verified) {
        key_len = store_key("status/", model.id, model.id_len, key);
        const char ok[] = "ok";
        const char bad[] = "mismatch";
        store_set(key, key_len, model.mismatch ? bad : ok,
                  model.mismatch ? 8 : 2);
    }
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Model* models, uint32_t count, const char* changed,
                    uint32_t changed_len, const char* status,
                    uint32_t status_len) {
    uint32_t enabled = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (models[i].enabled) ++enabled;

    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(4);
    w.key("models");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        w.map(6);
        w.key("id");
        w.text(models[i].id, models[i].id_len);
        w.key("engine");
        w.text(models[i].engine, models[i].engine_len);
        w.key("hash");
        w.text(models[i].hash, models[i].hash_len);
        w.key("enabled");
        w.boolean(models[i].enabled);
        w.key("status");
        if (!models[i].verified)
            w.text("declared");
        else if (models[i].mismatch)
            w.text("mismatch");
        else
            w.text("ok");
        w.key("path");
        w.text(models[i].path, models[i].path_len);
    }
    w.key("count");
    w.u64(enabled);
    w.key("changed");
    w.text(changed, changed_len);
    w.key("status");
    w.text(status, status_len);
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

    Model models[kMaxModels];
    const uint32_t count = load_models(models);
    for (uint32_t i = 0; i < count; ++i) load_state(&models[i]);

    const char* changed = nullptr;
    uint32_t changed_len = 0;
    const char* status = "";
    uint32_t status_len = 0;

    const char* type = nullptr;
    uint32_t type_len = 0;
    if (g_enabled && frame.event.text("type", &type, &type_len)) {
        const char* id = nullptr;
        uint32_t id_len = 0;
        if ((text_equal(type, type_len, "model.enable") ||
             text_equal(type, type_len, "model.verify")) &&
            frame.event.text("id", &id, &id_len)) {
            for (uint32_t i = 0; i < count; ++i) {
                if (models[i].id_len != id_len ||
                    !Frame::equal(models[i].id, models[i].id_len, id, id_len))
                    continue;
                changed = id;
                changed_len = id_len;
                if (text_equal(type, type_len, "model.enable")) {
                    bool enabled = true;
                    frame.event.boolean("enabled", &enabled);
                    models[i].enabled = enabled;
                    status = enabled ? "enabled" : "disabled";
                    status_len = enabled ? 7 : 8;
                } else {
                    const char* hash = nullptr;
                    uint32_t hash_len = 0;
                    if (frame.event.text("hash", &hash, &hash_len)) {
                        models[i].verified = true;
                        models[i].mismatch =
                            hash_len != models[i].hash_len ||
                            !Frame::equal(hash, hash_len, models[i].hash,
                                          hash_len);
                        status = models[i].mismatch ? "mismatch" : "ok";
                        status_len = models[i].mismatch ? 8 : 2;
                    }
                }
                save_state(models[i]);
                break;
            }
        }
    }

    return respond(out, cap, out_len, models, count, changed, changed_len,
                   status, status_len);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Model models[kMaxModels];
    const uint32_t count = load_models(models);
    for (uint32_t i = 0; i < count; ++i) load_state(&models[i]);
    return respond(out, cap, out_len, models, count, nullptr, 0, "", 0);
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
