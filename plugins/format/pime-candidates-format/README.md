# pime-candidates-format

`format` 插件：链的最后一段，把 workspace 里的候选变成宿主命令。

## 事件

| 事件 | 行为 |
|---|---|
| `key` + `action=space` | 提交当前页第一个候选，发 `commit` |
| `key` + `action=enter` | `commit_on_enter` 时提交第一个候选 |
| `key` + `action=digit` | 选 `page * page_size + digit - 1` |
| `key` + `action=pagedown/down` | 下一页（`end` 跳末页） |
| `key` + `action=pageup/up` | 上一页（`home` 回首页） |
| `key` + `action=escape` | `hide_candidates` |
| `candidate {index}` | 提交第 `index` 个候选 |
| 其他 | 只展示候选 |

候选按顺序从以下 workspace 键读取，取第一个非空数组：
`plugin/pime-shape-translate/candidates`、`plugin/pime-shortcuts/candidates`、`plugin/pime-hans-hant/candidates`、
`plugin/pime-special-candidates/candidates`、`plugin/pime-pinyin-rank/candidates`、
`plugin/pime-rank-default/candidates`、`plugin/pime-pinyin-translate/candidates`、
`plugin/pime-wubi-translate/candidates`、
`plugin/pime-cangjie-translate/candidates`、`plugin/pime-t9-translate/candidates`、
`plugin/pime-english-translate/candidates`、`plugin/pime-emoji-suggest/candidates`、
`plugin/pime-clip-code/candidates`、`plugin/pime-predict-context/candidates`、
`plugin/pime-handwriting-ncnn/candidates`、`plugin/pime-speech-ncnn/candidates`。

`pime-shortcuts` 与 `pime-hans-hant` 排在最前：两者都输出**合并视图**（短语/转换结果
＋上游候选），优先采用不会丢掉词典候选。`pime-emoji-suggest` / `pime-clip-code` /
`pime-predict-context` 排在词典源之后，只在组合结束、词典源为空时才生效。

## 读取的 Workspace

| 键 | 生产者 |
|---|---|
| `plugin/pime-keymap-default/action` | pime-keymap-default |
| `plugin/pime-pinyin-parse/raw` | pime-pinyin-parse |
| `plugin/pime-wubi-parse/code` | pime-wubi-parse |
| `plugin/pime-cangjie-parse/code` | pime-cangjie-parse |
| `plugin/pime-t9-parse/raw` | pime-t9-parse |
| `plugin/pime-english-parse/raw` | pime-english-parse |
| `plugin/pime-shape-translate/candidates` | pime-shape-translate |
| `plugin/pime-shortcuts/candidates` | pime-shortcuts |
| `plugin/pime-hans-hant/candidates` | pime-hans-hant |
| `plugin/pime-special-candidates/candidates` | pime-special-candidates |
| `plugin/pime-pinyin-rank/candidates` | pime-pinyin-rank |
| `plugin/pime-rank-default/candidates` | pime-rank-default |
| `plugin/pime-pinyin-translate/candidates` | pime-pinyin-translate |
| `plugin/pime-wubi-translate/candidates` | pime-wubi-translate |
| `plugin/pime-cangjie-translate/candidates` | pime-cangjie-translate |
| `plugin/pime-t9-translate/candidates` | pime-t9-translate |
| `plugin/pime-english-translate/candidates` | pime-english-translate |
| `plugin/pime-emoji-suggest/candidates` | pime-emoji-suggest |
| `plugin/pime-clip-code/candidates` | pime-clip-code |
| `plugin/pime-predict-context/candidates` | pime-predict-context |
| `plugin/pime-handwriting-ncnn/candidates` | pime-handwriting-ncnn |
| `plugin/pime-speech-ncnn/candidates` | pime-speech-ncnn |

## 写入的 Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-candidates-format/committed` | text | 本次提交的文本；仅提交时写 |

`pime-learn-frequency` / `pime-user-dict` 消费该键。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `page_size` | int | 5 | context 未给 `page_size` 时的每页条数（1..9） |
| `commit_on_enter` | bool | true | Enter 是否提交首候选 |
| `commit_raw` | bool | true | 没有候选时用原码上屏（英文透传） |
| `show_labels` | bool | true | 候选前显示 1..9 选词键 |
| `show_comments` | bool | true | 显示 `comment` / `pinyin` 注释 |

## 预算

`sync_us: 50`；只遍历最多 `candidates_max` 条候选。

## 用例

`tests/cases/`：空格提交首候选、Esc 隐藏、翻页、数字选词、候选事件选词、
原码提交与关闭原码提交。
