# pime-wubi-parse

`parse` 插件：把按键累积成五笔 86 字母码，并负责组合串的生命周期；候选、补全
与反查交给 `pime-wubi-translate`。

## 为什么 parse 读词表

两条顶屏规则必须在**同一帧**清空组合状态，而只有持有状态的插件能做到这一点：

- **四码唯一顶屏**：满四码且只有一个整码命中时直接上屏。
- **满码再敲**：码已满时再敲一个字母，先把当前码的首选上屏（无命中则丢弃），
  然后用新字母开始下一段组合。

因此 parse 需要知道“这个码有几个命中、首选是谁”，于是自己读一份
`wubi.dict`（与 translate 同一份数据，各自打包）。translate 仍然负责候选排序、
补全、`z` 反查与拼写建议。

## 事件

| 事件 | 行为 |
|---|---|
| `key` / `text` 字母 | 追加小写字母 |
| `action=backspace` | 删除最后一个字母；空码时交回编辑器删除 |
| `action=escape` | 清空并隐藏候选 |
| `action=space` / `enter` / `candidate` | 结束组合，保留 `code` 供 format 取候选 |
| 其他 | 不改动 |

`z` 开头且 `pinyin_lookup` 打开时进入反查模式：码可以超过 `max_code`，最长
`max_pinyin`，且不触发顶屏。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-wubi-parse/code` | text | 当前五笔码；顶屏后为空 |

读取：`plugin/pime-keymap-default/action`（`backspace` / `escape` /
`space` / `enter`）。

消费者：`pime-wubi-translate`、`pime-candidates-format`。

## 资产

`assets/wubi.dict`：`<code>\t<word>\t<freq>`。缺失时顶屏判断退化为“无命中”，
不写 `code` 以外的任何键。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_code` | int | 4 | 码长上限（1..16） |
| `auto_commit` | bool | true | 四码唯一顶屏与满码再敲 |
| `pinyin_lookup` | bool | true | `z` 开头的拼音反查 |
| `max_pinyin` | int | 16 | 反查码长上限（1..16） |

## 预算

`sync_us: 30`；词表只在首次顶屏判断时读入，之后线性扫描。

## 用例

`tests/cases/`：累积、退格、清空、满码重启、四码唯一顶屏、满码再敲、
`z` 反查码长、关闭顶屏。
