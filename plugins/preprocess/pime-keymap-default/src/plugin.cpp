/*
 * pime-keymap-default: maps control key events to chain-wide actions.
 *
 * Text keys are passed through untouched; parse plugins read event.text
 * directly. The plugin is stateless and only writes the workspace key
 * "action" (prefixed by the core with plugin/pime-keymap-default/).
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
alignas(8) uint8_t g_scratch[kScratchBytes];

constexpr uint64_t kModCtrl = 1u << 1;
constexpr uint64_t kModAlt = 1u << 2;
constexpr uint64_t kModMeta = 1u << 3;

constexpr uint64_t kKeyBackspace = 8;
constexpr uint64_t kKeyTab = 9;
constexpr uint64_t kKeyEnter = 13;
constexpr uint64_t kKeyShift = 16;
constexpr uint64_t kKeyCapsLock = 20;
constexpr uint64_t kKeyEscape = 27;
constexpr uint64_t kKeySpace = 32;
constexpr uint64_t kKeyPageUp = 33;
constexpr uint64_t kKeyPageDown = 34;
constexpr uint64_t kKeyEnd = 35;
constexpr uint64_t kKeyHome = 36;
constexpr uint64_t kKeyLeft = 37;
constexpr uint64_t kKeyUp = 38;
constexpr uint64_t kKeyRight = 39;
constexpr uint64_t kKeyDown = 40;
constexpr uint64_t kKeyDelete = 46;
constexpr uint64_t kKeyDigitFirst = 48;
constexpr uint64_t kKeyDigitLast = 57;
constexpr uint64_t kKeyBacktick = 192;

bool g_space_selects = true;
bool g_candidate_keys = true;
bool g_number_selects = true;

/* Navigation and paging only become actions while a chain has candidates;
 * candidates-format decides whether the action is consumed. Digits keep their
 * text too, so an editor without a composition still receives them. */
const char* action_for(uint64_t keycode) {
    switch (keycode) {
        case kKeyBackspace: return "backspace";
        case kKeyTab: return "tab";
        case kKeyEnter: return "enter";
        case kKeyEscape: return "escape";
        case kKeySpace: return "space";
        case kKeyDelete: return "delete";
        case kKeyShift: return "shift";
        case kKeyCapsLock: return "capslock";
        case kKeyBacktick: return "backtick";
        default: return nullptr;
    }
}

const char* candidate_action_for(uint64_t keycode) {
    switch (keycode) {
        case kKeyLeft: return "left";
        case kKeyRight: return "right";
        case kKeyUp: return "up";
        case kKeyDown: return "down";
        case kKeyPageUp: return "pageup";
        case kKeyPageDown: return "pagedown";
        case kKeyHome: return "home";
        case kKeyEnd: return "end";
        default: return nullptr;
    }
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const char* action) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    if (action) {
        w.map(1);
        w.key("action");
        w.text(action);
    } else {
        w.map(0);
    }
    w.key("handled");
    /* Mapping a key is not consuming it: parse/format decide whether the
     * action is used, so unmatched keys still reach the editor. */
    w.boolean(false);
    w.key("commands");
    w.array(0);
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_space_selects = true;
    g_candidate_keys = true;
    g_number_selects = true;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    if (!root.valid) return PIME_INVALID;
    MapView settings = MapView::of(root.get("settings"));
    if (settings.valid) {
        bool value = true;
        if (settings.boolean("space_selects_candidate", &value))
            g_space_selects = value;
        if (settings.boolean("candidate_keys", &value)) g_candidate_keys = value;
        if (settings.boolean("number_selects_candidate", &value))
            g_number_selects = value;
    }
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    const char* type = nullptr;
    uint32_t type_len = 0;
    if (!frame.event.text("type", &type, &type_len) ||
        !Frame::equal(type, type_len, "key", 3))
        return respond(out, cap, out_len, nullptr);

    uint64_t action = 0;
    if (frame.event.uint("action", &action) && action != 0)
        return respond(out, cap, out_len, nullptr);

    uint64_t modifiers = 0;
    if (frame.event.uint("modifiers", &modifiers) &&
        (modifiers & (kModCtrl | kModAlt | kModMeta)))
        return respond(out, cap, out_len, nullptr);

    uint64_t keycode = 0;
    if (!frame.event.uint("keycode", &keycode))
        return respond(out, cap, out_len, nullptr);

    const char* mapped = action_for(keycode);
    if (mapped && keycode == kKeySpace && !g_space_selects) mapped = nullptr;
    if (!mapped && g_candidate_keys) mapped = candidate_action_for(keycode);
    if (!mapped && g_number_selects && keycode >= kKeyDigitFirst &&
        keycode <= kKeyDigitLast)
        mapped = "digit";
    return respond(out, cap, out_len, mapped);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr);
}

PIME_EXPORT void pime_shutdown(void) { g_space_selects = true; }

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
