/*
 * pime-speech-sherpa: offline streaming speech recognition through the
 * sherpa-onnx C API.
 *
 * This is the native counterpart of pime-speech-ncnn.  It mirrors what
 * WeaveText does on Android: the host captures 16 kHz mono PCM, the plugin
 * feeds it to a streaming Zipformer2-CTC model (or any sherpa-onnx online
 * model), and partial text goes back as a composition while the final text is
 * committed when the host stops recording.
 *
 * Events (host -> plugin, owner thread):
 *   voice { action: 0 start | 1 stop | 2 cancel }
 *
 * Host services used from the worker thread:
 *   audio.capture  start / stop / read (16 kHz mono s16le)
 *   emit           composition updates (pime_host_emit)
 *
 * The runtime libraries and model weights are host assets: `runtime_dir` is
 * the app's native library directory (libsherpa-onnx-c-api.so and
 * libonnxruntime.so) and `model_dir` holds `model.int8.onnx` + `tokens.txt`.
 */
#include <pime/native_plugin.h>

#include <pimeplug/cbor.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "c-api.h"

using namespace pimeplug;

namespace {

/* HostKind::Audio and AudioOp values from pime-core's host ABI. */
constexpr uint32_t kHostKindAudio = 10;
constexpr uint32_t kAudioCaptureStart = 0;
constexpr uint32_t kAudioCaptureStop = 1;
constexpr uint32_t kAudioCaptureRead = 2;

/* HostKind::Log; the request carries level/tag/message. */
constexpr uint32_t kHostKindLog = 16;
constexpr uint32_t kLogDebug = 1;
constexpr uint32_t kLogInfo = 2;

constexpr uint32_t kSampleRate = 16000;
constexpr uint32_t kFrameBytes = 3200; /* 100 ms s16le mono */

struct State {
    const pime_native_host* host = nullptr;
    std::string runtime_dir;
    std::string model_dir;
    /* Developer hook: when set, the worker replays this 16 kHz mono wav
     * instead of asking the host for microphone audio. */
    std::string wav_file;
    std::vector<uint8_t> wav_pcm;
    size_t wav_pos = 0;
    bool wav_mode = false;
    int32_t threads = 2;
    int32_t sample_rate = 16000;

#if defined(_WIN32)
    HMODULE lib_ort = nullptr;
    HMODULE lib_sherpa = nullptr;
#else
    void* lib_ort = nullptr;
    void* lib_sherpa = nullptr;
#endif

    /* sherpa-onnx entry points, resolved once. */
    decltype(&SherpaOnnxCreateOnlineRecognizer) create_online = nullptr;
    decltype(&SherpaOnnxDestroyOnlineRecognizer) destroy_online = nullptr;
    decltype(&SherpaOnnxCreateOnlineStream) create_stream = nullptr;
    decltype(&SherpaOnnxDestroyOnlineStream) destroy_stream = nullptr;
    decltype(&SherpaOnnxOnlineStreamAcceptWaveform) accept = nullptr;
    decltype(&SherpaOnnxOnlineStreamInputFinished) finish_input = nullptr;
    decltype(&SherpaOnnxOnlineStreamReset) reset_stream = nullptr;
    decltype(&SherpaOnnxOnlineStreamIsEndpoint) is_endpoint = nullptr;
    decltype(&SherpaOnnxIsOnlineStreamReady) is_ready = nullptr;
    decltype(&SherpaOnnxDecodeOnlineStream) decode = nullptr;
    decltype(&SherpaOnnxGetOnlineStreamResult) get_result = nullptr;
    decltype(&SherpaOnnxDestroyOnlineRecognizerResult) destroy_result = nullptr;

    const SherpaOnnxOnlineRecognizer* recognizer = nullptr;
    const SherpaOnnxOnlineStream* stream = nullptr;
    uint64_t session = 0;

    std::thread worker;
    std::atomic<bool> stopping{false};
    std::mutex mutex;
    std::string final_text;
    std::string shown;
    bool active = false;
    bool capturing = false;

    ~State() { release(); }

