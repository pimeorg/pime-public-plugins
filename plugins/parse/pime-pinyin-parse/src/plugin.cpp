/*
 * pime-pinyin-parse: full-pinyin segmentation (greedy longest match).
 *
 * State: { buf: text } — the letters typed so far, max 64 bytes.
 * Workspace out: raw (text), syllables (array<text>).
 * Workspace in: keymap action, shuangpin syllable/consumed.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxBuffer = 64;
constexpr uint32_t kMaxSegments = 16;
alignas(8) uint8_t g_scratch[kScratchBytes];

const char kSyllables[] =
    "a ai an ang ao "
    "ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu "
    "ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng "
    "chi chong chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu "
    "cuan cui cun cuo "
    "da dai dan dang dao de dei den deng di dia dian diao die ding diu dong "
    "dou du duan dui dun duo "
    "e ei en eng er "
    "fa fan fang fei fen feng fo fou fu "
    "ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui "
    "gun guo "
    "ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui "
    "hun huo "
    "ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun "
    "ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui "
    "kun kuo "
    "la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu "
    "long lou lu luan lue lun luo lv lve "
    "ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu "
    "na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning niu "
    "nong nou nu nuan nue nun nuo nv nve "
    "o ou "
    "pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu "
    "qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun "
    "ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo "
    "sa sai san sang sao se sen seng sha shai shan shang shao she shei shen "
    "sheng shi shou shu shua shuai shuan shuang shui shun shuo si song sou su "
    "suan sui sun suo "
    "ta tai tan tang tao te tei teng ti tian tiao tie ting tong tou tu tuan "
    "tui tun tuo "
    "wa wai wan wang wei wen weng wo wu "
    "xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun "
    "ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun "
    "za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei "
    "zhen zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi "
    "zong zou zu zuan zui zun zuo ";

bool is_syllable(const char* text, uint32_t len) {
    uint32_t i = 0;
    while (kSyllables[i]) {
        while (kSyllables[i] == ' ') ++i;
        const uint32_t start = i;
        while (kSyllables[i] && kSyllables[i] != ' ') ++i;
        const uint32_t token_len = i - start;
        if (token_len == len &&
            Frame::equal(kSyllables + start, token_len, text, len))
            return true;
    }
    return false;
}

bool is_syllable_prefix(const char* text, uint32_t len) {
    if (len == 0 || len > 6) return false;
    uint32_t i = 0;
    while (kSyllables[i]) {
        while (kSyllables[i] == ' ') ++i;
        const uint32_t start = i;
        while (kSyllables[i] && kSyllables[i] != ' ') ++i;
        const uint32_t token_len = i - start;
        if (token_len >= len &&
            Frame::equal(kSyllables + start, len, text, len))
            return true;
    }
    return false;
}

struct Segment {
    uint32_t start;
    uint32_t end;
};

uint32_t segment(const char* buffer, uint32_t len, Segment* out,
                 uint32_t max_segments) {
    uint32_t count = 0;
    uint32_t i = 0;
    while (i < len && count < max_segments) {
        if (buffer[i] == '\'') {
            ++i;
            continue;
        }
        uint32_t best = 0;
        uint32_t limit = len - i;
        if (limit > 6) limit = 6;
        for (uint32_t n = limit; n >= 1; --n) {
            if (is_syllable(buffer + i, n)) {
                best = n;
                break;
            }
        }
        if (best == 0) {
            /* A trailing incomplete syllable ("zh", "h") stays one segment;
             * translate treats it as a prefix of any matching syllable. */
            const uint32_t rest = len - i;
            best = (rest <= 6 && is_syllable_prefix(buffer + i, rest)) ? rest : 1;
        }
        out[count].start = i;
        out[count].end = i + best;
        ++count;
        i += best;
    }
    return count;
}

uint32_t load_buffer(const Frame& frame, char* buffer) {
    MapView state = MapView::of(frame.root.get("state"));
    const char* text = nullptr;
    uint32_t len = 0;
    if (!state.valid || !state.text("buf", &text, &len)) return 0;
    if (len > kMaxBuffer) len = kMaxBuffer;
    for (uint32_t i = 0; i < len; ++i) buffer[i] = text[i];
    return len;
}

bool append(char* buffer, uint32_t* len, const char* text, uint32_t text_len) {
    bool changed = false;
    for (uint32_t i = 0; i < text_len && *len < kMaxBuffer; ++i) {
        char c = text[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || c == '\'') {
            buffer[(*len)++] = c;
            changed = true;
        }
    }
    return changed;
}

