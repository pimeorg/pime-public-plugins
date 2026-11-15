# pime-hans-hant

`translate` 插件：简体 → 繁体（词组优先最长匹配）。

读取链上最优的一份候选数组，逐个候选重写文本；命中转换时输出本插件的
`candidates`，`pime-candidates-format` 优先采用。没有发生任何转换时输出空数组，
format 继续用上游候选——**转换失败绝不遮挡正常候选**。

匹配规则：对每个位置最多尝试 6 个码点的键，长键优先；未命中时原样复制一个码点。

## Workspace 读取

| 键 | 说明 |
|---|---|
| `plugin/pime-shortcuts/candidates` | 合并来源 1 |
| `plugin/pime-special-candidates/candidates` | 合并来源 2 |
| `plugin/pime-pinyin-rank/candidates` | 合并来源 3 |
| `plugin/pime-rank-default/candidates` | 合并来源 4 |
| `plugin/pime-pinyin-translate/candidates` | 合并来源 5 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-hans-hant/candidates` | candidate[] | 转换后的候选（`source=hans-hant`，注释「繁体」） |

## 资产

`assets/hans-hant.tsv`：`<简体>\t<繁体>`，`#` 开头为注释。当前为 starter 规模
（约 660 条常见字/词）；完整 OpenCC 表由 `tools/` 生成，不提交生成物。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | false | 简→繁是可选输出；默认关闭，避免默认链把简体候选整体改写成繁体 |
| `comment` | bool | true | 是否加「繁体」注释 |
| `max_candidates` | int | 8 | 转换条数上限 |

## 预算

同步调用，`sync_us: 50`。表 ≤16KB，首次 `process` 懒加载；每个码点最多 6 次
线性查表，候选数受限。

## 用例

`tests/cases/`：单字、词组、混合文本、无候选、无需转换、禁用、多候选顺序。