    void release() {
        stopping.store(true);
        if (worker.joinable()) worker.join();
        std::lock_guard<std::mutex> lock(mutex);
        destroy_streams();
        if (recognizer && destroy_online) destroy_online(recognizer);
        recognizer = nullptr;
#if defined(_WIN32)
        if (lib_sherpa) FreeLibrary(lib_sherpa);
        if (lib_ort) FreeLibrary(lib_ort);
#else
        if (lib_sherpa) dlclose(lib_sherpa);
        if (lib_ort) dlclose(lib_ort);
#endif
        lib_sherpa = nullptr;
        lib_ort = nullptr;
    }

    void destroy_streams() {
        if (stream && destroy_stream) destroy_stream(stream);
        stream = nullptr;
    }
};

State g_state;

void plugin_log(State& state, uint32_t level, const std::string& message);

std::string join_path(const std::string& dir, const char* name) {
    if (dir.empty()) return std::string(name);
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

#if defined(_WIN32)
void* open_library(const std::string& path) {
    return reinterpret_cast<void*>(LoadLibraryA(path.c_str()));
}
void* library_symbol(void* lib, const char* name) {
    return reinterpret_cast<void*>(
            GetProcAddress(reinterpret_cast<HMODULE>(lib), name));
}
#else
void* open_library(const std::string& path) {
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}
void* library_symbol(void* lib, const char* name) {
    return dlsym(lib, name);
}
#endif

uint32_t le32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t le16(const uint8_t* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8));
}

/* Reads the PCM payload of a 16 kHz mono 16-bit RIFF wav. */
bool load_wav(const std::string& path, std::vector<uint8_t>* pcm,
              std::string* error) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        *error = "cannot open " + path;
        return false;
    }
    std::vector<uint8_t> file_data;
    uint8_t buffer[8192];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        file_data.insert(file_data.end(), buffer, buffer + got);
    std::fclose(file);
    if (file_data.size() < 44 ||
        std::memcmp(file_data.data(), "RIFF", 4) != 0 ||
        std::memcmp(file_data.data() + 8, "WAVE", 4) != 0) {
        *error = "not a wav file";
        return false;
    }
    uint32_t rate = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    size_t data_offset = 0;
    size_t data_size = 0;
    size_t pos = 12;
    while (pos + 8 <= file_data.size()) {
        const uint8_t* chunk = file_data.data() + pos;
        const uint32_t size = le32(chunk + 4);
        const size_t body = pos + 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16 &&
            body + 16 <= file_data.size()) {
            channels = le16(file_data.data() + body + 2);
            rate = le32(file_data.data() + body + 4);
            bits = le16(file_data.data() + body + 14);
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data_offset = body;
            data_size = size;
            if (data_offset + data_size > file_data.size())
                data_size = file_data.size() - data_offset;
        }
        pos = body + size + (size & 1u);
    }
    if (rate != kSampleRate || channels != 1 || bits != 16 ||
        data_size < 2) {
        *error = "need 16 kHz mono 16-bit wav";
        return false;
    }
    pcm->assign(file_data.begin() + static_cast<long>(data_offset),
                file_data.begin() + static_cast<long>(data_offset + data_size));
    return true;
}

/* Tries every plausible location for one runtime library. */
void* open_runtime(const std::string& dir, const char* name) {
    std::vector<std::string> candidates;
    if (!dir.empty()) candidates.push_back(join_path(dir, name));
    candidates.emplace_back(name);
    for (const auto& path : candidates) {
        if (void* lib = open_library(path)) return lib;
    }
    return nullptr;
}

