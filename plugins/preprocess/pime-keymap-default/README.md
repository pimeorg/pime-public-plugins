# pime-keymap-default

`preprocess` 插件：把物理按键事件翻译成链内统一的“动作”，文本键原样放行。
它不产生候选、不改状态，只写 workspace。

## 事件

| 事件 | 行为 |
|---|---|
| `key`（down） | 命中映射表时写 `action` 并 `handled=true`；否则放行 |
| `key`（up / repeat） | 放行 |
| 带 Ctrl / Alt / Meta 的 `key` | 放行（交给宿主快捷键） |
| 其他事件 | 放行 |

按键码：`Backspace=8 / Tab=9 / Enter=13 / Escape=27 / Space=32 / Delete=46`
（Windows virtual-key 与常用 IME 约定一致）。

修饰位：`1=Shift, 2=Ctrl, 4=Alt, 8=Meta`。

## Workspace

写入（短键，core 自动加 `plugin/pime-keymap-default/` 前缀）：

| 键 | 类型 | 值 |
|---|---|---|
| `plugin/pime-keymap-default/action` | text | `backspace` `delete` `enter` `escape` `space` `tab` |

下游 `pime-pinyin-parse` / `pime-wubi-parse` 读取该键处理编辑；`pime-candidates-format`
读取 `space` / `escape` / `enter` 完成选词与取消。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `space_selects_candidate` | bool | true | false 时 Space 不映射，直接放行 |

## 预算

同步调用，`sync_us: 50`。实现只有一次 map 查找和固定大小输出，无分配。

## 用例

`tests/cases/`：Space 映射、字母放行、Ctrl 组合放行、按键抬起放行。
