# pime-cangjie-translate

`translate` 插件：读 `plugin/pime-cangjie-parse/code`，查包内仓颉码表，产出带
分数的候选。

## 查询

| 输入 | 行为 | `comment` |
|---|---|---|
| 整码 | 全部命中，按词频降序 | 无 |
| 短码且 `completion` | 整码在前，补全在后 | 剩余编码 |

补全分数是 `freq - 200 * 剩余码长`，整码额外加 10000，所以整码总在补全之前。
码长上限由 `pime-cangjie-parse` 的 `max_code` 决定，本插件不做截断。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-cangjie-parse/code` | text | 输入（上游仓颉码） |
| `plugin/pime-cangjie-translate/candidates` | array<map> | 输出：`{text, score, source, ?comment}` |

`source` 取值：`dict`、`completion`。

消费者：`pime-rank-default`、`pime-candidates-format`。

## 资产

`assets/cangjie.dict`：`<code>\t<word>\t<freq>`，UTF-8，`#` 起始为注释。缺失时
`pime_process` 返回 `PIME_NOT_FOUND`，不静默降级。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 8 | 输出上限（1..32） |
| `completion` | bool | true | 短码给补全 |
| `completion_limit` | int | 24 | 补全条数上限（0..32） |

## 预算

`sync_us: 100`；线性扫描 starter 码表（大码表应换预索引二进制）。

## 用例

`tests/cases/`：整码命中、补全注释、关闭补全、无匹配、码长上限、多候选排序。