bool load_runtime(State& state) {
    if (state.lib_sherpa) return true;
    if (state.runtime_dir.empty()) return false;
    /* onnxruntime first, globally: libsherpa-onnx-c-api.so depends on it by
     * soname, and the app's native library directory is not searched for a
     * plugin binary that lives under the app's files directory. */
#if defined(_WIN32)
    state.lib_ort = reinterpret_cast<HMODULE>(
            open_runtime(state.runtime_dir, "onnxruntime.dll"));
#else
    state.lib_ort = open_runtime(state.runtime_dir, "libonnxruntime.so");
    if (!state.lib_ort)
        state.lib_ort = dlopen("libonnxruntime.so", RTLD_NOW | RTLD_GLOBAL);
#endif
    if (!state.lib_ort) return false;
#if defined(_WIN32)
    state.lib_sherpa = reinterpret_cast<HMODULE>(
            open_runtime(state.runtime_dir, "sherpa-onnx-c-api.dll"));
#else
    state.lib_sherpa = open_runtime(state.runtime_dir, "libsherpa-onnx-c-api.so");
#endif
    if (!state.lib_sherpa) return false;

#define RESOLVE(field, symbol)                                      \
    state.field = reinterpret_cast<decltype(state.field)>(          \
            library_symbol(state.lib_sherpa, symbol));              \
    if (!state.field) return false
    RESOLVE(create_online, "SherpaOnnxCreateOnlineRecognizer");
    RESOLVE(destroy_online, "SherpaOnnxDestroyOnlineRecognizer");
    RESOLVE(create_stream, "SherpaOnnxCreateOnlineStream");
    RESOLVE(destroy_stream, "SherpaOnnxDestroyOnlineStream");
    RESOLVE(accept, "SherpaOnnxOnlineStreamAcceptWaveform");
    RESOLVE(finish_input, "SherpaOnnxOnlineStreamInputFinished");
    RESOLVE(reset_stream, "SherpaOnnxOnlineStreamReset");
    RESOLVE(is_endpoint, "SherpaOnnxOnlineStreamIsEndpoint");
    RESOLVE(is_ready, "SherpaOnnxIsOnlineStreamReady");
    RESOLVE(decode, "SherpaOnnxDecodeOnlineStream");
    RESOLVE(get_result, "SherpaOnnxGetOnlineStreamResult");
    RESOLVE(destroy_result, "SherpaOnnxDestroyOnlineRecognizerResult");
#undef RESOLVE
    return true;
}

bool ensure_recognizer(State& state) {
    if (state.recognizer) return true;
    if (!load_runtime(state)) {
        plugin_log(state, kLogInfo,
                   "sherpa runtime not loaded from " + state.runtime_dir);
        return false;
    }
    if (state.model_dir.empty()) {
        plugin_log(state, kLogInfo, "model_dir is empty");
        return false;
    }

    const std::string model = join_path(state.model_dir, "model.int8.onnx");
    const std::string tokens = join_path(state.model_dir, "tokens.txt");

    SherpaOnnxOnlineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = static_cast<int32_t>(kSampleRate);
    config.feat_config.feature_dim = 80;
    config.model_config.zipformer2_ctc.model = model.c_str();
    config.model_config.tokens = tokens.c_str();
    config.model_config.num_threads = state.threads;
    config.model_config.provider = "cpu";
    config.decoding_method = "greedy_search";
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = 2.4f;
    config.rule2_min_trailing_silence = 1.2f;
    config.rule3_min_utterance_length = 20.0f;

    state.recognizer = state.create_online(&config);
    if (!state.recognizer) {
        plugin_log(state, kLogInfo, "recognizer failed: " + model);
        return false;
    }
    plugin_log(state, kLogInfo, "recognizer ready: " + model);
    return true;
}

/* --------------------------------------------------------------- responses */

pime_status write_empty(uint8_t* out, uint32_t cap, uint32_t* out_len,
                        bool handled) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(0);
    w.key("handled");
    w.boolean(handled);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

pime_status write_commands(uint8_t* out, uint32_t cap, uint32_t* out_len,
                           const std::string& text, bool commit) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(0);
    w.key("handled");
    w.boolean(true);
    w.key("commands");
    w.array(1);
    if (!commit) {
        w.map(3);
        w.key("type");
        w.text("set_composition");
        w.key("text");
        w.text(text.data(), static_cast<uint32_t>(text.size()));
        w.key("cursor");
        w.u64(text.size());
        return w.ok() ? (*out_len = w.size(), PIME_OK)
                      : PIME_BUFFER_TOO_SMALL;
    }
    w.map(2);
    w.key("type");
    w.text("commit");
    w.key("text");
    w.text(text.data(), static_cast<uint32_t>(text.size()));
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

