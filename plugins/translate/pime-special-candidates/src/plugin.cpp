/*
 * pime-special-candidates: v-mode numbers, dates and the editor "expr=" path.
 *
 * WeaveText parity (core/weave-engine/src/special.rs):
 *   - "v" + number/expression: result, uppercase amount, Chinese numerals and
 *     thousands separators (v1234, v12.5*4, v(128+32)*4).
 *   - rq / riqi / jintian / mingtian / zuotian, sj / shijian / xianzai and
 *     xq / xingqi / zhouji: local date, time and weekday.
 *   - Editor path: a `surrounding` event whose text before the cursor ends in
 *     "=" turns the expression into its result directly.
 *
 * The plugin runs after rank: it reads the best existing candidate array and
 * writes a merged one, so a date key keeps the ordinary first candidate and
 * inserts the date/time entries right after it. Format prefers this plugin's
 * array whenever it is non-empty.
 *
 * No libm: `^` takes an integral exponent only and results are formatted with
 * the shared fixed-point helper.
 *
 * State: none. Workspace in: parse raw, rank/translate candidates.
 * Workspace out: candidates.
 */
#include <pime/plugin_api.h>

#include <pimeplug/cbor.hpp>
#include <pimeplug/commands.hpp>
#include <pimeplug/host.hpp>
#include <pimeplug/text.hpp>
#include <pimeplug/workspace.hpp>

using namespace pimeplug;

