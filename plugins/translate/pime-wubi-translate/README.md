# pime-wubi-translate

`translate` 插件：读 `plugin/pime-wubi-parse/code`，查包内五笔词表，产出带分数的
候选。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-wubi-parse/code` | text | 输入（上游五笔码） |
| `plugin/pime-wubi-translate/candidates` | array<map> | 输出：`{text, score, source}` |

`score = freq + 10000`（整码命中时）；前缀命中只取 `freq`。候选按分数降序，最多
`max_candidates` 条。

消费者：`pime-rank-default`、`pime-candidates-format`。

## 资产

`assets/wubi.dict`：`<code>\t<word>\t<freq>`，UTF-8，`#` 起始为注释。资产缺失时
`pime_process` 返回 `PIME_NOT_FOUND`，不静默降级。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 8 | 输出上限（1..32） |

## 预算

`sync_us: 100`；线性扫描 starter 词表（大词表应换预索引二进制）。

## 用例

`tests/cases/`：整码命中、前缀命中、无匹配。
