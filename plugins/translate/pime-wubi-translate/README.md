# pime-wubi-translate

`translate` 插件：读 `plugin/pime-wubi-parse/code`，查包内五笔词表，产出带分数
的候选。

## 三种查询

| 输入 | 行为 | `comment` |
|---|---|---|
| 整码 | 全部命中，按词频降序 | 无 |
| 短码（< 4 且 `completion`） | 整码在前，补全在后 | 剩余编码 |
| `z` + 拼音（`pinyin_lookup`） | 拼音前缀反查 | 该词的完整五笔码 |
| 无命中且码长 ≥ 4 | Damerau–Levenshtein 距离 ≤ 2 的邻居 | `拼写建议` |

补全的分数是 `freq - 200 * 剩余码长`；拼写建议是 `freq - 4000 * 距离`。整码额外
加 10000，所以整码总在补全之前。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-wubi-parse/code` | text | 输入（上游五笔码） |
| `plugin/pime-wubi-translate/candidates` | array<map> | 输出：`{text, score, source, ?comment}` |

`source` 取值：`dict`、`completion`、`pinyin-lookup`、`suggestion`。

消费者：`pime-rank-default`、`pime-candidates-format`。

## 资产

- `assets/wubi.dict`：`<code>\t<word>\t<freq>`，同时用于 `z` 反查的“字 → 码”
  反向表（最多索引 1024 个词条）。
- `assets/pinyin.dict`：`<space separated syllables>\t<word>\t<freq>`。

资产缺失时 `pime_process` 返回 `PIME_NOT_FOUND`，不静默降级。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 8 | 输出上限（1..32） |
| `completion` | bool | true | 短码给补全 |
| `completion_limit` | int | 24 | 补全条数上限（0..32） |
| `corrections` | bool | true | 无命中时给拼写建议 |
| `pinyin_lookup` | bool | true | `z` 开头的拼音反查 |

## 预算

`sync_us: 100`；线性扫描词表（大词表应换预索引二进制）。

## 用例

`tests/cases/`：整码命中、补全、`z` 反查、拼写建议、关闭补全、关闭反查、无匹配。