namespace {

constexpr uint32_t kScratchBytes = 16384;
constexpr uint32_t kMaxCandidates = 32;
constexpr uint32_t kMaxText = 192;
constexpr uint32_t kMaxComment = 48;
constexpr uint32_t kMaxExpr = 200;

alignas(8) uint8_t g_scratch[kScratchBytes];

bool g_enabled = true;
bool g_v_mode = true;
bool g_date = true;
uint32_t g_max_candidates = 8;

struct Item {
    char text[kMaxText];
    uint32_t text_len;
    char comment[kMaxComment];
    uint32_t comment_len;
    const char* source;
    bool has_words;
    uint32_t words;
};

const char* const kUpper[10] = {"零", "壹", "贰", "叁", "肆",
                                "伍", "陆", "柒", "捌", "玖"};
const char* const kLower[10] = {"零", "一", "二", "三", "四",
                                "五", "六", "七", "八", "九"};
const char* const kSmallUpper[4] = {"仟", "佰", "拾", ""};
const char* const kSmallLower[4] = {"千", "百", "十", ""};
const char* const kBigUnits[4] = {"", "万", "亿", "万亿"};
/* 0 = Sunday, matching CivilTime::weekday. */
const char* const kWeekdays[7] = {"星期日", "星期一", "星期二", "星期三",
                                  "星期四", "星期五", "星期六"};

bool same(const char* text, uint32_t len, const char* expected) {
    return text && len == cstr_len(expected) &&
           Frame::equal(text, len, expected, len);
}

void push(Item* items, uint32_t* count, const char* text, uint32_t text_len,
          const char* comment, const char* source) {
    if (*count >= kMaxCandidates || text_len == 0 || text_len >= kMaxText) return;
    Item& item = items[(*count)++];
    for (uint32_t i = 0; i < text_len; ++i) item.text[i] = text[i];
    item.text_len = text_len;
    const uint32_t comment_len = comment ? cstr_len(comment) : 0;
    const uint32_t copy =
        comment_len < kMaxComment ? comment_len : kMaxComment - 1;
    for (uint32_t i = 0; i < copy; ++i) item.comment[i] = comment[i];
    item.comment_len = copy;
    item.source = source;
    item.has_words = false;
    item.words = 0;
}

void push_buffer(Item* items, uint32_t* count, const Buffer& buffer,
                 const char* comment, const char* source) {
    push(items, count, buffer.data, buffer.len, comment, source);
}

/* ---------------------------------------------------------------- arithmetic */

struct Expr {
    const char* s;
    uint32_t n;
    uint32_t i;
    uint32_t depth;
};

bool expr_sum(Expr* p, double* out);

bool expr_atom(Expr* p, double* out) {
    if (p->i >= p->n) return false;
    if (p->s[p->i] == '(') {
        if (++p->depth > 32) return false;
        ++p->i;
        double value = 0;
        if (!expr_sum(p, &value)) return false;
        if (p->i >= p->n || p->s[p->i] != ')') return false;
        ++p->i;
        --p->depth;
        *out = value;
        return true;
    }
    const uint32_t start = p->i;
    bool dot = false;
    while (p->i < p->n) {
        const char c = p->s[p->i];
        if (c >= '0' && c <= '9') {
            ++p->i;
        } else if (c == '.') {
            if (dot) return false;
            dot = true;
            ++p->i;
        } else {
            break;
        }
    }
    if (p->i == start) return false;
    uint32_t dot_at = p->i;
    for (uint32_t k = start; k < p->i; ++k) {
        if (p->s[k] == '.') {
            dot_at = k;
            break;
        }
    }
    double integer = 0;
    for (uint32_t k = start; k < dot_at; ++k)
        integer = integer * 10 + (p->s[k] - '0');
    double fraction = 0;
    double scale = 1;
    for (uint32_t k = dot_at + 1; k < p->i; ++k) {
        fraction = fraction * 10 + (p->s[k] - '0');
        scale *= 10;
    }
    *out = integer + (scale > 1 ? fraction / scale : 0);
    return true;
}

bool expr_unary(Expr* p, double* out) {
    if (p->i < p->n && (p->s[p->i] == '-' || p->s[p->i] == '+')) {
        const char op = p->s[p->i++];
        double value = 0;
        if (!expr_unary(p, &value)) return false;
        *out = op == '-' ? -value : value;
        return true;
    }
    return expr_atom(p, out);
}

bool expr_power(Expr* p, double* out) {
    double base = 0;
    if (!expr_unary(p, &base)) return false;
    if (p->i < p->n && p->s[p->i] == '^') {
        ++p->i;
        double exponent = 0;
        if (!expr_power(p, &exponent)) return false;
        const double rounded =
            exponent >= 0
                ? static_cast<double>(static_cast<long long>(exponent + 0.5))
                : static_cast<double>(static_cast<long long>(exponent - 0.5));
        if (rounded != exponent || exponent > 64 || exponent < -64) return false;
        const long long n = static_cast<long long>(rounded);
        double result = 1;
        for (long long k = 0; k < (n < 0 ? -n : n); ++k) result *= base;
        *out = n < 0 ? 1.0 / result : result;
        return true;
    }
    *out = base;
    return true;
}

bool expr_product(Expr* p, double* out) {
    double value = 0;
    if (!expr_power(p, &value)) return false;
    while (p->i < p->n) {
        const char op = p->s[p->i];
        if (op != '*' && op != '/' && op != '%') break;
        ++p->i;
        double right = 0;
        if (!expr_power(p, &right)) return false;
        if ((op == '/' || op == '%') && right == 0) return false;
        if (op == '*') {
            value *= right;
        } else if (op == '/') {
            value /= right;
        } else {
            /* fmod without libm: remove the truncated quotient. */
            const long long q = static_cast<long long>(value / right);
            value -= static_cast<double>(q) * right;
        }
    }
    *out = value;
    return true;
}

bool expr_sum(Expr* p, double* out) {
    double value = 0;
    if (!expr_product(p, &value)) return false;
    while (p->i < p->n) {
        const char op = p->s[p->i];
        if (op != '+' && op != '-') break;
        ++p->i;
        double right = 0;
        if (!expr_product(p, &right)) return false;
        value = op == '+' ? value + right : value - right;
    }
    *out = value;
    return true;
}

bool finite(double v) { return v == v && v < 1e300 && v > -1e300; }

bool eval(const char* text, uint32_t len, double* out) {
    char normalized[kMaxExpr + 1];
    uint32_t n = 0;
    for (uint32_t i = 0; i < len; ++i) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == 'x' || c == 'X') c = '*';
        if (n >= kMaxExpr) return false;
        normalized[n++] = c;
    }
    if (n == 0) return false;
    Expr p{normalized, n, 0, 0};
    double value = 0;
    if (!expr_sum(&p, &value)) return false;
    if (p.i != n || !finite(value)) return false;
    *out = value;
    return true;
}