/* Sends one composition update from the worker thread. */
void emit_partial(State& state, const std::string& text) {
    if (!state.host || !state.host->emit || state.session == 0) return;
    uint8_t out[4096];
    Writer w(out, sizeof(out));
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(0);
    w.key("handled");
    w.boolean(true);
    w.key("commands");
    w.array(1);
    w.map(3);
    w.key("type");
    w.text("set_composition");
    w.key("text");
    w.text(text.data(), static_cast<uint32_t>(text.size()));
    w.key("cursor");
    w.u64(text.size());
    if (!w.ok()) return;
    state.host->emit(state.host->ud, state.session, out, w.size());
}

/* ------------------------------------------------------------- diagnostics */

void plugin_log(State& state, uint32_t level, const std::string& message) {
    if (!state.host || !state.host->invoke) return;
    uint8_t request[512];
    Writer w(request, sizeof(request));
    w.map(3);
    w.key("level");
    w.u64(level);
    w.key("tag");
    w.text("speech-sherpa");
    w.key("message");
    w.text(message.data(), static_cast<uint32_t>(message.size()));
    if (!w.ok()) return;
    uint8_t response[64];
    uint32_t response_len = 0;
    state.host->invoke(state.host->ud, kHostKindLog, 0, request, w.size(),
                       response, sizeof(response), &response_len);
}

/* ------------------------------------------------------------- audio bridge */

pime_status host_audio(State& state, uint32_t op, const uint8_t* in,
                       uint32_t in_len, uint8_t* out, uint32_t cap,
                       uint32_t* out_len) {
    if (!state.host || !state.host->invoke) return PIME_UNSUPPORTED;
    return static_cast<pime_status>(state.host->invoke(
            state.host->ud, kHostKindAudio, op, in, in_len, out, cap, out_len));
}

bool capture_start(State& state) {
    uint8_t request[8];
    Writer w(request, sizeof(request));
    w.map(0);
    uint8_t response[256];
    uint32_t response_len = 0;
    const pime_status status = host_audio(state, kAudioCaptureStart, request,
                                          w.size(), response, sizeof(response),
                                          &response_len);
    if (status != PIME_OK) {
        plugin_log(state, kLogInfo,
                   "capture start failed: " + std::to_string(status));
        return false;
    }
    state.capturing = true;
    return true;
}

void capture_stop(State& state) {
    if (!state.capturing) return;
    uint8_t response[256];
    uint32_t response_len = 0;
    host_audio(state, kAudioCaptureStop, nullptr, 0, response,
               sizeof(response), &response_len);
    state.capturing = false;
}

/* Reads one PCM block. Returns false when capture is gone or empty. */
bool capture_read(State& state, std::vector<uint8_t>* pcm) {
    uint8_t request[32];
    Writer w(request, sizeof(request));
    w.map(1);
    w.key("timeout_ms");
    w.u64(200);
    uint8_t response[16384];
    uint32_t response_len = 0;
    const pime_status status = host_audio(state, kAudioCaptureRead, request,
                                          w.size(), response, sizeof(response),
                                          &response_len);
    if (status != PIME_OK) {
        static uint32_t failures = 0;
        if (failures++ < 3)
            plugin_log(state, kLogDebug,
                       "capture read: " + std::to_string(status));
        return false;
    }
    /* Host replies are { status, result } envelopes (see pimeplug/host.hpp);
     * the PCM block itself is inside "result". */
    MapView envelope = MapView::of(Span{response, response + response_len});
    uint64_t reply = 1;
    if (!envelope.valid || !envelope.uint("status", &reply) || reply != 0)
        return false;
    MapView result = MapView::of(envelope.get("result"));
    const uint8_t* data = nullptr;
    uint32_t len = 0;
    if (!span_bytes(result.get("data"), &data, &len) || len < 2) return false;
    pcm->assign(data, data + len);
    return true;
}

