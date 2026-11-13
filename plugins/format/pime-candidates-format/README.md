# pime-candidates-format

`format` 插件：链的最后一段，把 workspace 里的候选变成宿主命令。

## 事件

| 事件 | 行为 |
|---|---|
| `key` + `action=space` | 提交第一个候选，发 `commit` |
| `key` + `action=enter` | `commit_on_enter` 时提交第一个候选 |
| `key` + `action=escape` | `hide_candidates` |
| `candidate {index}` | 提交第 `index` 个候选 |
| 其他 | 只展示候选 |

候选按顺序从以下 workspace 键读取，取第一个非空数组：
`plugin/pime-pinyin-rank/candidates`、`plugin/pime-rank-default/candidates`、
`plugin/pime-pinyin-translate/candidates`、`plugin/pime-wubi-translate/candidates`、
`plugin/pime-predict-context/candidates`、`plugin/pime-handwriting-ncnn/candidates`、
`plugin/pime-speech-ncnn/candidates`。

## 读取的 Workspace

| 键 | 生产者 |
|---|---|
| `plugin/pime-keymap-default/action` | pime-keymap-default |
| `plugin/pime-pinyin-parse/raw` | pime-pinyin-parse |
| `plugin/pime-wubi-parse/code` | pime-wubi-parse |
| `plugin/pime-cangjie-parse/code` | pime-cangjie-parse |
| `plugin/pime-pinyin-rank/candidates` | pime-pinyin-rank |
| `plugin/pime-rank-default/candidates` | pime-rank-default |
| `plugin/pime-pinyin-translate/candidates` | pime-pinyin-translate |
| `plugin/pime-wubi-translate/candidates` | pime-wubi-translate |
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
| `page_size` | int | 5 | context 未给 `page_size` 时的展示条数 |
| `commit_on_enter` | bool | true | Enter 是否提交首候选 |

## 预算

`sync_us: 50`；只遍历最多 `candidates_max` 条候选。

## 用例

`tests/cases/`：空格提交首候选、Esc 隐藏、方向数字选词（candidate 事件）。
