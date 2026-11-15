# pime-keymap-default

`preprocess` 插件：把物理按键事件翻译成链内统一的“动作”，文本键原样放行。
它不产生候选、不改状态，只写 workspace。

## 事件

| 事件 | 行为 |
|---|---|
| `key`（down） | 命中映射表时写 `action`；否则不写。`handled` 恒为 false |
| `key`（up / repeat） | 放行 |
| 带 Ctrl / Alt / Meta 的 `key` | 放行（交给宿主快捷键） |
| 其他事件 | 放行 |

键位映射**不吞键**：即使写了 `action`，文本/按键仍会由下游决定是否消费；
空组合下的退格、空格必须能回传给编辑器。

按键码：`Backspace=8 / Tab=9 / Enter=13 / Shift=16 / CapsLock=20 / Escape=27 /
Space=32 / PageUp=33 / PageDown=34 / End=35 / Home=36 / Left=37 / Up=38 /
Right=39 / Down=40 / Delete=46 / 0-9=48-57 / Backtick=192`
（Windows virtual-key 与常用 IME 约定一致）。

修饰位：`1=Shift, 2=Ctrl, 4=Alt, 8=Meta`。

## Workspace

写入（短键，core 自动加 `plugin/pime-keymap-default/` 前缀）：

| 键 | 类型 | 值 |
|---|---|---|
| `plugin/pime-keymap-default/action` | text | `backspace` `delete` `enter` `escape` `space` `tab` `shift` `capslock` `backtick` `digit` `left` `right` `up` `down` `pageup` `pagedown` `home` `end` |
| `plugin/pime-keymap-default/digit` | uint | `action=digit` 时给出主键盘数字值（0–9） |

下游 `pime-pinyin-parse` / `pime-wubi-parse` 读取该键处理编辑；`pime-candidates-format`
读取 `space` / `escape` / `enter` / `digit` / 方向键完成选择、分页与取消；
`digit` 动作同时读 `digit` 键值，用于数字选词。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `space_selects_candidate` | bool | true | false 时 Space 不映射，直接放行 |
| `candidate_keys` | bool | true | 是否把方向键、翻页、Home/End 映射为候选动作 |
| `number_selects_candidate` | bool | true | 是否把主键盘 0-9 映射为 `digit` |

## 预算

同步调用，`sync_us: 50`。实现只有一次 map 查找和固定大小输出，无分配。

## 用例

`tests/cases/`：Space、翻页、数字、Shift、字母放行、Ctrl 组合放行、按键抬起放行。