bool is_expression(const char* text, uint32_t len) {
    bool has_operator = false;
    bool has_digit = false;
    uint32_t i = 0;
    while (i < len && (text[i] == '+' || text[i] == '-')) ++i;
    for (; i < len; ++i) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            has_digit = true;
        } else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
                   c == '^') {
            has_operator = true;
        } else if (c == '.' || c == '(' || c == ')') {
            continue;
        } else {
            return false;
        }
    }
    return has_operator && has_digit;
}

/* ------------------------------------------------------------ Chinese numerals */

void digit_group(uint32_t n, const char* const* digits,
                 const char* const* units, bool zero_lead, Buffer& out) {
    const uint32_t d[4] = {n / 1000, n / 100 % 10, n / 10 % 10, n % 10};
    bool zero = zero_lead;
    bool started = false;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t x = d[i];
        if (x == 0) {
            if (started || zero_lead) zero = true;
            continue;
        }
        if (zero && (started || zero_lead)) out.append(digits[0]);
        zero = false;
        started = true;
        out.append(digits[x]);
        out.append(units[i]);
    }
}

void integer_text(uint64_t n, const char* const* digits,
                  const char* const* units, Buffer& out) {
    if (n == 0) {
        out.append(digits[0]);
        return;
    }
    uint32_t groups[4] = {0, 0, 0, 0};
    uint32_t count = 0;
    uint64_t rest = n;
    while (rest > 0 && count < 4) {
        groups[count++] = static_cast<uint32_t>(rest % 10000);
        rest /= 10000;
    }
    bool need_zero = false;
    for (int i = static_cast<int>(count) - 1; i >= 0; --i) {
        const uint32_t g = groups[i];
        if (g == 0) {
            need_zero = out.len > 0;
            continue;
        }
        const bool lead = out.len > 0 && (need_zero || g < 1000);
        digit_group(g, digits, units, lead, out);
        out.append(kBigUnits[i]);
        need_zero = false;
    }
}

/* 「一十」开头读作「十」. */
void chinese_number(uint64_t n, char* out, uint32_t cap, uint32_t* len) {
    char tmp[128];
    Buffer b(tmp, sizeof(tmp));
    integer_text(n, kLower, kSmallLower, b);
    const uint32_t start =
        (b.len >= 6 && Frame::equal(b.data, 6, "一十", 6)) ? 3 : 0;
    Buffer o(out, cap);
    o.append(b.data + start, b.len - start);
    *len = o.len;
}