/* --------------------------------------------------------------- worker */

void run_worker() {
    State& state = g_state;
    std::vector<uint8_t> pcm;
    pcm.reserve(kFrameBytes * 2);
    std::vector<float> samples;
    samples.reserve(16000);
    uint32_t frames = 0;
    uint64_t energy = 0;
    uint64_t samples_seen = 0;

    while (!state.stopping.load()) {
        pcm.clear();
        if (state.wav_mode) {
            if (state.wav_pos >= state.wav_pcm.size()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            const size_t take =
                    std::min<size_t>(kFrameBytes, state.wav_pcm.size() -
                                                          state.wav_pos);
            pcm.assign(state.wav_pcm.begin() +
                               static_cast<long>(state.wav_pos),
                       state.wav_pcm.begin() +
                               static_cast<long>(state.wav_pos + take));
            state.wav_pos += take;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else if (!capture_read(state, &pcm)) {
            continue;
        }
        const uint32_t count = static_cast<uint32_t>(pcm.size() / 2);
        samples.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            const int16_t value = static_cast<int16_t>(
                    static_cast<uint16_t>(pcm[i * 2]) |
                    (static_cast<uint16_t>(pcm[i * 2 + 1]) << 8));
            samples[i] = static_cast<float>(value) / 32768.0f;
            const uint64_t magnitude = value < 0
                    ? static_cast<uint64_t>(-static_cast<int32_t>(value))
                    : static_cast<uint64_t>(value);
            energy += magnitude * magnitude;
        }
        if (count == 0) continue;
        samples_seen += count;
        if (++frames % 10 == 0) {
            const double rms = samples_seen
                    ? std::sqrt(static_cast<double>(energy) /
                                static_cast<double>(samples_seen))
                    : 0.0;
            plugin_log(state, kLogDebug,
                       "audio frames=" + std::to_string(frames) + " rms=" +
                       std::to_string(static_cast<int>(rms)));
            energy = 0;
            samples_seen = 0;
        }

        std::string update;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!state.recognizer || !state.stream) continue;
            state.accept(state.stream, static_cast<int32_t>(kSampleRate),
                         samples.data(), static_cast<int32_t>(count));
            while (state.is_ready(state.recognizer, state.stream))
                state.decode(state.recognizer, state.stream);

            const SherpaOnnxOnlineRecognizerResult* result =
                    state.get_result(state.recognizer, state.stream);
            const std::string partial =
                    result && result->text ? result->text : "";
            if (result) state.destroy_result(result);

            bool endpoint = false;
            if (state.is_endpoint(state.recognizer, state.stream)) {
                if (!partial.empty()) {
                    state.final_text += partial;
                }
                state.reset_stream(state.recognizer, state.stream);
                endpoint = true;
                plugin_log(state, kLogDebug, "endpoint, " +
                           std::to_string(state.final_text.size()) + " chars");
            }
            update = state.final_text + (endpoint ? "" : partial);
        }
        if (!update.empty() && update != state.shown) {
            state.shown = update;
            emit_partial(state, update);
        }
    }
}

/* Finalizes the current stream and returns all text. Caller holds the lock. */
std::string finish_text(State& state) {
    if (!state.recognizer || !state.stream) return state.final_text;
    state.finish_input(state.stream);
    while (state.is_ready(state.recognizer, state.stream))
        state.decode(state.recognizer, state.stream);
    const SherpaOnnxOnlineRecognizerResult* result =
            state.get_result(state.recognizer, state.stream);
    if (result) {
        if (result->text && result->text[0]) state.final_text += result->text;
        state.destroy_result(result);
    }
    state.destroy_streams();
    state.stream = state.create_stream(state.recognizer);
    state.shown.clear();
    std::string text = state.final_text;
    state.final_text.clear();
    return text;
}

void stop_worker(State& state) {
    state.stopping.store(true);
    if (state.worker.joinable()) state.worker.join();
    state.stopping.store(false);
}

}  // namespace

