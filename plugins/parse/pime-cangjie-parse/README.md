# pime-cangjie-parse

`parse` 插件：把按键累积成仓颉字母码（默认最多 5 键），写入 workspace 供
下游（翻译 / 格式化）使用。

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
| `plugin/pime-cangjie-parse/code` | text | 当前仓颉码 |

消费者：`pime-candidates-format`（读取 `plugin/pime-cangjie-parse/code`）。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_code` | int | 5 | 码长上限（1..8） |

## 预算

`sync_us: 30`；只做一次字符串扫描。

## 用例

`tests/cases/`：累积、退格、清空。
