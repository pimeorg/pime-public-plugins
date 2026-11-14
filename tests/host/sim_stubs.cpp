/*
 * Host stubs for the native plugin simulator.
 *
 * The simulator links one plugin translation unit against these functions so
 * the exact WASM entry points can be driven from a test harness. Only the
 * asset capability is functional (it reads from the plugin's assets/ dir);
 * everything else reports UNSUPPORTED, which is what the core does when a
 * capability is absent. This is test scaffolding, not a plugin runtime.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sim_stubs.h"

using namespace pimeplug;

namespace {

std::string g_asset_dir;

struct AssetEntry {
    std::string path;
    std::vector<uint8_t> data;
};

std::vector<AssetEntry> g_assets;
std::map<std::string, std::string> g_store;

uint64_t sim_now_ms() {
    const char* value = std::getenv("SIM_NOW_MS");
    return value ? std::strtoull(value, nullptr, 10) : 1767323045000ull;
}

int sim_tz_minutes() {
    const char* value = std::getenv("SIM_TZ_MIN");
    return value ? std::atoi(value) : 0;
}

bool load_asset(const std::string& path, std::vector<uint8_t>* out) {
    if (g_asset_dir.empty()) return false;
    std::ifstream file(g_asset_dir + "/" + path, std::ios::binary);
    if (!file) return false;
    out->assign(std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
    return true;
}

pime_status envelope(uint8_t* out, uint32_t cap, uint32_t* out_len,
                     pime_status status) {
    Writer w(out, cap);
    w.map(2);
    w.key("status");
    w.u64(static_cast<uint64_t>(status));
    if (status == PIME_OK) {
        w.key("result");
        w.map(0);
    } else {
        w.key("error");
        w.map(2);
        w.key("code");
        w.u64(static_cast<uint64_t>(status));
        w.key("message");
        w.text("unsupported in simulator");
    }
    if (out_len) *out_len = w.size();
    return w.ok() ? status : PIME_BUFFER_TOO_SMALL;
}

uint64_t request_handle(MapView request) {
    uint64_t handle = 0;
    request.uint("handle", &handle);
    return handle;
}

AssetEntry* entry_for(uint64_t handle) {
    if (handle == 0 || handle > g_assets.size()) return nullptr;
    return &g_assets[static_cast<size_t>(handle - 1)];
}

}  // namespace

void sim_set_asset_dir(const char* dir) { g_asset_dir = dir ? dir : ""; }

extern "C" {

pime_status pime_host_asset(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                            uint8_t* out, uint32_t cap, uint32_t* out_len) {
    if (!op) return PIME_INVALID;
    MapView request;
    if (in && in_len) {
        request = MapView::of(Span{in, in + in_len});
        if (!request.valid) return PIME_INVALID;
    }
    const uint8_t opcode = *op;
    if (opcode == 1) { /* open */
        const char* path = nullptr;
        uint32_t path_len = 0;
        if (!request.text("path", &path, &path_len)) return PIME_INVALID;
        std::vector<uint8_t> data;
        if (!load_asset(std::string(path, path_len), &data))
            return envelope(out, cap, out_len, PIME_NOT_FOUND);
        g_assets.push_back(AssetEntry{std::string(path, path_len), std::move(data)});
        Writer w(out, cap);
        w.map(2);
        w.key("status");
        w.u64(0);
        w.key("result");
        w.map(1);
        w.key("handle");
        w.u64(g_assets.size());
        if (out_len) *out_len = w.size();
        return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
    }
    if (opcode == 2) { /* size */
        AssetEntry* entry = entry_for(request_handle(request));
        if (!entry) return envelope(out, cap, out_len, PIME_NOT_FOUND);
        Writer w(out, cap);
        w.map(2);
        w.key("status");
        w.u64(0);
        w.key("result");
        w.map(1);
        w.key("size");
        w.u64(entry->data.size());
        if (out_len) *out_len = w.size();
        return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
    }
    if (opcode == 3) { /* read */
        AssetEntry* entry = entry_for(request_handle(request));
        if (!entry) return envelope(out, cap, out_len, PIME_NOT_FOUND);
        uint64_t offset = 0, length = 0;
        request.uint("offset", &offset);
        request.uint("len", &length);
        if (offset > entry->data.size()) return PIME_INVALID;
        const uint64_t available = entry->data.size() - offset;
        if (length > available) length = available;
        Writer w(out, cap);
        w.map(2);
        w.key("status");
        w.u64(0);
        w.key("result");
        w.bytes(entry->data.data() + offset, static_cast<uint32_t>(length));
        if (out_len) *out_len = w.size();
        return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
    }
    if (opcode == 5) { /* close */
        return envelope(out, cap, out_len, PIME_OK);
    }
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}

