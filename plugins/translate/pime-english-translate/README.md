# pime-english-translate

`translate` 插件：读英文原码，产出英文候选。独立英文链里原码来自
`plugin/pime-english-parse/raw`；挂在中文链时回退到
`plugin/pime-pinyin-parse/raw`，并把英文候选追加到中文候选之后（中英混输）。

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

## 中英混输（挂在中文链时）

中文链里没有 `pime-english-parse`，本插件直接读拼音 parse 的 `raw`：

| 条件 | 行为 |
|---|---|
| `plugin/pime-pinyin-parse/complete == true`（`ni`、`nihao`、`xian`） | 纯拼音：不查英文词表，输出空数组 |
| 原码不足 3 个字符、含非字母/撇号、或没有元音（`go`、`ok`、`nh`、`zsh`） | 不混入英文；两字母还可能是简拼，阈值以上才算一个像样的英文词 |
| 词表里没有以原码开头的词（`nih`、`zhongg`） | 不混入英文；`nih` 只差一个音节就是 `nihao`，不能拿 `big`/`air` 之类的拼写建议冒充补全 |
| 其余（`goo`、`hel`、`dont`、`want`） | 生成英文候选：原码 + 两条补全在前，中文候选紧随其后，其余补全垫底 |

中文链里**不做拼写建议**，只做前缀补全；拼写建议只在独立英文链生效。
`goo` 于是得到 `goo`、`good`、`goods`、`够`、`工`、…、`Google`：先看清楚是
英文，中文候选也不会被挤掉。中文候选保持它们之间的相对顺序；三处都做完全
重复剔除，总数上限 32 条（英文 16 + 中文 16，正好放得下）。英文没有实际新增（只剩原码重复项）时输出空数组，
`pime-candidates-format` 继续使用上游中文候选。`complete` 缺失时（旧版
parse）以 `pime-pinyin-translate` 是否产出 `source=raw` 作为回退信号。

因为展示顺序不再对应 `pime-pinyin-rank` 的下标，rank 对这类输入不记录学习
（见该插件 README）；纯拼音与无元音简拼不受影响。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-english-parse/raw` | text | 英文链输入（原码） |
| `plugin/pime-pinyin-parse/raw` | text | 中文链回退输入 |
| `plugin/pime-pinyin-parse/complete` | bool | 纯拼音判定；缺失时走回退 |
| `plugin/pime-shape-translate/candidates` 等 | array<map> | 中文上游候选，按 format 的优先级取第一个非空 |
| `plugin/pime-english-translate/candidates` | array<map> | 输出：`{text, score, source, ?comment}`；中文链里是“中文 + 英文”合并视图 |

`source` 取值：`raw`、`dict`、`completion`、`suggestion`。

消费者：`pime-rank-default`、`pime-candidates-format`。中文链里 format 优先采用
本插件的合并视图；空数组时回退到中文候选。

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