bool text_is(const char* text, uint32_t len, const char* expected) {
    return text && len == cstr_len(expected) &&
           Frame::equal(text, len, expected, len);
}

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const char* buffer, uint32_t len, bool handled,
                    bool commit_phase) {
    Segment segments[kMaxSegments];
    const uint32_t segment_count = segment(buffer, len, segments, kMaxSegments);

    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(1);
    w.key("buf");
    /* On a commit the composition is gone, but the workspace still carries the
     * pre-commit raw text so translate and format can resolve the selection. */
    w.text(buffer, commit_phase ? 0 : len);

    w.key("workspace");
    w.map(2);
    w.key("raw");
    w.text(buffer, len);
    w.key("syllables");
    w.array(len > 0 ? segment_count : 0);
    for (uint32_t i = 0; i < segment_count && len > 0; ++i)
        w.text(buffer + segments[i].start, segments[i].end - segments[i].start);

    w.key("handled");
    w.boolean(handled);

    w.key("commands");
    if (commit_phase) {
        w.array(0);
    } else if (len > 0) {
        w.array(1);
        w.map(4);
        w.key("type");
        w.text("set_composition");
        w.key("text");
        w.text(buffer, len);
        w.key("cursor");
        w.u64(len);
        w.key("segments");
        w.array(segment_count);
        for (uint32_t i = 0; i < segment_count; ++i) {
            w.map(3);
            w.key("start");
            w.u64(segments[i].start);
            w.key("end");
            w.u64(segments[i].end);
            w.key("type");
            w.u64(0);
        }
    } else if (handled) {
        /* The composition is gone: finish it in the editor and drop the bar. */
        w.array(2);
        cmd_set_composition(w, "", 0, 0);
        cmd_hide_candidates(w);
    } else {
        w.array(0);
    }

    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t*, uint32_t) { return PIME_OK; }

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    char buffer[kMaxBuffer];
    uint32_t len = load_buffer(frame, buffer);
    bool handled = false;

    const char* type = nullptr;
    uint32_t type_len = 0;
    const bool has_type = frame.event.text("type", &type, &type_len);
    const bool is_key = has_type && text_is(type, type_len, "key");
    const bool is_text = has_type && text_is(type, type_len, "text");
    const bool is_candidate =
        has_type && text_is(type, type_len, "candidate");

    const char* action = nullptr;
    uint32_t action_len = 0;
    span_text(frame.workspace_value("plugin/pime-keymap-default/action"),
              &action, &action_len);

    bool shuangpin_consumed = false;
    span_bool(frame.workspace_value("plugin/pime-shuangpin-standard/consumed"),
              &shuangpin_consumed);

    bool commit_phase = false;
    if (is_candidate) {
        /* The candidate is being committed: clear the state, but keep the raw
         * text in the workspace so format can resolve the selection. */
        commit_phase = true;
    } else if (is_key || is_text) {
        if (text_is(action, action_len, "backspace")) {
            /* Empty buffer: let the editor delete the committed text. */
            if (len > 0) {
                --len;
                handled = true;
            }
        } else if (text_is(action, action_len, "escape")) {
            len = 0;
            handled = true;
        } else if (text_is(action, action_len, "space") ||
                   text_is(action, action_len, "enter")) {
            /* format commits the highlighted candidate on these keys. */
            commit_phase = true;
        } else if (shuangpin_consumed) {
            handled = true;
            const char* syllable = nullptr;
            uint32_t syllable_len = 0;
            if (span_text(
                    frame.workspace_value("plugin/pime-shuangpin-standard/syllable"),
                    &syllable, &syllable_len))
                append(buffer, &len, syllable, syllable_len);
        } else {
            const char* text = nullptr;
            uint32_t text_len = 0;
            if (frame.event_text(&text, &text_len))
                handled = append(buffer, &len, text, text_len);
        }
    }

    return respond(out, cap, out_len, buffer, len, handled, commit_phase);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t* in, uint32_t in_len,
                                      uint8_t* out, uint32_t cap,
                                      uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;
    char buffer[kMaxBuffer];
    const uint32_t len = load_buffer(frame, buffer);
    return respond(out, cap, out_len, buffer, len, false, false);
}

PIME_EXPORT void pime_shutdown(void) {}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
