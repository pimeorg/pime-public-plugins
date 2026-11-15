# pime-shape-translate

`translate` 插件：拆字查字与字形筛选（对应 WeaveText `weave-dict/shape.rs`
的查表部分）。

- **shape 模式**：`code` 是部件拼音序列。整码命中优先（`source=shape`，注释
  「拆字」）；码是某条目的前缀时列出补全，注释「补全+剩余码」。
- **glyph 模式**：`code` 非空时作为字形组名（`` `mu `` → 木林森树）；`code` 为空时
  取当前最优候选首字的字形组（`` ` `` 在「林」之后列出木组字）。

## Workspace 读取

| 键 | 说明 |
|---|---|
| `plugin/pime-shape-parse/mode` | `shape` / `glyph` |
| `plugin/pime-shape-parse/code` | 去前缀后的码串 |
| `plugin/pime-shortcuts/candidates` | glyph 模式首字来源 1 |
| `plugin/pime-hans-hant/candidates` | glyph 模式首字来源 2 |
| `plugin/pime-special-candidates/candidates` | glyph 模式首字来源 3 |
| `plugin/pime-pinyin-rank/candidates` | glyph 模式首字来源 4 |
| `plugin/pime-rank-default/candidates` | glyph 模式首字来源 5 |
| `plugin/pime-pinyin-translate/candidates` | glyph 模式首字来源 6 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-shape-translate/candidates` | candidate[] | `source=shape` / `glyph` |

## 资产

| 文件 | 格式 | 说明 |
|---|---|---|
| `shape.tsv` | `<部件拼音>\t<字>` | starter 拆字表（常用部件与组合） |
| `glyphs.tsv` | `<组名>\t<同组字>` | starter 字形组表 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不查表 |
| `max_candidates` | int | 8 | 条数上限 |

## 预算

同步调用，`sync_us: 50`。两个资产 ≤12KB，首次 `process` 懒加载，之后线性扫表。

## 用例

`tests/cases/`：整码、前缀补全、按组名筛选、按首字筛选、无模式、未知组、禁用。
