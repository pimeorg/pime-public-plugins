# pime-english-parse

`parse` 插件：累积英文片段，保留用户实际输入的大小写。

## 事件

| 事件 | 行为 |
|---|---|
| `key` / `text` 字母 | 原样追加（大小写不变） |
| `text` 撇号 | 片段非空时追加，开头忽略 |
| `action=backspace` | 删除最后一个字符；空片段时交回编辑器删除 |
| `action=escape` | 清空并隐藏候选 |
| `action=space` / `enter` / `candidate` | 结束组合，保留 `raw` 供 format 取候选 |
| 其他 | 不改动 |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-english-parse/raw` | text | 当前英文片段（原码） |

读取：`plugin/pime-keymap-default/action`。

消费者：`pime-english-translate`、`pime-candidates-format`。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_code` | int | 32 | 片段长度上限（1..64） |

## 预算

`sync_us: 30`；只做一次字符串扫描。

## 用例

`tests/cases/`：累积、大小写保留、撇号、开头撇号忽略、退格、清空、
空格结束组合、码长上限。
