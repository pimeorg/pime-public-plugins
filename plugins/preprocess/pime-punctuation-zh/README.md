# pime-punctuation-zh

`preprocess`（WASM）：把半角标点在提交前替换为中文标点。第二版由 Lua 改为 WASM，
行为由 native 模拟器用例覆盖。

## 行为

- 处理 `event.type = text` / `key`；`Ctrl` / `Alt` / `Meta` 放行（交给宿主），
  `Shift` 允许（`?` `:` 等需要 Shift）。
- keymap 已识别为 `space / enter / escape / backspace / tab / delete` 时放行。
- 命中映射表后输出一条 `commit` 命令，`handled = true`。

| 输入 | 输出 |
|---|---|
| `,` `.` `?` `!` `:` `;` | `，` `。` `？` `！` `：` `；` |
| `(` `)` `[` `]` | `（` `）` `【` `】` |
| `{` `}` | 默认 `｛` `｝`；`braces=corner` 时 `「` `」` |
| `\` `~` `@` `#` `$` `%` `^` `&` `*` | `、` `～` `＠` `＃` `￥` `％` `＾` `＆` `＊` |
| `-` `_` `+` `=` `\|` `/` `` ` `` | `－` `＿` `＋` `＝` `｜` `／` `｀` |
| `<` `>` | `《` `》` |
| `^` `_`（`multi_char=true`） | `……` `——` |
| `"` `'`（`quotes=chinese`） | 交替 `“` `”` / `‘` `’` |
| `"` `'`（`quotes=straight`） | 原样放行 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后完全放行 |
| `quotes` | enum | `chinese` | `chinese` 交替中文引号；`straight` 保持原样 |
| `braces` | enum | `fullwidth` | `{ }` 映射为全角花括号或 `「」` 角括号 |
| `multi_char` | bool | false | `^` → `……`，`_` → `——` |

## State

| 键 | 类型 | 说明 |
|---|---|---|
| `quote_open` | bool | 下一个中文引号是否为开引号 |

## 已知边界

“自动补全括号并把光标放到中间”需要编辑器光标移动命令；pime 命令 ABI 只有
`commit` / `set_composition`（`set_composition` 带光标但会留下未提交的组合串），
没有“提交后移动光标”的原语，因此本插件不做近似插入。成对标点以**成对映射**
（`（）` `【】` `《》` `「」`）形式提供。

## 预算

同步调用，`sync_us: 30`。一次查表 + 固定大小输出，无分配。

## 用例

`tests/cases/`：逗号、放行、直引号、引号交替/闭合、单引号、方括号、符号、
角括号、全角花括号、多字符省略号/破折号、修饰键放行、退格动作放行、key 事件。
