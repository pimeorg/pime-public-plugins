# pime-pinyin-parse

`parse` 插件：把全拼字母流切成音节、纠正常见错拼，写 workspace 并驱动预编辑。

## 事件

| 事件 | 行为 |
|---|---|
| `key` / `text`，`event.text` 为字母 | 追加到内部缓冲，重新切分并纠错 |
| `text` 且无 state（一次传入整串） | 以 `event.text` 作为当前组合串 |
| `text` 带 `event.neighbour` | 触点邻键纠正的候选字母 |
| `key`，workspace 有 `action=backspace` | 删除缓冲最后一个字节 |
| `key`，workspace 有 `action=escape` | 清空缓冲 |
| 双拼链：`plugin/pime-shuangpin-standard/consumed=true` | 读取 `syllable` 追加完整音节，忽略原始按键文本 |

缓冲只保留 `[a-z']`，最多 96 字节；`'` 作为音节分隔符，不参与匹配。

切分：先做完整音节最长匹配；某个位置无法成音节时，若剩余整串是某音节的前缀
（`zh`、`h`），保持为一个“半音节”段，供 translate 做前缀匹配；否则按单字母切成
简拼段（`ws` → `["w","s"]`）。

## 自动纠错

只有“原串无法完整读成音节”且“纠正后能完整读成音节”时才采用纠正；纯简拼
（不含元音的串，如 `zsh`、`hbg`、`dhrs`）和已能读的串（如 `xianr`）不纠。

| 类型 | 例子 |
|---|---|
| `swap` | `zhogn` → `zhong` |
| `delete` | `nihhao` → `nihao`（优先删重复字母） |
| `insert` | `nhao` → `nihao` |
| `replace` | 触点 `nihap` + neighbour `o` → `nihao` |

## Workspace

写入（短键）：

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-pinyin-parse/raw` | text | 纠正后的字母串，translate 用它解码 |
| `plugin/pime-pinyin-parse/input` | text | 用户实际输入的字母串 |
| `plugin/pime-pinyin-parse/syllables` | array<text> | 纠错串的最大匹配切分，如 `["ni","hao"]` |
| `plugin/pime-pinyin-parse/marks` | array<map> | 纠正标记 `{type, start, end}`，偏移相对 `input` |

`set_composition.segments[].type`：`0` 正常、`1` 纠正、`2` 半音节/简拼。

读取：

| 键 | 生产者 |
|---|---|
| `plugin/pime-keymap-default/action` | pime-keymap-default |
| `plugin/pime-shuangpin-standard/consumed` | pime-shuangpin-standard |
| `plugin/pime-shuangpin-standard/syllable` | pime-shuangpin-standard |

## 输出

缓冲非空时发 `set_composition`（`cursor` 为字节偏移，`segments` 与音节一一对应）；
清空时发 `hide_candidates`。状态保存原串，下一次按键继续在用户实际输入上追加。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `fuzzy` | bool | true | 保留位：由 translate 使用 |
| `max_syllables` | int | 8 | 一次切分的音节上限 |
| `auto_correct` | bool | true | 错拼自动纠正 |
| `tap_correct` | bool | true | 触点邻键纠正 |

## 预算

`sync_us: 50`。音节表是只读常量，最大匹配最多 6 字节 × 8 段，无分配。

## 用例

`tests/cases/`：`nihao` 切分、退格、转义清空、分隔符。
