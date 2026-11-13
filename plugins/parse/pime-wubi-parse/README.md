# pime-wubi-parse

`parse` 插件：把按键累积成五笔 86 字母码（默认最多 4 键），交给
`pime-wubi-translate` 查词表。

## 事件

| 事件 | 行为 |
|---|---|
| `key` / `text` 字母 | 追加小写字母，达到 `max_code` 后不再接收 |
| `action=backspace` | 删除最后一个字母 |
| `action=escape` | 清空并隐藏候选 |
| 其他 | 不改动 |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-wubi-parse/code` | text | 当前五笔码 |

读取：`plugin/pime-keymap-default/action`（`backspace` / `escape`）。

消费者：`pime-wubi-translate`、`pime-candidates-format`。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_code` | int | 4 | 码长上限（1..8） |

## 预算

`sync_us: 30`；只做一次字符串扫描。

## 用例

`tests/cases/`：累积、退格、清空、码长上限。
