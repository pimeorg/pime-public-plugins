# pime-special-candidates

`translate` 插件：在常规候选之外提供 WeaveText 的特殊候选——`v` 模式的数字与
算式、日期/时间/星期，以及正文里的“算式=”。它排在 rank 之后，读入已排好序的
候选，写出合并后的候选数组。

## 事件

- `key` / `text`：读 `plugin/pime-pinyin-parse/raw`，识别 `v` 前缀与日期键。
- `surrounding`：读光标前文本（`event.before`），以 `=` 结尾时直接给出算式
  结果，并自行发出 `show_candidates` 命令。

## 输入

| 键 | 生产者 | 说明 |
|---|---|---|
| `plugin/pime-pinyin-parse/raw` | pime-pinyin-parse | 原始串；`v` 开头即 v 模式，`rq/sj/xq` 等为日期键 |
| `plugin/pime-pinyin-rank/candidates` | pime-pinyin-rank | 已有候选（日期合并时保留首位） |

## 输出

`plugin/pime-special-candidates/candidates`：`array<map>`，每项
`{ text, source: "special", comment? , words? }`。非空时 format 优先展示它。

### v 模式

`v` 之后的正文按 WeaveText 规则处理：

- **算式**（含 `+ - * / % ^` 与括号）：结果、`原式=结果`、结果的大写金额。
  仅支持整数指数（wasm 无 libm），非有限结果放弃。
- **数字**：原样、大写金额、中文数字（含小数读作“点”）、千分位（整数部分
  超过 3 位时）。

示例：`v1234` → `1234` / `壹仟贰佰叁拾肆元整` / `一千二百三十四` / `1,234`；
`v(128+32)*4` → `640` / `(128+32)*4=640` / `陆佰肆拾元整`。

### 日期键

| 键 | 候选 |
|---|---|
| `rq` `riqi` `jintian` `mingtian` `zuotian` | `Y年M月D日`、`YYYY-MM-DD`、`Y年M月D日 星期X`、`YYYY/MM/DD` |
| `sj` `shijian` `xianzai` | `HH:MM`、`YYYY-MM-DD HH:MM`、`H点MM分`、`HH:MM:SS` |
| `xq` `xingqi` `zhouji` | 星期 |

日期候选插在已有首位候选之后，其余候选顺延，因此“日期”这个词本身仍然可选。
本地时间取自 `time` 能力（墙钟毫秒 + 时区分钟），不依赖平台 API。

### 正文算式

光标前文本以 `=` 结尾且前面是一段纯算式（数字、运算符、括号、小数点）时，
直接给出结果与算式两条候选；算式前紧邻字母（如 `abc1+2=`）视为普通文本，
不接管。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 总开关 |
| `v_mode` | bool | true | `v` 数字/算式 |
| `date` | bool | true | 日期/时间/星期键 |
| `max_candidates` | int | 8 | 输出上限（1..32） |

## 预算

`sync_us: 80`；无资产、无状态，递归下降求值深度上限 32、表达式长度上限 200。

## 用例

`tests/cases/`：整数与算式、小数、负数、`rq`/`sj`/`xq`（固定时钟）、`mingtian`、
日期合并顺序、正文 `12*3=`、算式前有字母不接管、无匹配、`v_mode`/`date` 关闭。
