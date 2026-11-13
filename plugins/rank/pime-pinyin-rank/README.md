# pime-pinyin-rank

`rank` 插件：对 `pime-pinyin-translate` 的候选做一次重排。

## 规则

1. 读取 `plugin/pime-pinyin-translate/candidates`。
2. 文本长度为 1 或 2 个 Unicode 标量的候选加 `short_boost` 分。
3. 按分数降序稳定排序，截断到 `max_candidates`。

## Workspace

| 方向 | 键 |
|---|---|
| 读 | `plugin/pime-pinyin-translate/candidates` |
| 写 | `plugin/pime-pinyin-rank/candidates` |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `short_boost` | int | 100 | 1–2 字候选加分 |
| `max_candidates` | int | 16 | 输出上限 |

## 预算

`sync_us: 50`；最多 32 条，插入排序。

## 用例

`tests/cases/`：短词加权、上限截断、无候选透传。
