# pime-learn-user-dict

`learn` 插件：跨词的用户学习。它从 `pime-candidates-format` 的提交结果观察
选择，维护**追加式日志、计数衰减、近期选择、退格撤销、连续造词、用户二元组**，
并为下一词联想提供 `next`。

与 `pime-pinyin-rank` 的分工：

- `pime-pinyin-rank`：同一次组合内的学习（`pick/<raw>`、最近选择加分、退格删除
  刚学会的候选）。它必须在下游格式化之前运行，才能影响本次候选排序。
- 本插件：跨组合、跨词的统计。它必须在上游格式化之后运行，才能看到
  `committed`；它产出的 `candidates` 供同一次链中其后的插件使用，`next` 供
  `pime-predict-context` 生成联想。

## 事件

| 事件 | 行为 |
|---|---|
| 任意事件（带 `plugin/pime-candidates-format/committed`） | 记录一次提交：计数、日志、二元组、连续造词、MRU |
| `key` 且 `plugin/pime-keymap-default/action == backspace` | 在 `undo_window_ms` 内撤销上一次提交的计数，并写日志墓碑 |
| 其他事件 | 只做查询：按当前 raw 取回学过的词，按 last 生成 `next` |

`handled` 恒为 `false`：学习插件不改按键语义。

## Workspace 读取

| 键 | 说明 |
|---|---|
| `plugin/pime-candidates-format/committed` | 本次调用提交的文本 |
| `plugin/pime-keymap-default/action` | `backspace` 触发撤销 |
| `plugin/pime-pinyin-parse/raw` | 当前码串（也接受五笔/仓颉/九键/英文的 `raw`/`code`） |
| `plugin/pime-special-candidates/candidates` | 合并来源 1 |
| `plugin/pime-pinyin-rank/candidates` | 合并来源 2 |
| `plugin/pime-rank-default/candidates` | 合并来源 3 |
| `plugin/pime-pinyin-translate/candidates` | 合并来源 4 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-learn-user-dict/candidates` | candidate[] | 当前码串学过的词（`source=user`，注释「自造」）＋原有候选 |
| `plugin/pime-learn-user-dict/next` | candidate[] | 上次提交词的二元组后继（`source=user-bigram`） |
| `plugin/pime-learn-user-dict/recent` | text[] | 最近提交的 MRU 词表 |
| `plugin/pime-learn-user-dict/last` | text | 最近一次提交的词 |

## 存储布局（`store.kv` 字符串）

计数一律写成 `<count>:<day>`，`day` 为本地日序号；读取时按 `decay_days` 每过
一个周期折半，最低为 1。

| 键 | 值 | 说明 |
|---|---|---|
| `word/<word>` | `<count>:<day>` | 词频 |
| `pair/<raw>\x1f<word>` | `<count>:<day>` | 码串 → 词（重打同一码串时取回） |
| `next/<word1>\x1f<word2>` | `<count>:<day>` | 用户二元组 |
| `log/<seq>` | `<ts>|<tag>|<raw>|<word>` | 追加式日志，`tag` 为 `commit` / `new` / `undo` |

日志只追加：撤销写 `undo` 墓碑，不重写历史。`seq` 达到 `65535` 后停止追加，
避免无界增长；已有计数继续更新。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不记录、不查询，只回空结果 |
| `decay_days` | int | 30 | 衰减周期（天）；0 关闭衰减 |
| `undo_window_ms` | int | 10000 | 提交后多久内的退格可撤销 |
| `max_candidates` | int | 8 | `candidates` / `next` 上限 |
| `max_recent` | int | 8 | MRU 上限 |

## 预算

同步调用，`sync_us: 60`。一次提交最多 4 次 KV 写 + 1~2 次日志写；查询是一次
`scan`。无动态分配，全部使用固定大小缓冲。

## 用例

`tests/cases/`：提交后重打取回、二元组后继、连续造词、退格撤销、MRU、禁用、
与上游候选合并去重。
