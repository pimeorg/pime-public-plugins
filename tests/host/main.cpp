/*
 * Minimal core host for plugin/chain tests.
 *
 * Loads every package directory under --plugins, builds and activates the
 * generation from a chain config produced by tools/chainc, then feeds one
 * event and prints the host sink output as plain text lines:
 *
 *   pime_case_host --plugins DIR --chain chain.cbor [--trust DIR] \
 *                  [--text UTF8] [--key KEYCODE] [--modifiers M]
 *
 * This binary links the real core; it only ever appears when a pime::core
 * package is installed. It implements no input-method logic.
 */
#include <pime/core.h>
#include <pime/hal.h>
#include <pime/plugin.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct Output {
    std::vector<std::string> lines;
};

void add_line(Output* out, const char* kind, const char* text, uint32_t len) {
    std::string line(kind);
    line += ": ";
    line.append(text ? text : "", text ? len : 0);
    out->lines.push_back(line);
}

pime_status on_commit(void* ud, const pime_text* text) {
    add_line(static_cast<Output*>(ud), "commit", text->text, text->text_len);
    return PIME_OK;
}

pime_status on_composition(void* ud, const pime_composition* composition) {
    add_line(static_cast<Output*>(ud), "composition", composition->text,
             composition->text_len);
    return PIME_OK;
}

pime_status on_candidates(void* ud, const pime_candidate_list* list) {
    Output* out = static_cast<Output*>(ud);
    for (uint32_t i = 0; i < list->count; ++i) {
        add_line(out, "candidate", list->items[i].text,
                 list->items[i].text_len);
    }
    return PIME_OK;
}

pime_status on_hide(void* ud) {
    static_cast<Output*>(ud)->lines.emplace_back("hide");
    return PIME_OK;
}

pime_status on_beep(void* ud) {
    static_cast<Output*>(ud)->lines.emplace_back("beep");
    return PIME_OK;
}

pime_status on_notify(void* ud, pime_status code) {
    static_cast<Output*>(ud)->lines.emplace_back(
        std::string("notify: ") + pime_status_string(code));
    return PIME_OK;
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

pime_text make_text(const std::string& value) {
    pime_text text{};
    text.size = sizeof(pime_text);
    text.version = PIME_ABI_MAJOR;
    text.text = value.data();
    text.text_len = static_cast<uint32_t>(value.size());
    return text;
}

}  // namespace

int main(int argc, char** argv) {
    std::string plugins;
    std::string chain;
    std::string trust;
    std::string text;
    std::string key_text;
    uint32_t modifiers = 0;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (!std::strcmp(arg, "--plugins") && has_value) plugins = argv[++i];
        else if (!std::strcmp(arg, "--chain") && has_value) chain = argv[++i];
        else if (!std::strcmp(arg, "--trust") && has_value) trust = argv[++i];
        else if (!std::strcmp(arg, "--text") && has_value) text = argv[++i];
        else if (!std::strcmp(arg, "--key") && has_value) key_text = argv[++i];
        else if (!std::strcmp(arg, "--modifiers") && has_value)
            modifiers = static_cast<uint32_t>(std::stoul(argv[++i]));
    }
    if (plugins.empty() || chain.empty()) {
        std::fprintf(stderr,
                     "usage: pime_case_host --plugins DIR --chain FILE "
                     "[--trust DIR] [--text TEXT] [--key KEYCODE] "
                     "[--modifiers M]\n");
        return 2;
    }

    Output output;
    pime_host_sink sink{};
    sink.size = sizeof(pime_host_sink);
    sink.version = PIME_ABI_MAJOR;
    sink.set_composition = on_composition;
    sink.commit = on_commit;
    sink.show_candidates = on_candidates;
    sink.hide_candidates = on_hide;
    sink.beep = on_beep;
    sink.notify = on_notify;

    pime_core_config config{};
    config.size = sizeof(pime_core_config);
    config.version = PIME_ABI_MAJOR;
    config.plugin_dir = plugins.data();
    config.plugin_dir_len = static_cast<uint32_t>(plugins.size());
    config.trust_store = trust.data();
    config.trust_store_len = static_cast<uint32_t>(trust.size());
    config.log_level = 1;

    pime_core* core = pime_core_create(&config, &sink, &output);
    if (!core) {
        std::fprintf(stderr, "core create failed\n");
        return 3;
    }

    for (const auto& entry : std::filesystem::directory_iterator(plugins)) {
        if (!entry.is_directory()) continue;
        const std::string path = entry.path().string();
        const pime_status status =
            pime_plugin_load(core, path.data(), static_cast<uint32_t>(path.size()));
        if (status != PIME_OK) {
            std::fprintf(stderr, "load %s: %s\n", path.c_str(),
                         pime_status_string(status));
            pime_core_destroy(core);
            return 4;
        }
    }

    const std::vector<uint8_t> generation_config = read_file(chain);
    if (generation_config.empty()) {
        std::fprintf(stderr, "empty chain config %s\n", chain.c_str());
        pime_core_destroy(core);
        return 5;
    }
    uint64_t generation = 0;
    pime_status status = pime_generation_build(
        core, generation_config.data(),
        static_cast<uint32_t>(generation_config.size()), &generation);
    if (status == PIME_OK) status = pime_generation_activate(core, generation);
    if (status != PIME_OK) {
        std::fprintf(stderr, "generation: %s\n", pime_status_string(status));
        pime_core_destroy(core);
        return 6;
    }

    pime_session_config session_config{};
    session_config.size = sizeof(pime_session_config);
    session_config.version = PIME_ABI_MAJOR;
    pime_session* session = pime_session_create(core, &session_config);
    if (!session) {
        std::fprintf(stderr, "session create failed\n");
        pime_core_destroy(core);
        return 7;
    }

    if (!text.empty()) {
        const pime_text payload = make_text(text);
        status = pime_core_text(session, &payload);
    } else if (!key_text.empty()) {
        pime_key_event event{};
        event.size = sizeof(pime_key_event);
        event.version = PIME_ABI_MAJOR;
        event.keycode = static_cast<uint32_t>(std::stoul(key_text));
        event.modifiers = modifiers;
        int handled = 0;
        status = pime_core_key(session, &event, &handled);
        if (status == PIME_OK)
            output.lines.push_back(handled ? "handled: 1" : "handled: 0");
    }
    pime_core_pump(core);

    for (const auto& line : output.lines) std::printf("%s\n", line.c_str());

    pime_session_destroy(session);
    pime_generation_retire(core, generation);
    pime_core_destroy(core);
    return status == PIME_OK ? 0 : 8;
}
