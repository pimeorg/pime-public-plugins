# pime-t9-parse

`parse` 插件：累积九键（2–9）或 14 键（a–n）键码，交给
`pime-t9-translate` 做歧义解码。

## 键位

| 分组 | 键码 | 字母 |
|---|---|---|
| 九键 | `2`–`9` | `abc` / `def` / `ghi` / `jkl` / `mno` / `pqrs` / `tuv` / `wxyz` |
| 九键 | `1` | 音节分隔符（强制边界，且不重复、不在开头） |
| 14 键 | `a`–`n` | `qw` `er` `ty` `ui` `op` / `as` `df` `gh` `jk` `l` / `zx` `cv` `bn` `m` |

`settings.grouping` 选择分组，默认 `nine`。

## 事件

| 事件 | 行为 |
|---|---|
| `text` / `key` + 字母或数字 | 追加到键码串，最多 `max_codes` |
| `key` + `action=backspace` | 删除最后一位；空串时不置 `handled` |
| `key` + `action=escape` | 清空组合 |
| `key` + `action=space/enter` | 提交阶段：`state` 清空，workspace 保留键码 |
| `candidate` | 同上（提交阶段） |

提交阶段保留 workspace 里的键码，是因为 `translate`/`format` 在同一次链调用里
要靠它定位候选；`state` 同时清空，下一次输入不会接上旧码。

## Workspace

| 方向 | 键 | 类型 |
|---|---|---|
| 读 | `plugin/pime-keymap-default/action` | text |
| 写 | `plugin/pime-t9-parse/raw` | text（含分隔符） |
| 写 | `plugin/pime-t9-parse/input` | text（同 raw） |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `grouping` | text | nine | `nine` 或 `fourteen` |
| `max_codes` | int | 24 | 键码长度上限（1..32） |

## 预算

`sync_us: 30`；只做一次追加与一次写出。

## 用例

`tests/cases/`：九键累积（`94664486`）、分隔符只保留一个、非法键忽略、
退格、提交阶段、14 键（`zxghopbnghghuiop`）。
