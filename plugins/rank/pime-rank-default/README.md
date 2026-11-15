# pime-rank-default

`rank` 插件：与具体输入法无关的候选重排。

## 规则

1. 依次尝试读取 `plugin/pime-pinyin-translate/candidates`、
   `plugin/pime-wubi-translate/candidates`、
   `plugin/pime-cangjie-translate/candidates`、`plugin/pime-t9-translate/candidates`、
   `plugin/pime-english-translate/candidates`，取第一个非空数组。
2. 丢弃 `score < min_score` 的候选。
3. 按分数降序稳定排序，截断到 `max_candidates`。

## Workspace

| 方向 | 键 |
|---|---|
| 读 | `plugin/pime-pinyin-translate/candidates` |
| 读 | `plugin/pime-wubi-translate/candidates` |
| 读 | `plugin/pime-cangjie-translate/candidates` |
| 读 | `plugin/pime-t9-translate/candidates` |
| 读 | `plugin/pime-english-translate/candidates` |
| 写 | `plugin/pime-rank-default/candidates` |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `min_score` | int | 0 | 低于该分数的候选被丢弃 |
| `max_candidates` | int | 16 | 输出上限 |

## 预算

`sync_us: 50`；最多 32 条。

## 用例

`tests/cases/`：排序、低分过滤、无候选透传。
