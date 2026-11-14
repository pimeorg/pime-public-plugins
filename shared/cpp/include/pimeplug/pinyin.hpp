/*
 * Shared Mandarin syllable table and helpers (toneless, ü written v).
 * Used by pinyin, shuangpin and T9 parsers/translators.
 */
#ifndef PIMEPLUG_PINYIN_HPP
#define PIMEPLUG_PINYIN_HPP

#include <stdint.h>

#include "cbor.hpp"
#include "text.hpp"

namespace pimeplug {

inline const char* syllable_list() {
    return "a ai an ang ao "
           "ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu "
           "ca cai can cang cao ce cen ceng cha chai chan chang chao che chen "
           "cheng chi chong chou chu chua chuai chuan chuang chui chun chuo ci "
           "cong cou cu cuan cui cun cuo "
           "da dai dan dang dao de dei den deng di dia dian diao die ding diu "
           "dong dou du duan dui dun duo "
           "e ei en eng er "
           "fa fan fang fei fen feng fo fou fu "
           "ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang "
           "gui gun guo "
           "ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang "
           "hui hun huo "
           "ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun "
           "ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang "
           "kui kun kuo "
           "la lai lan lang lao le lei leng li lia lian liang liao lie lin ling "
           "liu long lou lu luan lue lun luo lv lve "
           "ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo "
           "mou mu "
           "na nai nan nang nao ne nei nen neng ni nian niang niao nie nin ning "
           "niu nong nou nu nuan nue nun nuo nv nve "
           "o ou "
           "pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu "
           "qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun "
           "ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo "
           "sa sai san sang sao se sen seng sha shai shan shang shao she shei "
           "shen sheng shi shou shu shua shuai shuan shuang shui shun shuo si "
           "song sou su suan sui sun suo "
           "ta tai tan tang tao te tei teng ti tian tiao tie ting tong tou tu "
           "tuan tui tun tuo "
           "wa wai wan wang wei wen weng wo wu "
           "xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun "
           "ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun "
           "za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe "
           "zhei zhen zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun "
           "zhuo zi zong zou zu zuan zui zun zuo ";
}

inline bool is_syllable(const char* text, uint32_t len) {
    const char* list = syllable_list();
    uint32_t i = 0;
    while (list[i]) {
        while (list[i] == ' ') ++i;
        const uint32_t start = i;
        while (list[i] && list[i] != ' ') ++i;
        if (i - start == len &&
            Frame::equal(list + start, len, text, len))
            return true;
    }
    return false;
}

inline bool is_syllable_prefix(const char* text, uint32_t len) {
    if (len == 0 || len > 6) return false;
    const char* list = syllable_list();
    uint32_t i = 0;
    while (list[i]) {
        while (list[i] == ' ') ++i;
        const uint32_t start = i;
        while (list[i] && list[i] != ' ') ++i;
        if (i - start >= len && Frame::equal(list + start, len, text, len))
            return true;
    }
    return false;
}

/* Splits a toneless syllable into initial (up to 2 bytes) and final. */
inline void split_initial_final(const char* text, uint32_t len, uint32_t* ini,
                                uint32_t* fin) {
    *ini = 0;
    *fin = 0;
    if (len == 0) return;
    if (len >= 2 && ((text[0] == 'z' && text[1] == 'h') ||
                     (text[0] == 'c' && text[1] == 'h') ||
                     (text[0] == 's' && text[1] == 'h'))) {
        *ini = 2;
        *fin = len - 2;
        return;
    }
    const char c = text[0];
    const bool vowel = (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
                        c == 'v');
    if (!vowel) {
        *ini = 1;
        *fin = len - 1;
    } else {
        *fin = len;
    }
}

/* Canonical fuzzy form (zh/z, ch/c, sh/s, n/l, ang/an, eng/en, ing/in,
 * iang/ian, uang/uan, v/u). */
inline uint32_t canonical_syllable(const char* text, uint32_t len, char* out) {
    uint32_t i = 0;
    uint32_t o = 0;
    if (len >= 2 && text[0] == 'z' && text[1] == 'h') {
        out[o++] = 'z';
        i = 2;
    } else if (len >= 2 && text[0] == 'c' && text[1] == 'h') {
        out[o++] = 'c';
        i = 2;
    } else if (len >= 2 && text[0] == 's' && text[1] == 'h') {
        out[o++] = 's';
        i = 2;
    } else if (text[0] == 'l') {
        out[o++] = 'n';
        i = 1;
    } else {
        out[o++] = text[0];
        i = 1;
    }
    while (i < len) {
        if (i + 4 <= len && text[i] == 'i' && text[i + 1] == 'a' &&
            text[i + 2] == 'n' && text[i + 3] == 'g') {
            out[o++] = 'i';
            out[o++] = 'a';
            out[o++] = 'n';
            i += 4;
        } else if (i + 4 <= len && text[i] == 'u' && text[i + 1] == 'a' &&
                   text[i + 2] == 'n' && text[i + 3] == 'g') {
            out[o++] = 'u';
            out[o++] = 'a';
            out[o++] = 'n';
            i += 4;
        } else if (i + 3 <= len && text[i] == 'a' && text[i + 1] == 'n' &&
                   text[i + 2] == 'g') {
            out[o++] = 'a';
            out[o++] = 'n';
            i += 3;
        } else if (i + 3 <= len && text[i] == 'e' && text[i + 1] == 'n' &&
                   text[i + 2] == 'g') {
            out[o++] = 'e';
            out[o++] = 'n';
            i += 3;
        } else if (i + 3 <= len && text[i] == 'i' && text[i + 1] == 'n' &&
                   text[i + 2] == 'g') {
            out[o++] = 'i';
            out[o++] = 'n';
            i += 3;
        } else {
            char c = text[i++];
            if (c == 'v') c = 'u';
            out[o++] = c;
        }
    }
    return o;
}

}  // namespace pimeplug

#endif /* PIMEPLUG_PINYIN_HPP */
