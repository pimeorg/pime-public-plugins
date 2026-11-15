# pime-translate-online

`service`（Lua）：在线翻译能力接入。

## 事件

| 事件 | 行为 |
|---|---|
| `translate.request {id, text, from?, to?}` | 发起 HTTPS POST（`text` ≤2000 字节） |
| `net.data {id, data}` | 累积译文（上限 64KB） |
| `net.close {id}` | `commit` 译文并写 `translate/last` |
| `net.error {id}` | `notify(4)` |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `endpoint` | string | "" | POST 端点；空则禁用 |
| `from` | string | auto | 源语言 |
| `to` | string | zh | 目标语言 |
| `timeout_ms` | int | 8000 | 超时 |

## 隐私

只发送宿主显式给出的待译文本；不附带词库、学习记录或上下文。