bool money_upper(const char* s, uint32_t n, char* out, uint32_t cap,
                 uint32_t* len) {
    bool negative = false;
    uint32_t i = 0;
    if (i < n && (s[i] == '-' || s[i] == '+')) {
        negative = s[i] == '-';
        ++i;
    }
    const uint32_t int_start = i;
    while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
    const uint32_t int_len = i - int_start;
    if (int_len == 0 || int_len > 16) return false;
    uint32_t jiao = 0;
    uint32_t fen = 0;
    if (i < n) {
        if (s[i] != '.') return false;
        ++i;
        const uint32_t frac_start = i;
        while (i < n && s[i] >= '0' && s[i] <= '9') ++i;
        const uint32_t frac_len = i - frac_start;
        if (i != n || frac_len > 2) return false;
        if (frac_len > 0) jiao = static_cast<uint32_t>(s[frac_start] - '0');
        if (frac_len > 1) fen = static_cast<uint32_t>(s[frac_start + 1] - '0');
    }
    uint64_t value = 0;
    for (uint32_t k = int_start; k < int_start + int_len; ++k)
        value = value * 10 + static_cast<uint64_t>(s[k] - '0');

    Buffer b(out, cap);
    if (negative && (value > 0 || jiao > 0 || fen > 0)) b.append("负");
    if (value > 0) {
        integer_text(value, kUpper, kSmallUpper, b);
        b.append("元");
    }
    if (jiao == 0 && fen == 0) {
        if (value == 0) {
            b.append("零元整");
        } else {
            b.append("整");
        }
    } else {
        if (jiao > 0) {
            b.append(kUpper[jiao]);
            b.append("角");
        } else if (value > 0) {
            b.append("零");
        }
        if (fen > 0) {
            b.append(kUpper[fen]);
            b.append("分");
        }
    }
    *len = b.len;
    return !b.overflow;
}

void thousands(const char* s, uint32_t n, char* out, uint32_t cap,
               uint32_t* len) {
    Buffer b(out, cap);
    uint32_t i = 0;
    if (i < n && s[i] == '-') {
        b.append('-');
        ++i;
    }
    const uint32_t int_start = i;
    while (i < n && s[i] != '.') ++i;
    const uint32_t int_len = i - int_start;
    for (uint32_t k = 0; k < int_len; ++k) {
        if (k > 0 && (int_len - k) % 3 == 0) b.append(',');
        b.append(s[int_start + k]);
    }
    if (i < n) b.append(s + i, n - i);
    *len = b.len;
}

/* ---------------------------------------------------------------- v mode */

void v_candidates(const char* body, uint32_t body_len, Item* items,
                  uint32_t* count) {
    while (body_len > 0 && body[body_len - 1] == '=') --body_len;
    for (uint32_t i = 0; i < body_len; ++i) {
        if (body[i] == '=') return;
    }

    if (is_expression(body, body_len)) {
        double value = 0;
        if (!eval(body, body_len, &value)) return;
        char number[64];
        const uint32_t number_len = format_fixed(value, number, sizeof(number));
        if (number_len == 0) return;
        push(items, count, number, number_len, "计算结果", "special");
        char equation[kMaxText];
        Buffer eq(equation, sizeof(equation));
        eq.append(body, body_len);
        eq.append('=');
        eq.append(number, number_len);
        push_buffer(items, count, eq, "算式", "special");
        char money[kMaxText];
        uint32_t money_len = 0;
        if (money_upper(number, number_len, money, sizeof(money), &money_len))
            push(items, count, money, money_len, "大写金额", "special");
        return;
    }

    /* Plain number: optional sign, digits, at most one dot, one digit. */
    uint32_t i = 0;
    bool negative = false;
    if (i < body_len && (body[i] == '-' || body[i] == '+')) {
        negative = body[i] == '-';
        ++i;
    }
    const uint32_t magnitude_start = i;
    bool has_digit = false;
    bool dot = false;
    for (; i < body_len; ++i) {
        const char c = body[i];
        if (c >= '0' && c <= '9') {
            has_digit = true;
        } else if (c == '.') {
            if (dot) return;
            dot = true;
        } else {
            return;
        }
    }
    if (!has_digit) return;
    const uint32_t magnitude_len = body_len - magnitude_start;

    push(items, count, body, body_len, "", "special");

    char money[kMaxText];
    uint32_t money_len = 0;
    if (money_upper(body, body_len, money, sizeof(money), &money_len))
        push(items, count, money, money_len, "大写金额", "special");

    uint32_t int_len = 0;
    while (int_len < magnitude_len && body[magnitude_start + int_len] != '.')
        ++int_len;
    uint64_t integer = 0;
    for (uint32_t k = 0; k < int_len; ++k)
        integer = integer * 10 +
                  static_cast<uint64_t>(body[magnitude_start + k] - '0');

    char reading[kMaxText];
    Buffer rb(reading, sizeof(reading));
    uint32_t chinese_len = 0;
    chinese_number(integer, reading, sizeof(reading), &chinese_len);
    rb.assign(reading, chinese_len);
    if (int_len < magnitude_len) {
        rb.append("点");
        for (uint32_t k = int_len + 1; k < magnitude_len; ++k)
            rb.append(kLower[body[magnitude_start + k] - '0']);
    }
    bool non_zero = false;
    for (uint32_t k = magnitude_start; k < body_len; ++k) {
        if (body[k] >= '1' && body[k] <= '9') non_zero = true;
    }
    if (negative && non_zero) {
        char prefixed[kMaxText];
        Buffer pb(prefixed, sizeof(prefixed));
        pb.append("负");
        pb.append(rb.data, rb.len);
        push_buffer(items, count, pb, "中文数字", "special");
    } else {
        push_buffer(items, count, rb, "中文数字", "special");
    }

    if (int_len > 3) {
        const char* number = body[0] == '+' ? body + 1 : body;
        const uint32_t number_len = body_len - (body[0] == '+' ? 1 : 0);
        char grouped[kMaxText];
        uint32_t grouped_len = 0;
        thousands(number, number_len, grouped, sizeof(grouped), &grouped_len);
        push(items, count, grouped, grouped_len, "千分位", "special");
    }
}