namespace {

pime_status plugin_init(const uint8_t* config, uint32_t len, void** state) {
    if (!state) return PIME_INVALID;
    State& s = g_state;
    s.threads = 2;
    s.sample_rate = static_cast<int32_t>(kSampleRate);
    if (config && len) {
        MapView root = MapView::of(Span{config, config + len});
        MapView settings = MapView::of(root.get("settings"));
        const char* text = nullptr;
        uint32_t text_len = 0;
        if (settings.text("runtime_dir", &text, &text_len))
            s.runtime_dir.assign(text, text_len);
        if (settings.text("model_dir", &text, &text_len))
            s.model_dir.assign(text, text_len);
        if (settings.text("wav_file", &text, &text_len))
            s.wav_file.assign(text, text_len);
        uint64_t value = 0;
        if (settings.uint("threads", &value) && value >= 1 && value <= 8)
            s.threads = static_cast<int32_t>(value);
        if (settings.uint("sample_rate", &value) && value >= 8000)
            s.sample_rate = static_cast<int32_t>(value);
    }
    *state = &s;
    return PIME_OK;
}

pime_status plugin_set_host(void*, const pime_native_host* host) {
    g_state.host = host;
    return PIME_OK;
}

pime_status plugin_process(void*, const uint8_t* in, uint32_t in_len,
                           uint8_t* out, uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    *out_len = 0;
    State& state = g_state;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    if (!frame.is_event("voice")) return write_empty(out, cap, out_len, false);

    uint64_t session = 0;
    frame.root.uint("session", &session);
    uint64_t action = 0;
    frame.event.uint("action", &action);

    if (action == 0) { /* start */
        if (!ensure_recognizer(state)) return PIME_NOT_FOUND;
        stop_worker(state);
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.destroy_streams();
            state.stream = state.create_stream(state.recognizer);
            if (!state.stream) return PIME_UNSUPPORTED;
            state.final_text.clear();
            state.shown.clear();
        }
        state.session = session;
        state.wav_mode = !state.wav_file.empty();
        if (state.wav_mode) {
            std::string error;
            if (!load_wav(state.wav_file, &state.wav_pcm, &error)) {
                plugin_log(state, kLogInfo, "wav: " + error);
                std::lock_guard<std::mutex> lock(state.mutex);
                state.destroy_streams();
                return PIME_NOT_FOUND;
            }
            state.wav_pos = 0;
        } else if (!capture_start(state)) {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.destroy_streams();
            return PIME_UNSUPPORTED;
        }
        state.active = true;
        state.worker = std::thread(run_worker);
        plugin_log(state, kLogInfo,
                   "listening, session=" + std::to_string(session));
        return write_empty(out, cap, out_len, true);
    }

    if (action == 1) { /* stop: finalize synchronously and commit */
        stop_worker(state);
        capture_stop(state);
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::string text = finish_text(state);
        state.session = 0;
        state.active = false;
        plugin_log(state, kLogInfo, "stopped, " + std::to_string(text.size()) +
                                     " chars");
        if (text.empty()) return write_commands(out, cap, out_len, "", false);
        return write_commands(out, cap, out_len, text, true);
    }

    /* cancel */
    stop_worker(state);
    capture_stop(state);
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.destroy_streams();
        if (state.recognizer) state.stream = state.create_stream(state.recognizer);
        state.final_text.clear();
        state.shown.clear();
    }
    state.session = 0;
    state.active = false;
    return write_commands(out, cap, out_len, "", false);
}

void plugin_shutdown(void*) {
    capture_stop(g_state);
    g_state.release();
}

}  // namespace

extern "C" {

PIME_PLUGIN_EXPORT pime_status pime_native_plugin_query(
        uint32_t abi_major, pime_native_plugin* out) {
    if (!out || abi_major != PIME_ABI_MAJOR) return PIME_INVALID;
    out->size = sizeof(pime_native_plugin);
    out->version = PIME_ABI_MAJOR;
    out->init = &plugin_init;
    out->process = &plugin_process;
    out->on_event = nullptr;
    out->shutdown = &plugin_shutdown;
    out->set_host = &plugin_set_host;
    return PIME_OK;
}

}  // extern "C"
