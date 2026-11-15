# pime-clip-code

`predict` 插件：短信验证码提取（对应 WeaveText `convert.rs` 验证码路径）。

从光标前后文本和（宿主支持时）`context.clipboard` 里扫描**完整**的 4–8 位数字串，
作为候选输出；数字串不允许是更长数字的一部分（长于 `max_len` 的整段直接跳过）。
附近出现「验证码 / 校验码 / 动态码 / 确认码 / code」只提高分数，不作为硬条件。

`handled` 恒为 `false`。

## 读取

| 来源 | 说明 |
|---|---|
| `context.surrounding_before` / `after` | 光标前后文本 |
| `event.before` / `after` | `surrounding` 事件 |
| `context.clipboard` | 宿主可选提供的剪贴板 |

## Workspace 写入

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-clip-code/candidates` | candidate[] | `source=clip`，注释「验证码」 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不产候选 |
| `min_len` | int | 4 | 最短位数 |
| `max_len` | int | 8 | 最长位数 |
| `max_candidates` | int | 3 | 条数上限 |

## 预算

同步调用，`sync_us: 30`。只做一次线性扫描，无分配。

## 用例

`tests/cases/`：光标前验证码、光标后验证码、剪贴板、过长数字不截断、无数字、禁用。