/* ---------------------------------------------------------------- dates */

bool key_is(const char* raw, uint32_t len, const char* key) {
    return same(raw, len, key);
}

bool is_date_key(const char* raw, uint32_t len) {
    return key_is(raw, len, "rq") || key_is(raw, len, "riqi") ||
           key_is(raw, len, "日期") || key_is(raw, len, "jintian") ||
           key_is(raw, len, "今天") || key_is(raw, len, "mingtian") ||
           key_is(raw, len, "明天") || key_is(raw, len, "zuotian") ||
           key_is(raw, len, "昨天") || key_is(raw, len, "sj") ||
           key_is(raw, len, "shijian") || key_is(raw, len, "时间") ||
           key_is(raw, len, "xianzai") || key_is(raw, len, "现在") ||
           key_is(raw, len, "xq") || key_is(raw, len, "xingqi") ||
           key_is(raw, len, "星期") || key_is(raw, len, "zhouji") ||
           key_is(raw, len, "周几");
}

void append_padded(Buffer& b, uint64_t value, uint32_t width) {
    char tmp[24];
    const uint32_t n = format_uint(value, tmp, sizeof(tmp));
    for (uint32_t i = n; i < width; ++i) b.append('0');
    b.append(tmp, n);
}

void date_candidates(const char* raw, uint32_t raw_len, uint64_t wall_ms,
                     int64_t offset_minutes, Item* items, uint32_t* count) {
    int64_t shift_days = 0;
    if (key_is(raw, raw_len, "mingtian") || key_is(raw, raw_len, "明天"))
        shift_days = 1;
    else if (key_is(raw, raw_len, "zuotian") || key_is(raw, raw_len, "昨天"))
        shift_days = -1;
    const uint64_t stamp =
        shift_days >= 0
            ? wall_ms + static_cast<uint64_t>(shift_days) * 86400000ull
            : wall_ms - static_cast<uint64_t>(-shift_days) * 86400000ull;
    const CivilTime civil = civil_time(stamp, static_cast<int>(offset_minutes));
    const char* weekday = kWeekdays[civil.weekday];

    const bool date_key =
        key_is(raw, raw_len, "rq") || key_is(raw, raw_len, "riqi") ||
        key_is(raw, raw_len, "日期") || key_is(raw, raw_len, "jintian") ||
        key_is(raw, raw_len, "今天") || key_is(raw, raw_len, "mingtian") ||
        key_is(raw, raw_len, "明天") || key_is(raw, raw_len, "zuotian") ||
        key_is(raw, raw_len, "昨天");
    const bool time_key =
        key_is(raw, raw_len, "sj") || key_is(raw, raw_len, "shijian") ||
        key_is(raw, raw_len, "时间") || key_is(raw, raw_len, "xianzai") ||
        key_is(raw, raw_len, "现在");

    if (date_key) {
        char ymd_text[kMaxText];
        char iso_text[kMaxText];
        char full_text[kMaxText];
        char slash_text[kMaxText];
        char tmp[24];
        Buffer b(ymd_text, sizeof(ymd_text));
        uint32_t n = format_uint(static_cast<uint64_t>(civil.year), tmp,
                                 sizeof(tmp));
        b.append(tmp, n);
        b.append("年");
        n = format_uint(civil.month, tmp, sizeof(tmp));
        b.append(tmp, n);
        b.append("月");
        n = format_uint(civil.day, tmp, sizeof(tmp));
        b.append(tmp, n);
        b.append("日");
        push_buffer(items, count, b, "日期", "special");

        Buffer iso(iso_text, sizeof(iso_text));
        append_padded(iso, static_cast<uint64_t>(civil.year), 4);
        iso.append('-');
        append_padded(iso, civil.month, 2);
        iso.append('-');
        append_padded(iso, civil.day, 2);
        push_buffer(items, count, iso, "日期", "special");

        Buffer full(full_text, sizeof(full_text));
        full.append(b.data, b.len);
        full.append(' ');
        full.append(weekday);
        push_buffer(items, count, full, "日期", "special");

        Buffer slash(slash_text, sizeof(slash_text));
        append_padded(slash, static_cast<uint64_t>(civil.year), 4);
        slash.append('/');
        append_padded(slash, civil.month, 2);
        slash.append('/');
        append_padded(slash, civil.day, 2);
        push_buffer(items, count, slash, "日期", "special");
        return;
    }

    if (time_key) {
        char clock_text[kMaxText];
        char stamp_text_buf[kMaxText];
        char spoken_text[kMaxText];
        char precise_text[kMaxText];
        Buffer clock(clock_text, sizeof(clock_text));
        append_padded(clock, civil.hour, 2);
        clock.append(':');
        append_padded(clock, civil.minute, 2);
        push_buffer(items, count, clock, "时间", "special");

        Buffer stamp_text(stamp_text_buf, sizeof(stamp_text_buf));
        append_padded(stamp_text, static_cast<uint64_t>(civil.year), 4);
        stamp_text.append('-');
        append_padded(stamp_text, civil.month, 2);
        stamp_text.append('-');
        append_padded(stamp_text, civil.day, 2);
        stamp_text.append(' ');
        append_padded(stamp_text, civil.hour, 2);
        stamp_text.append(':');
        append_padded(stamp_text, civil.minute, 2);
        push_buffer(items, count, stamp_text, "时间", "special");

        Buffer spoken(spoken_text, sizeof(spoken_text));
        char tmp[24];
        const uint32_t n = format_uint(civil.hour, tmp, sizeof(tmp));
        spoken.append(tmp, n);
        spoken.append("点");
        append_padded(spoken, civil.minute, 2);
        spoken.append("分");
        push_buffer(items, count, spoken, "时间", "special");

        Buffer precise(precise_text, sizeof(precise_text));
        append_padded(precise, civil.hour, 2);
        precise.append(':');
        append_padded(precise, civil.minute, 2);
        precise.append(':');
        append_padded(precise, civil.second, 2);
        push_buffer(items, count, precise, "时间", "special");
        return;
    }

    push(items, count, weekday, cstr_len(weekday), "星期", "special");
}