pime_status pime_host_log(uint32_t, const uint8_t*, uint32_t, const uint8_t*,
                          uint32_t) {
    return PIME_OK;
}

pime_status pime_host_workspace(const uint8_t*, const uint8_t*, uint32_t,
                                uint8_t* out, uint32_t cap, uint32_t* out_len) {
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}
pime_status pime_host_store(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                            uint8_t* out, uint32_t cap, uint32_t* out_len) {
    if (!op) return PIME_INVALID;
    MapView request;
    if (in && in_len) {
        request = MapView::of(Span{in, in + in_len});
        if (!request.valid) return PIME_INVALID;
    }
    const char* key = nullptr;
    uint32_t key_len = 0;
    const uint8_t opcode = *op;
    if (opcode == 0) { /* get */
        if (!request.text("key", &key, &key_len)) return PIME_INVALID;
        const std::string name(key, key_len);
        const auto it = g_store.find(name);
        Writer w(out, cap);
        w.map(2);
        w.key("status");
        w.u64(0);
        w.key("result");
        w.map(2);
        w.key("found");
        w.boolean(it != g_store.end());
        w.key("value");
        if (it != g_store.end())
            w.text(it->second.data(), static_cast<uint32_t>(it->second.size()));
        else
            w.text("");
        if (out_len) *out_len = w.size();
        return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
    }
    if (opcode == 1) { /* set */
        const char* value = nullptr;
        uint32_t value_len = 0;
        if (!request.text("key", &key, &key_len) ||
            !request.text("value", &value, &value_len))
            return PIME_INVALID;
        g_store[std::string(key, key_len)] = std::string(value, value_len);
        return envelope(out, cap, out_len, PIME_OK);
    }
    if (opcode == 2) { /* del */
        if (!request.text("key", &key, &key_len)) return PIME_INVALID;
        g_store.erase(std::string(key, key_len));
        return envelope(out, cap, out_len, PIME_OK);
    }
    if (opcode == 4) { /* scan */
        if (!request.text("prefix", &key, &key_len)) return PIME_INVALID;
        uint64_t limit = 1000;
        request.uint("limit", &limit);
        if (limit == 0) limit = 1000;
        const std::string prefix(key, key_len);
        std::vector<const std::pair<const std::string, std::string>*> matched;
        for (const auto& entry : g_store) {
            if (entry.first.compare(0, prefix.size(), prefix) == 0)
                matched.push_back(&entry);
        }
        if (matched.size() > limit) matched.resize(static_cast<size_t>(limit));
        Writer w(out, cap);
        w.map(2);
        w.key("status");
        w.u64(0);
        w.key("result");
        w.array(matched.size());
        for (const auto* entry : matched) {
            w.map(2);
            w.key("k");
            w.text(entry->first.data(), static_cast<uint32_t>(entry->first.size()));
            w.key("v");
            w.text(entry->second.data(),
                   static_cast<uint32_t>(entry->second.size()));
        }
        if (out_len) *out_len = w.size();
        return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
    }
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}
pime_status pime_host_config(const uint8_t*, const uint8_t*, uint32_t,
                             uint8_t* out, uint32_t cap, uint32_t* out_len) {
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}
pime_status pime_host_ctx(const uint8_t*, const uint8_t*, uint32_t, uint8_t* out,
                          uint32_t cap, uint32_t* out_len) {
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}
pime_status pime_host_channel(const uint8_t*, const uint8_t*, uint32_t,
                              uint8_t* out, uint32_t cap, uint32_t* out_len) {
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}
pime_status pime_host_sync(const uint8_t*, const uint8_t*, uint32_t, uint8_t* out,
                           uint32_t cap, uint32_t* out_len) {
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}
pime_status pime_host_shm(const uint8_t*, const uint8_t*, uint32_t, uint8_t* out,
                          uint32_t cap, uint32_t* out_len) {
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}

