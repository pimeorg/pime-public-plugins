# pime-pinyin-rank

`rank` 插件：在 `pime-pinyin-translate` 之后做个性化重排，并记录用户选择。

## 规则

1. 读取 `plugin/pime-pinyin-translate/candidates`（`text/score/source/words`）。
2. 基础加权：
   - 1–2 个 Unicode 标量的候选加 `short_boost`；
   - `words == 1` 且长度 ≥ 3 的整词加 `cohesion_boost`（长词内聚）。
3. 个性化加分（可分别关闭）：
   - **学习**：读取 `pick/<raw>`。键等于当前 raw 时加 `learn_boost`，
     当前 raw 是其前缀时加一半；命中的候选注释为“常用”。
   - **近期**：读取 `last/<text>`，7 天内使用过的候选按剩余时间线性加
     `recent_boost`；注释“最近”。时间按小时分桶，保证同一次输入的排序稳定。
   - **领域**：`settings.profile` 选中的小词表（聊天/代码/正式）命中加
     `profile_boost`；注释“领域”。
4. 稳定排序后截断到 `max_candidates`；`handled` 保持 `false`。

注释优先级：常用 > 最近 > 领域。

## 学习与撤销

选词事件（`event.type = candidate`，`event.index`）发生时，按**本次输出顺序**
取出被选候选，写入：

| 键 | 值 |
|---|---|
| `pick/<raw>` | 被选文本 |
| `last/<text>` | 选择时的墙上毫秒 |

选词后 5 秒内按退格会删除这两条记录（退格撤销），并清空 state。排序先于
学习，保证 `index` 对应的就是刚才展示的那一行。

## State

`{ last_raw, last_text, last_ms }`：仅用于下一次退格撤销，`state_schema: 2`。

## Workspace

| 方向 | 键 |
|---|---|
| 读 | `plugin/pime-pinyin-translate/candidates`、`plugin/pime-pinyin-parse/raw`（回退 `input`）、`plugin/pime-keymap-default/action` |
| 写 | `plugin/pime-pinyin-rank/candidates` |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `short_boost` | int | 100 | 1–2 字候选加分 |
| `cohesion_boost` | int | 1500 | 单整词 3 字以上的内聚加分 |
| `learn_boost` | int | 20000 | 学习命中加分 |
| `recent_boost` | int | 4000 | 7 天内使用过的加分上限 |
| `profile_boost` | int | 3000 | 领域词加分 |
| `profile` | enum | `general` | `general` / `chat` / `code` / `formal` |
| `max_candidates` | int | 16 | 输出上限 |

## 预算

`sync_us: 50`；最多 32 条。每次调用一次 `pick/` 扫描、每条候选一次
`last/` 查询（`store.kv` + `time` 能力）。

## 用例

`tests/cases/`：短词加权、上限截断、无候选透传；以及多步用例（同一进程内
状态与 KV 持续）：学习命中、退格撤销、近期选择、领域词、长词内聚。
