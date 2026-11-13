# pime-learn-frequency

`learn`（Lua）：记录用户实际提交的词，形成词频表。

## 行为

- 每个 `process` 读取 `plugin/pime-candidates-format/committed`；
- 非空词（≤64 字节）在插件 KV 中自增 `freq/<word>`；
- 写出 `last` / `count` 供调试与其他插件读取；
- 不改 `handled`，不产生命令；
- 定时器按 `flush_interval_ms` 批量 `store.flush()`，`shutdown` 时再刷一次。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-learn-frequency/last` | text | 最近提交的词 |
| `plugin/pime-learn-frequency/count` | uint | 该词的累计次数 |

## Store

`freq/<word>` → uint。schema 1；后续若改为二进制需递增 `store_schema`。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不学习 |
| `flush_interval_ms` | int | 30000 | 批量落盘周期 |

## 用例

`tests/cases/`：提交后计数、无提交时无动作。