uint32_t pime_host_net_request(const uint8_t*, uint32_t) { return 0; }
uint32_t pime_host_net_result(uint32_t, uint8_t*, uint32_t) { return 0; }
uint32_t pime_host_net_open(const uint8_t*, const uint8_t*, uint32_t) { return 0; }
pime_status pime_host_net_send(uint32_t, const uint8_t*, uint32_t) {
    return PIME_UNSUPPORTED;
}
pime_status pime_host_net_close(uint32_t) { return PIME_UNSUPPORTED; }
pime_status pime_host_crypto(const uint8_t*, uint8_t*, uint32_t, uint32_t*) {
    return PIME_UNSUPPORTED;
}
pime_status pime_host_audio(const uint8_t*, uint8_t*, uint32_t, uint32_t*) {
    return PIME_UNSUPPORTED;
}
/* Fixed classifier output so recognize plugins can be exercised end to end:
 * two u32 label ids, 20320 (你) and 22909 (好). */
uint32_t pime_host_ml_load(const uint8_t*, uint32_t) { return 1; }
pime_status pime_host_ml_load_ex(const uint8_t*, uint32_t, uint32_t* session) {
    if (session) *session = 1;
    return PIME_OK;
}
pime_status pime_host_ml_run(uint32_t, const uint8_t*, uint32_t, uint8_t* out,
                             uint32_t cap, uint32_t* out_len) {
    const uint32_t ids[2] = {20320u, 22909u};
    uint8_t data[sizeof(ids)];
    for (uint32_t i = 0; i < 2; ++i) {
        data[i * 4] = static_cast<uint8_t>(ids[i] & 0xff);
        data[i * 4 + 1] = static_cast<uint8_t>((ids[i] >> 8) & 0xff);
        data[i * 4 + 2] = static_cast<uint8_t>((ids[i] >> 16) & 0xff);
        data[i * 4 + 3] = static_cast<uint8_t>((ids[i] >> 24) & 0xff);
    }
    Writer w(out, cap);
    w.map(3);
    w.key("dtype");
    w.text("u32");
    w.key("shape");
    w.array(2);
    w.u64(1);
    w.u64(2);
    w.key("data");
    w.bytes(data, sizeof(data));
    if (out_len) *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}
uint32_t pime_host_ml_run_async(uint32_t, const uint8_t*, uint32_t) { return 0; }
pime_status pime_host_ml_cancel(uint32_t) { return PIME_UNSUPPORTED; }
pime_status pime_host_ml_free(uint32_t) { return PIME_UNSUPPORTED; }

uint64_t pime_host_time(uint32_t kind) {
    if (kind == 1) return sim_now_ms();
    if (kind == 2)
        return static_cast<uint64_t>(static_cast<int64_t>(sim_tz_minutes()));
    return 0;
}
pime_status pime_host_random(uint8_t* out, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) out[i] = 0;
    return PIME_OK;
}
uint32_t pime_host_timer_start(uint32_t, uint32_t, const uint8_t*, uint32_t) {
    return 0;
}
pime_status pime_host_timer_cancel(uint32_t) { return PIME_UNSUPPORTED; }
pime_status pime_host_caps(uint8_t* out, uint32_t cap, uint32_t* out_len) {
    return envelope(out, cap, out_len, PIME_UNSUPPORTED);
}

pime_status pime_host_thread_create(const uint8_t*, uint32_t, uint32_t*) {
    return PIME_UNSUPPORTED;
}
pime_status pime_host_thread_post(uint32_t, const uint8_t*, uint32_t) {
    return PIME_UNSUPPORTED;
}
pime_status pime_host_thread_recv(uint32_t, uint8_t*, uint32_t, uint32_t*,
                                  uint32_t) {
    return PIME_UNSUPPORTED;
}
pime_status pime_host_thread_park(uint32_t, uint32_t) { return PIME_UNSUPPORTED; }
pime_status pime_host_thread_unpark(uint32_t) { return PIME_UNSUPPORTED; }
pime_status pime_host_thread_close(uint32_t) { return PIME_UNSUPPORTED; }
pime_status pime_host_thread_join(uint32_t, uint32_t) { return PIME_UNSUPPORTED; }
pime_status pime_host_thread_info(uint32_t, uint8_t*, uint32_t, uint32_t*) {
    return PIME_UNSUPPORTED;
}
pime_status pime_host_emit(uint64_t, const uint8_t*, uint32_t) {
    return PIME_UNSUPPORTED;
}

}  // extern "C"
