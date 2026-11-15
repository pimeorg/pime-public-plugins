/*
 * Native plugin simulator.
 *
 * Links one plugin's WASM entry points (compiled natively) against
 * sim_stubs.cpp and drives a single call:
 *
 *   sim_<plugin> --assets <dir> [--event] [--init <hex config>] < input.cbor
 *
 * The input frame is read from stdin, the output frame is written to stdout.
 * When stdin holds a CBOR array instead of a map, the frames are processed in
 * order in one process (so plugin state and the KV store persist) and the
 * output is an array of byte strings, one per frame. This is test scaffolding
 * for the C++ plugin logic; it is not the host ABI.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "sim_stubs.h"

namespace {

std::vector<uint8_t> hex_to_bytes(const std::string& text) {
    std::vector<uint8_t> out;
    uint8_t high = 0;
    bool half = false;
    for (char c : text) {
        uint8_t value = 0;
        if (c >= '0' && c <= '9') value = static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value = static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value = static_cast<uint8_t>(c - 'A' + 10);
        else continue;
        if (!half) {
            high = value;
            half = true;
        } else {
            out.push_back(static_cast<uint8_t>((high << 4) | value));
            half = false;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    /* Binary CBOR frames must not go through CRLF translation. */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string assets;
    std::string init_hex;
    bool on_event = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            assets = argv[++i];
        } else if (std::strcmp(argv[i], "--init") == 0 && i + 1 < argc) {
            init_hex = argv[++i];
        } else if (std::strcmp(argv[i], "--event") == 0) {
            on_event = true;
        }
    }
    sim_set_asset_dir(assets.c_str());

    std::vector<uint8_t> input((std::istreambuf_iterator<char>(std::cin)),
                               std::istreambuf_iterator<char>());
    std::vector<uint8_t> config;
    if (init_hex.empty()) {
        config.push_back(0xa0); /* empty map */
    } else {
        config = hex_to_bytes(init_hex);
    }

    pime_status status = pime_init(config.data(), static_cast<uint32_t>(config.size()));
    if (status != PIME_OK) {
        std::fprintf(stderr, "init=%d\n", static_cast<int>(status));
        return 11;
    }

    auto call = [&](const uint8_t* data, uint32_t len,
                    std::vector<uint8_t>* result) -> pime_status {
        result->resize(65536);
        uint32_t out_len = 0;
        pime_status st =
            on_event ? pime_on_event(data, len, result->data(),
                                     static_cast<uint32_t>(result->size()),
                                     &out_len)
                     : pime_process(data, len, result->data(),
                                    static_cast<uint32_t>(result->size()),
                                    &out_len);
        if (st == PIME_BUFFER_TOO_SMALL) {
            result->resize(out_len);
            out_len = 0;
            st = on_event ? pime_on_event(data, len, result->data(),
                                          static_cast<uint32_t>(result->size()),
                                          &out_len)
                          : pime_process(data, len, result->data(),
                                         static_cast<uint32_t>(result->size()),
                                         &out_len);
        }
        if (st == PIME_OK) result->resize(out_len);
        return st;
    };

    /* A CBOR array means "run these frames in order in one process"; state and
     * the KV store then persist across steps, which single-frame tests cannot
     * exercise. */
    pimeplug::Span root_span{input.data(), input.data() + input.size()};
    pimeplug::ArrayView frames = pimeplug::ArrayView::of(root_span);
    if (frames.valid) {
        std::vector<std::vector<uint8_t>> results;
        results.reserve(static_cast<size_t>(frames.count));
        for (uint64_t i = 0; i < frames.count; ++i) {
            pimeplug::Span frame = frames.at(i);
            std::vector<uint8_t> result;
            const pime_status st =
                call(frame.p, static_cast<uint32_t>(frame.e - frame.p), &result);
            if (st != PIME_OK) {
                std::fprintf(stderr, "process[%llu]=%d\n",
                             static_cast<unsigned long long>(i),
                             static_cast<int>(st));
                return 12;
            }
            results.push_back(std::move(result));
        }
        size_t total = 8;
        for (const std::vector<uint8_t>& result : results)
            total += result.size() + 9;
        std::vector<uint8_t> out(total);
        pimeplug::Writer w(out.data(), static_cast<uint32_t>(out.size()));
        w.array(results.size());
        for (const std::vector<uint8_t>& result : results)
            w.bytes(result.data(), static_cast<uint32_t>(result.size()));
        if (!w.ok()) {
            std::fprintf(stderr, "encode failed\n");
            return 13;
        }
        std::fwrite(out.data(), 1, w.size(), stdout);
        pime_shutdown();
        return 0;
    }

    std::vector<uint8_t> output;
    const pime_status final_status =
        call(input.data(), static_cast<uint32_t>(input.size()), &output);
    if (final_status != PIME_OK) {
        std::fprintf(stderr, "process=%d\n", static_cast<int>(final_status));
        return 12;
    }
    std::fwrite(output.data(), 1, output.size(), stdout);
    pime_shutdown();
    return 0;
}