/* ---------------------------------------------------------------- merging */

ArrayView best_candidates(const Frame& frame) {
    static const char* const kSources[] = {"pime-pinyin-rank",
                                           "pime-rank-default",
                                           "pime-pinyin-translate"};
    for (const char* id : kSources) {
        WorkspaceKey key = full_key(id, "candidates");
        ArrayView array = ArrayView::of(frame.workspace_value(key.text));
        if (array.valid && array.count > 0) return array;
    }
    return ArrayView{};
}

void copy_item(ArrayView array, uint64_t index, Item* items, uint32_t* count) {
    if (*count >= kMaxCandidates) return;
    MapView item = MapView::of(array.at(index));
    if (!item.valid) return;
    const char* text = nullptr;
    uint32_t text_len = 0;
    if (!item.text("text", &text, &text_len)) return;
    const char* comment = nullptr;
    uint32_t comment_len = 0;
    item.text("comment", &comment, &comment_len);
    if (text_len >= kMaxText) text_len = kMaxText - 1;
    Item& out = items[(*count)++];
    for (uint32_t i = 0; i < text_len; ++i) out.text[i] = text[i];
    out.text_len = text_len;
    const uint32_t copy =
        comment_len < kMaxComment ? comment_len : kMaxComment - 1;
    for (uint32_t i = 0; i < copy; ++i) out.comment[i] = comment[i];
    out.comment_len = copy;
    out.source = "dict";
    out.has_words = false;
    uint64_t words = 0;
    if (item.uint("words", &words)) {
        out.has_words = true;
        out.words = static_cast<uint32_t>(words);
    }
}

