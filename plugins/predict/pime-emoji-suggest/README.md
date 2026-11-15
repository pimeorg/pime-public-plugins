# pime-emoji-suggest

`predict` 插件：按光标前文本的末词联想表情（对应 WeaveText `convert.rs`
表情/颜文字路径）。

取光标前文本（没有 surrounding 时退回 `pime-learn-user-dict/last`），在
`assets/emoji.tsv` 上按 4/3/2/1 字后缀最长匹配，命中即输出表情候选。

`handled` 恒为 `false`。

## 读取

| 来源 | 说明 |
|---|---|
| `context.surrounding_before` / `event.before` | 光标前文本 |
| `plugin/pime-learn-user-dict/last` | 无 surrounding 时的末词兜底 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-emoji-suggest/candidates` | candidate[] | `source=emoji`，注释「表情」 |

## 资产

`assets/emoji.tsv`：`<词>\t<表情>`，`#` 开头为注释。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不产候选 |
| `max_candidates` | int | 3 | 条数上限 |

## 预算

同步调用，`sync_us: 40`。表 ≤8KB，首次 `process` 懒加载，之后线性扫表。

## 用例

`tests/cases/`：末词命中、长词后缀命中、无命中、learn 兜底、禁用。
