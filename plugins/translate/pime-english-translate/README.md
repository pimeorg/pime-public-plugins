# pime-english-translate

`translate` 插件：读 `plugin/pime-english-parse/raw`，产出英文候选。

## 查询

| 步骤 | 行为 |
|---|---|
| 原码 | 输入原文永远排第一（`source=raw`），空格/回车/标点不会把它换成别的词 |
| 前缀补全 | 去掉撇号、转小写后做前缀匹配，`source=completion` |
| 拼写建议 | 没有任何前缀命中时，给 Damerau–Levenshtein 距离 ≤ 2 的词，注释“拼写建议” |

补全分数是 `freq - 350 * 多出的字母数`，整码额外加 10000。候选文字按输入大小写
回写：全大写保持全大写，首字母大写则补全也首字母大写（`GitHub`、`WiFi`、
`API` 这类词条保留自身写法）。

`dont` 不会自动变成 `don't`——原码仍在首位，`don't` 作为候选可主动选择。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-english-parse/raw` | text | 输入（原码） |
| `plugin/pime-english-translate/candidates` | array<map> | 输出：`{text, score, source, ?comment}` |

`source` 取值：`raw`、`dict`、`completion`、`suggestion`。

消费者：`pime-rank-default`、`pime-candidates-format`。

## 资产

`assets/english.dict`：`<word>\t<word>\t<freq>`，键为小写。缺失时
`pime_process` 返回 `PIME_NOT_FOUND`，不静默降级。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 8 | 输出上限（1..32） |
| `completion` | bool | true | 前缀补全 |
| `completion_limit` | int | 24 | 补全条数上限（0..32） |
| `corrections` | bool | true | 无命中时给拼写建议 |

## 预算

`sync_us: 100`；线性扫描词表（大词表应换预索引二进制）。

## 用例

`tests/cases/`：原码优先、补全、全大写、首字母大写、拼写建议、撇号词、
技术词大小写、关闭补全、空输入。
