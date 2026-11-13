# pime-predict-context

`predict` 插件：读取光标前文本，用内置短语表做后缀匹配，给出下一词候选。

## 行为

- 从 `context.surrounding_before` 取文本（缺省则为空）。
- 用最长可匹配的短语表项（按表顺序，最多 `max_candidates` 条）产出候选。
- 不置 `handled`，不产生命令；候选由 `pime-candidates-format` 展示。

内置表（`src/plugin.cpp` 的 `kTable`，starter）：

| 前文 | 候选 |
|---|---|
| 你好 | 吗 / 啊 |
| 我们 | 的 / 一起 |
| 今天 | 的 / 天气 |
| 谢谢 | 你 |
| 对不 | 起 |
| 可以 | 的 / 吗 |
| 没有 | 问题 |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-predict-context/candidates` | array<map> | `{text, score, source:"context"}` |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 3 | 输出上限（1..8） |

## 预算

`sync_us: 40`；11 条表项 × 后缀比较。

## 用例

`tests/cases/`：`你好` 后缀命中、无上下文为空。