void date_merge(const Frame& frame, const char* raw, uint32_t raw_len,
                uint64_t wall_ms, int64_t offset_minutes, Item* items,
                uint32_t* count) {
    ArrayView existing = best_candidates(frame);
    if (existing.valid && existing.count > 0)
        copy_item(existing, 0, items, count);
    date_candidates(raw, raw_len, wall_ms, offset_minutes, items, count);
    for (uint64_t i = 1; existing.valid && i < existing.count; ++i)
        copy_item(existing, i, items, count);
}

/* Editor path: "12*3=" before the cursor. */
bool editor_result(const char* before, uint32_t len, Item* items,
                   uint32_t* count) {
    if (len < 2 || before[len - 1] != '=') return false;
    const uint32_t end = len - 1;
    uint32_t start = end;
    while (start > 0) {
        const char c = before[start - 1];
        if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' ||
            c == '*' || c == '/' || c == '%' || c == '^' || c == '(' ||
            c == ')') {
            --start;
        } else {
            break;
        }
    }
    if (start == end) return false;
    /* A letter right before the expression means the "=" belongs to a word. */
    if (start > 0 && ascii_alpha(before[start - 1])) return false;
    double value = 0;
    if (!eval(before + start, end - start, &value)) return false;
    char number[64];
    const uint32_t number_len = format_fixed(value, number, sizeof(number));
    if (number_len == 0) return false;
    push(items, count, number, number_len, "计算结果", "special");
    char equation[kMaxText];
    Buffer eq(equation, sizeof(equation));
    eq.append(before + start, end - start);
    eq.append('=');
    eq.append(number, number_len);
    push_buffer(items, count, eq, "算式", "special");
    return true;
}

/* ---------------------------------------------------------------- output */

pime_status respond(uint8_t* out, uint32_t cap, uint32_t* out_len,
                    const Item* items, uint32_t count, bool handled,
                    bool show_command) {
    Writer w(out, cap);
    w.map(4);
    w.key("state");
    w.map(0);
    w.key("workspace");
    w.map(1);
    w.key("candidates");
    w.array(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t fields = 2;
        if (items[i].comment_len) ++fields;
        if (items[i].has_words) ++fields;
        w.map(fields);
        w.key("text");
        w.text(items[i].text, items[i].text_len);
        w.key("source");
        w.text(items[i].source);
        if (items[i].comment_len) {
            w.key("comment");
            w.text(items[i].comment, items[i].comment_len);
        }
        if (items[i].has_words) {
            w.key("words");
            w.u64(items[i].words);
        }
    }
    w.key("handled");
    w.boolean(handled);
    w.key("commands");
    if (!show_command || count == 0) {
        w.array(0);
    } else {
        w.array(1);
        cmd_show_candidates_begin(w, count);
        for (uint32_t i = 0; i < count; ++i)
            cmd_candidate_item_ex(w, items[i].text, items[i].text_len,
                                  items[i].comment, items[i].comment_len,
                                  nullptr, 0);
    }
    *out_len = w.size();
    return w.ok() ? PIME_OK : PIME_BUFFER_TOO_SMALL;
}

