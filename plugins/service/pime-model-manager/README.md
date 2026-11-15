# pime-model-manager

`service` 插件：语音 / 手写 / 语言模型目录、启停与声明哈希校验。

- 目录来自 `assets/models.tsv`：`<id>\t<engine>\t<path>\t<声明哈希>\t<默认启用>`。
- 启停写入 `model/<id>`；`settings.disabled` 可部署期强制关闭。
- 校验状态写入 `status/<id>`（`ok` / `mismatch`）：宿主用 `model.verify { id, hash }`
  传入实际哈希，与目录里的声明值比对。**真正的权重校验仍由 core 在 `ml.infer`
  加载时完成**，本插件只维护目录与状态。

## 事件

| 事件 | 行为 |
|---|---|
| `model.enable {id, enabled}` | 切换启停，回报 `changed` / `status` |
| `model.verify {id, hash}` | 比对声明哈希，回报 `status` (`ok` / `mismatch`) |
| 其他 | 只回报目录 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-model-manager/models` | map[] | `{id, engine, hash, enabled, status, path}` |
| `plugin/pime-model-manager/count` | uint | 启用数量 |
| `plugin/pime-model-manager/changed` | text | 本次变更的模型 id |
| `plugin/pime-model-manager/status` | text | `ok` / `mismatch` / `enabled` / `disabled` / 空 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不接受变更事件 |
| `disabled` | string | "" | 强制关闭的模型 id，逗号分隔 |

## 用例

`tests/cases/`：默认目录、部署强制关闭、启停、哈希一致、哈希不一致、禁用。
