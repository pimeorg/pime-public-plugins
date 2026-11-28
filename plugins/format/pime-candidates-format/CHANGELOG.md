# Changelog

## 1.5.1

- 翻页键别名：keymap 把 `-`/`=` 与数字键盘 `-`/`+` 映射为 `pageup`/`pagedown`。
- v 模式表达式里 `-`/`+`/`=` 仍是运算符：组合以 `v` 开头时这些键不翻页。

## 1.3.3

- 候选来源顺序把 `pime-english-translate` 提到 `pime-hans-hant` 之前：
  中文链里它是“中文候选 + 英文补全”的合并视图；空数组时照旧回退。

## 1.1.0

- 候选注释：透传 `comment`，缺失时回退 `pinyin`。
- 选择标签：每页 1–9 的 `label`，可关闭。
- 分页：state `{page}` 跨调用保留，PageUp/PageDown/方向键/Home/End。
- 数字选词：读取 keymap 的 `digit` 值，按页内偏移选择。
- 原码提交：无候选时空格/回车直接提交组合原文（`commit_raw`）。
- 扩展候选来源列表，预留给 T9/英文/特殊候选/快捷短语。

## 1.0.0

- 首个版本：候选展示、空格/Enter 提交首候选、Esc 隐藏、candidate 事件选词。
