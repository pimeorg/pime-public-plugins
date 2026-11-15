# pime-personal-merge

`service` 插件：个人资料跨设备合并（对应 WeaveText `personal.rs` 的 Lamport 版本）。

- 条目存储为 `entry/<key>` = `<lamport>\x1f<value>`。
- 合并规则：Lamport 更大者胜；相同时按值字节序取较大者，保证两端收敛，并记一次
  冲突。本地较新时保留本地值。
- 事件 `{ type: "personal.merge", entries: [ {key, value, lamport} ] }` 由宿主或云
  服务喂入下载数据；本插件**只合并，不上传**。

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-personal-merge/merged` | uint | 本次采纳的条目数 |
| `plugin/pime-personal-merge/conflicts` | uint | Lamport 相同但值不同的次数 |
| `plugin/pime-personal-merge/total` | uint | 合并后条目总数 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不合并 |
| `max_entries` | int | 32 | 单次事件处理上限 |

## 用例

`tests/cases/`：新条目、旧版本忽略、新版本覆盖、相同 Lamport 冲突、无事件、禁用。
