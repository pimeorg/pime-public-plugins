# pime-app-mode

`learn` 插件：按前台应用记住中/英输入模式（对应 WeaveText `session.rs`）。

模式推断只看当前链里活跃的 parse 插件：

| 观察到 | 模式 |
|---|---|
| `plugin/pime-english-parse/raw` 非空 | `en` |
| `plugin/pime-pinyin-parse/raw` 非空 | `zh` |
| `plugin/pime-wubi-parse/code` 非空 | `zh` |
| `plugin/pime-cangjie-parse/code` 非空 | `zh` |
| `plugin/pime-t9-parse/raw` 非空 | `zh` |

`focus` 或带 app id 的 `context` 事件直接用存储里的模式回答；一次按键推断出的
新语言会覆盖并写回存储。插件不产生候选、不消费按键，`handled` 恒为 `false`。

## Workspace 读取

| 键 | 说明 |
|---|---|
| `plugin/pime-english-parse/raw` 等 | 判断刚输入的语言 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-app-mode/mode` | text | `zh` 或 `en` |
| `plugin/pime-app-mode/app` | text | 当前 app id（无则空串） |

## 存储布局（`store.kv`）

| 键 | 值 |
|---|---|
| `app/<app-id>` | `zh` / `en` |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后只回默认模式 |
| `remember` | bool | true | 是否把观察结果写回存储 |
| `observe` | bool | true | 是否用当前输入覆盖已记住的模式 |
| `default_mode` | enum | zh | 没有记录时的模式 |

## 预算

同步调用，`sync_us: 30`。一次调用最多一次 KV 读 + 一次 KV 写，无分配。

## 用例

`tests/cases/`：观察英文、focus 恢复、观察中文、默认模式、语言切换、禁用。
