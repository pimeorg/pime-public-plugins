# pime-dict-packs

`service` 插件：专业词库包按需启停（对应 WeaveText `dictpacks.json` 的 14 个包）。

- 默认状态来自 `assets/packs.tsv` 第三列；用户开关写入 `store.kv` 的 `pack/<id>`，
  重启后保持。
- `settings.disabled` 可在部署时强制关闭一组包（逗号分隔），优先级高于存储。
- 事件 `{ type: "dictpack", id, enabled }` 切换单个包；其他事件只回报状态。

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-dict-packs/active` | text[] | 当前启用的包 id |
| `plugin/pime-dict-packs/count` | uint | 启用数量 |
| `plugin/pime-dict-packs/changed` | text | 本次切换的包 id（无则空） |
| `plugin/pime-dict-packs/enabled` | bool | 切换后的状态 |

## 资产

`assets/packs.tsv`：`<id>\t<名称>\t<默认启用 1/0>`。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不接受切换 |
| `disabled` | string | "" | 强制关闭的包 id，逗号分隔 |

## 用例

`tests/cases/`：默认启用、部署强制关闭、切换、未知包、禁用、跨调用持久化。
