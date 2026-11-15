/*
 * pime-punctuation-zh: half-width ASCII punctuation -> Chinese punctuation.
 *
 * Converted from the Lua original to WASM so the behaviour is covered by the
 * native simulator. The mapping table keeps the first-batch defaults; new
 * settings cover the remaining "full punctuation set" items:
 *   - braces:  "{ }" -> full-width braces (default) or corner brackets "「」";
 *   - multi_char: "^" -> "……", "_" -> "——" (multi-character output);
 *   - quotes: chinese (alternating “ ” / ‘ ’) or straight (pass through).
 *
 * Paired-bracket auto-insertion (typing "(" inserts "（）" and places the caret
 * between them) needs editor caret control; the command ABI has no caret-move
 * command, so it is not approximated.
 *
 * State: { quote_open: bool } so quotes alternate across calls.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 4096;

alignas(8) uint8_t g_scratch[kScratchBytes];

bool g_enabled = true;
bool g_chinese_quotes = true;
bool g_corner_braces = false;
bool g_multi_char = false;

struct Mapping {
    char input;
    const char* output;
};

const Mapping kMappings[] = {
    {',', "，"}, {'.', "。"}, {'?', "？"}, {'!', "！"}, {':', "："},
    {';', "；"}, {'(', "（"}, {')', "）"}, {'[', "【"}, {']', "】"},
    {'\\', "、"}, {'~', "～"}, {'@', "＠"}, {'#', "＃"}, {'$', "￥"},
    {'%', "％"}, {'^', "＾"}, {'&', "＆"}, {'*', "＊"}, {'-', "－"},
    {'_', "＿"}, {'+', "＋"}, {'=', "＝"}, {'|', "｜"}, {'/', "／"},
    {'`', "｀"}, {'<', "《"}, {'>', "》"},
};

bool blocked_action(const char* action, uint32_t len) {
    return text_equal(action, len, "space") || text_equal(action, len, "enter") ||
           text_equal(action, len, "escape") ||
           text_equal(action, len, "backspace") ||
           text_equal(action, len, "tab") || text_equal(action, len, "delete");
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len, bool quote,
                    bool quote_valid, const char* text, uint32_t text_len,
                    bool handled) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    if (quote_valid) {
        w.map(1);
        w.key("quote_open");
        w.boolean(quote);
    } else {
        w.map(0);
    }
    w.key("workspace");
    w.map(0);
    w.key("handled");
    w.boolean(handled);
    w.key("commands");
    if (handled) {
        w.array(1);
        cmd_commit(w, text, text_len);
    } else {
        w.array(0);
    }
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_enabled = true;
    g_chinese_quotes = true;
    g_corner_braces = false;
    g_multi_char = false;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    if (settings.boolean("multi_char", &value)) g_multi_char = value;
    const char* quotes = nullptr;
    uint32_t quotes_len = 0;
    if (settings.text("quotes", &quotes, &quotes_len))
        g_chinese_quotes = text_equal(quotes, quotes_len, "chinese");
    const char* braces = nullptr;
    uint32_t braces_len = 0;
    if (settings.text("braces", &braces, &braces_len))
        g_corner_braces = text_equal(braces, braces_len, "corner");
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    MapView state = MapView::of(frame.root.get("state"));
    bool quote_open = false;
    const bool has_quote = state.valid && state.boolean("quote_open", &quote_open);

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_text = has_type && text_equal(type, type_len, "text");
    const bool is_key = has_type && text_equal(type, type_len, "key");
    if (!g_enabled || (!is_text && !is_key))
        return respond(out, cap, out_len, quote_open, has_quote, nullptr, 0, false);

    /* Shift is how "?" ":" and friends are typed, so only Ctrl/Alt/Meta
     * release the key (the host owns those shortcuts). */
    uint64_t modifiers = 0;
    if (frame.event.uint("modifiers", &modifiers) && (modifiers & ~1ull) != 0)
        return respond(out, cap, out_len, quote_open, has_quote, nullptr, 0, false);

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"), &action,
              &action_len);
    if (blocked_action(action, action_len))
        return respond(out, cap, out_len, quote_open, has_quote, nullptr, 0, false);

    const char* raw = nullptr;
    uint32_t raw_len = 0;
    if (!frame.event.text("text", &raw, &raw_len) || raw_len != 1)
        return respond(out, cap, out_len, quote_open, has_quote, nullptr, 0, false);

    const char input = raw[0];
    if (input == '"' || input == '\'') {
        if (!g_chinese_quotes)
            return respond(out, cap, out_len, quote_open, has_quote, nullptr, 0, false);
        const char* text = nullptr;
        uint32_t text_len = 0;
        if (input == '"') {
            text = quote_open ? "”" : "“";
            text_len = 3;
        } else {
            text = quote_open ? "’" : "‘";
            text_len = 3;
        }
        return respond(out, cap, out_len, !quote_open, true, text, text_len, true);
    }

    if (input == '{' || input == '}') {
        const char* text = nullptr;
        if (g_corner_braces)
            text = input == '{' ? "「" : "」";
        else
            text = input == '{' ? "｛" : "｝";
        return respond(out, cap, out_len, has_quote, has_quote, text, 3, true);
    }

    if (g_multi_char && input == '^')
        return respond(out, cap, out_len, has_quote, has_quote, "……", 6, true);
    if (g_multi_char && input == '_')
        return respond(out, cap, out_len, has_quote, has_quote, "——", 6, true);

    for (const Mapping& entry : kMappings) {
        if (entry.input != input) continue;
        return respond(out, cap, out_len, has_quote, has_quote, entry.output,
                       cstr_len(entry.output), true);
    }
    return respond(out, cap, out_len, quote_open, has_quote, nullptr, 0, false);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, false, false, nullptr, 0, false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_enabled = true;
    g_chinese_quotes = true;
    g_corner_braces = false;
    g_multi_char = false;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
