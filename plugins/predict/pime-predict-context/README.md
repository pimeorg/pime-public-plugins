# pime-predict-context

`predict` 插件：光标前文本 → 下一词候选，四档来源（对应 WeaveText
`predict.rs`）：

1. **用户二元组**：读 `plugin/pime-learn-user-dict/next`（同一次链调用里，
   `pime-learn-user-dict` 在刚提交的词上写出的后继）。
2. **接续表**：`assets/continuation.tsv`，按光标前 4/3/2/1 字后缀最长匹配。
3. **字符搭配模型**：`assets/collocations.tsv`（`<前字><后字>\t<权重>`），取光标
   前最后一个字的后继，权重越大分越高。
4. **末字续字**：内置常用字表，仅在设置显式打开且前三档无结果时兜底。

联想在 `pime-candidates-format` 之后运行（提交与本次组合的显示由 format 负责），
因此由本插件直接发 `show_candidates` 命令；组合进行中（任一 parse 插件还有非空
`raw`/`code`）不联想、不发命令，避免抢走真正的候选列表。

`handled` 恒为 `false`。

## 事件

| 事件 | 行为 |
|---|---|
| `text` / `key` / `focus` / `candidate` 等同步事件 | 取上下文做四档联想 |
| 组合进行中 | 输出空候选、不发命令 |
| `on_event`（异步） | 输出空 |

## Workspace 读取

| 键 | 说明 |
|---|---|
| `plugin/pime-learn-user-dict/next` | 用户二元组后继 |
| `plugin/pime-learn-user-dict/last` | 无宿主 surrounding 时的上下文兜底 |
| `plugin/pime-pinyin-parse/raw` 等 | 判断组合是否活跃 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-predict-context/candidates` | candidate[] | `source` 为 `user-bigram` / `continuation` / `collocation` / `fallback` |

## 资产

| 文件 | 格式 | 说明 |
|---|---|---|
| `continuation.tsv` | `<前文后缀>\t<下一词>\t<权重>` | 接续表，最长后缀优先 |
| `collocations.tsv` | `<前字><后字>\t<权重>` | 字符搭配表，取 `data/pinyin/collocations.tsv` 前 300 行 |

两个资产缺失时只关闭对应档位，不影响链的其余部分；正式包由构建保证存在。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 5 | 联想条数上限 |
| `user_bigram` | bool | true | 用户二元组档 |
| `continuation` | bool | true | 接续表档 |
| `collocation` | bool | true | 搭配模型档 |
| `fallback` | bool | true | 末字续字档 |
| `show_command` | bool | true | 是否发 `show_candidates` |

## 预算

同步调用，`sync_us: 60`。资产只在首次 `process` 懒加载（continuation ≤8KB，
collocations ≤16KB）；之后每次至多线性扫一遍小表，无动态分配。

## 用例

`tests/cases/`：用户二元组、接续表、搭配模型、末字续字（关闭搭配档）、无上下文、
组合进行中不联想。
