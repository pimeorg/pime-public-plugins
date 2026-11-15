# pime-shape-parse

`parse` 插件：识别 u 拆字与反引号字形筛选前缀（对应 WeaveText
`weave-dict/shape.rs` 的前缀判断部分）。

只看链上第一个非空的 parse 码串（全拼/五笔/仓颉/九键/英文）：

| 码串 | mode | code |
|---|---|---|
| `umumu` | `shape` | `mumu` |
| `` `mu `` | `glyph` | `mu` |
| `` ` ``（或 keymap `action=backtick`） | `glyph` | 空 |
| 其他 | 空 | 空 |

真正的查字/筛选在 `pime-shape-translate`（translate role）。

## Workspace 读取

| 键 | 说明 |
|---|---|
| `plugin/pime-pinyin-parse/raw` 等 | 当前码串 |
| `plugin/pime-keymap-default/action` | `backtick` 动作 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-shape-parse/mode` | text | `shape` / `glyph` / 空 |
| `plugin/pime-shape-parse/code` | text | 去掉前缀后的码串 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不识别前缀 |

## 预算

同步调用，`sync_us: 20`。只做前缀判断，无分配。

## 用例

`tests/cases/`：u 模式、反引号码、普通码放行、backtick 动作。
