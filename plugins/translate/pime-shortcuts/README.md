# pime-shortcuts

`translate` 插件：快捷短语与日期/时间模板。

- 码串与 `assets/shortcuts.tsv` 的键**完全相等**时（ASCII 不区分大小写）展开为
  短语；短语里的模板在展开时求值：
  - `{date}` → `2026年9月27日`
  - `{time}` → `08:26`
  - `{weekday}` → `星期日`
  - `{iso}` → `2026-09-27`
- 展开后的短语排在最前，其后接本次链里已有的候选（special / rank / translate），
  所以快捷码不会遮住词典候选。`pime-candidates-format` 优先采用本插件的数组。
- 没有命中时输出空数组，链继续使用上游候选。

## Workspace 读取

| 键 | 说明 |
|---|---|
| `plugin/pime-pinyin-parse/raw` 等 | 当前码串（取第一个非空） |
| `plugin/pime-special-candidates/candidates` | 合并来源 1 |
| `plugin/pime-pinyin-rank/candidates` | 合并来源 2 |
| `plugin/pime-rank-default/candidates` | 合并来源 3 |
| `plugin/pime-pinyin-translate/candidates` | 合并来源 4 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-shortcuts/candidates` | candidate[] | 短语（`source=shortcut`，注释「快捷短语」）＋上游候选 |

## 资产

| 文件 | 格式 | 说明 |
|---|---|---|
| `shortcuts.tsv` | `<码>\t<文本>` | `#` 开头为注释；文本可含模板 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不产候选 |
| `templates` | bool | true | 是否展开 `{date}` 等模板（关闭时原样输出） |
| `max_candidates` | int | 4 | 短语条数上限（另加一条上游候选） |

## 预算

同步调用，`sync_us: 40`。资产 ≤8KB，首次 `process` 懒加载；之后线性扫表。

## 用例

`tests/cases/`：短语、日期模板、时间模板、星期模板、无命中、禁用、模板关闭、
与上游候选合并。