uint32_t read_raw(const Frame& frame, const char** text) {
    WorkspaceKey key = full_key("pime-pinyin-parse", "raw");
    uint32_t len = 0;
    if (span_text(frame.workspace_value(key.text), text, &len)) return len;
    return 0;
}

}  // namespace

extern "C" {

PIME_EXPORT pime_status pime_init(const uint8_t* config, uint32_t len) {
    g_enabled = true;
    g_v_mode = true;
    g_date = true;
    g_max_candidates = 8;
    if (!config || !len) return PIME_OK;
    MapView root = MapView::of(Span{config, config + len});
    MapView settings = MapView::of(root.get("settings"));
    if (!settings.valid) return PIME_OK;
    bool value = true;
    if (settings.boolean("enabled", &value)) g_enabled = value;
    if (settings.boolean("v_mode", &value)) g_v_mode = value;
    if (settings.boolean("date", &value)) g_date = value;
    uint64_t max = 0;
    if (settings.uint("max_candidates", &max) && max >= 1 &&
        max <= kMaxCandidates)
        g_max_candidates = static_cast<uint32_t>(max);
    return PIME_OK;
}

PIME_EXPORT pime_status pime_process(const uint8_t* in, uint32_t in_len,
                                     uint8_t* out, uint32_t cap,
                                     uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    Frame frame;
    if (!frame.parse(in, in_len)) return PIME_CORRUPT;

    Item items[kMaxCandidates];
    uint32_t count = 0;
    bool handled = false;
    bool show_command = false;

    const char* type = nullptr;
    uint32_t type_len = 0;
    frame.event.text("type", &type, &type_len);

    if (same(type, type_len, "surrounding")) {
        const char* before = nullptr;
        uint32_t before_len = 0;
        if (g_enabled && frame.event.text("before", &before, &before_len) &&
            editor_result(before, before_len, items, &count)) {
            handled = true;
            show_command = true;
        }
    } else if (g_enabled) {
        const char* raw = nullptr;
        const uint32_t raw_len = read_raw(frame, &raw);
        if (raw_len > 0) {
            if (g_v_mode && raw[0] == 'v' && raw_len >= 2) {
                v_candidates(raw + 1, raw_len - 1, items, &count);
            } else if (g_date && is_date_key(raw, raw_len)) {
                date_merge(frame, raw, raw_len, host_time(1),
                           static_cast<int64_t>(host_time(2)), items, &count);
            }
            if (count > g_max_candidates) count = g_max_candidates;
            handled = count > 0;
        }
    }

    return respond(out, cap, out_len, items, count, handled, show_command);
}

PIME_EXPORT pime_status pime_on_event(const uint8_t*, uint32_t, uint8_t* out,
                                      uint32_t cap, uint32_t* out_len) {
    if (!out_len) return PIME_INVALID;
    return respond(out, cap, out_len, nullptr, 0, false, false);
}

PIME_EXPORT void pime_shutdown(void) {
    g_enabled = true;
    g_v_mode = true;
    g_date = true;
    g_max_candidates = 8;
}

PIME_EXPORT uint32_t pime_scratch_ptr(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch));
}

PIME_EXPORT uint32_t pime_scratch_cap(void) { return kScratchBytes; }

}  // extern "C"
