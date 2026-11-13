# pime-punctuation-zh

`preprocess`（Lua）：把半角标点在提交前替换为中文全角标点。

## 行为

- 仅处理 `event.type = text` / `key`，且无修饰键。
- keymap 已识别为 `space / enter / escape / backspace` 时放行。
- 命中映射表后输出一条 `commit` 命令，`handled = true`。

| 输入 | 输出 |
|---|---|
| `,` `.` `?` `!` `:` `;` | `，` `。` `？` `！` `：` `；` |
| `(` `)` `\` `~` | `（` `）` `、` `～` |
| `"` `'`（`quotes=chinese`） | `”` `’` |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后完全放行 |
| `quotes` | enum | `chinese` | `chinese` 转中文引号；`straight` 保持原样 |

## 用例

`tests/cases/`：逗号转全角、空格放行、关引号样式。
