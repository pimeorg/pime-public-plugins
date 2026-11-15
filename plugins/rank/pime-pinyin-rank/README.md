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
   - **用户词典**：扫描 `usr/<raw>\x1f*`，把学过的词（含连续造词）注入候选或
     加权，`user_boost + count*500`；注释“自造”。
   - **热词**：`hot/<text>` 有权重时加 `hotword_boost + weight`；注释“热词”。
   - **近期**：读取 `last/<text>`，7 天内使用过的候选按剩余时间线性加
     `recent_boost`；注释“最近”。
   - **领域**：`settings.profile` 选中的小词表（聊天/代码/正式）命中加
     `profile_boost`；注释“领域”。
4. 稳定排序后截断到 `max_candidates`；`handled` 保持 `false`。

注释优先级：常用 > 自造 > 热词 > 最近 > 领域。

## 学习与撤销

选词事件（`event.type = candidate`，`event.index`）以及 **space / enter / 数字**
提交都会记录选择；后三者用与 `pime-candidates-format` 相同的分页规则换算槽位
（`settings.page_size` 必须与 format 一致，默认都是 5）。

写入：

| 键 | 值 |
|---|---|
| `pick/<raw>` | 被选文本 |
| `last/<text>` | 选择时的墙上毫秒 |
| `usr/<raw>\x1f<text>` | `<count>:<day>`，按 `decay_days` 折半 |

连续造词：连续提交两个及以上单字（码串为纯字母）时，把拼接词按拼接码串写入
`usr/<concatenated-raw>\x1f<word>`，下次输入该码串即可召回。

选词后 5 秒内按退格会删除 `pick/`、`last/` 并回退 `usr/` 计数（退格撤销），
并清空 state。排序先于学习，保证 `index` 对应的就是刚才展示的那一行。

当链上存在合并视图（`pime-shape-translate` / `pime-shortcuts` /
`pime-hans-hant` / `pime-special-candidates` 任一有候选）时，展示的数组不是本插件
的输出，槽位无法对应，此时**不学习**。

## 热词

云端热词由 `pime-cloud-hotwords` 下载并验签；宿主把验签后的 payload 以
`hotword.update { payload: "<词>\t<权重>\n..." }` 事件转给本插件，本插件只做
存储与排序偏置。`hotword.clear` 清空。

## State

`{ last_raw, last_text, last_ms, page, chain, chain_raw }`：撤销记录、分页位置与
连续造词累积，`state_schema: 2`。

## Workspace

| 方向 | 键 |
|---|---|
| 读 | `plugin/pime-pinyin-translate/candidates`、`plugin/pime-pinyin-parse/raw`（回退 `input`）、`plugin/pime-keymap-default/action` / `digit`、`plugin/pime-shape-translate/candidates` 等合并视图判断 |
| 写 | `plugin/pime-pinyin-rank/candidates`、`plugin/pime-pinyin-rank/learned`（本次注入/加权的用户词数）、`plugin/pime-pinyin-rank/hotwords`（热词命中数） |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `short_boost` | int | 100 | 1–2 字候选加分 |
| `cohesion_boost` | int | 1500 | 单整词 3 字以上的内聚加分 |
| `learn_boost` | int | 20000 | 学习命中加分 |
| `user_boost` | int | 8000 | 用户词典基础加分 |
| `hotword_boost` | int | 12000 | 热词基础加分 |
| `decay_days` | int | 30 | 用户词典衰减周期 |
| `recent_boost` | int | 4000 | 7 天内使用过的加分上限 |
| `profile_boost` | int | 3000 | 领域词加分 |
| `profile` | enum | `general` | `general` / `chat` / `code` / `formal` |
| `page_size` | int | 5 | 必须与 format 一致 |
| `max_candidates` | int | 16 | 输出上限 |

## 预算

`sync_us: 60`；最多 32 条。每次调用一次 `pick/` + 一次 `usr/` 扫描、每条候选
一次 `hot/` 与 `last/` 查询。

## 用例

`tests/cases/`：短词加权、上限截断、无候选透传；学习命中、退格撤销、近期选择、
领域词、长词内聚；分页 space/数字提交、连续造词召回、热词更新与清空、
退格回退用户词。
