# pime-pinyin-parse

`parse` 插件：把全拼字母流切成音节，写 workspace 并驱动预编辑。

## 事件

| 事件 | 行为 |
|---|---|
| `key` / `text`，`event.text` 为字母 | 追加到内部缓冲，重新切分 |
| `key`，workspace 有 `action=backspace` | 删除缓冲最后一个字节 |
| `key`，workspace 有 `action=escape` | 清空缓冲 |
| 双拼链：`plugin/pime-shuangpin-standard/consumed=true` | 读取 `syllable` 追加完整音节，忽略原始按键文本 |

缓冲只保留 `[a-z']`，最多 64 字节；`'` 作为音节分隔符，不参与匹配。

## Workspace

写入（短键）：

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-pinyin-parse/raw` | text | 原始字母串 |
| `plugin/pime-pinyin-parse/syllables` | array<text> | 最大匹配切分结果，如 `["ni","hao"]` |

读取：

| 键 | 生产者 |
|---|---|
| `plugin/pime-keymap-default/action` | pime-keymap-default |
| `plugin/pime-shuangpin-standard/consumed` | pime-shuangpin-standard |
| `plugin/pime-shuangpin-standard/syllable` | pime-shuangpin-standard |

## 输出

缓冲非空时发 `set_composition`（`cursor` 为字节偏移，`segments` 与音节一一对应）；
清空时发 `hide_candidates`。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `fuzzy` | bool | true | 保留位：由 translate 使用 |
| `max_syllables` | int | 8 | 一次切分的音节上限 |

## 预算

`sync_us: 50`。音节表是只读常量，最大匹配最多 6 字节 × 8 段，无分配。

## 用例

`tests/cases/`：`nihao` 切分、退格、转义清空、分隔符。
