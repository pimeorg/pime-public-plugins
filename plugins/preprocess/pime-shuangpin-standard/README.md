# pime-shuangpin-standard

`preprocess` 插件：把两次按键合成一个完整拼音音节，交给 `pime-pinyin-parse`
（parse 不再处理原始按键文本）。

## 键位方案

音节 = 首键（声母）+ 次键（韵母）。`v / i / u` 作为声母时分别表示
`zh / ch / sh`。支持四套方案，`settings.scheme` 切换：

| 方案 | 零声母 | `ing` 键 | 备注 |
|---|---|---|---|
| `xiaohe`（默认） | 单韵母双击、双字母原样、三字母首字母+韵母键 | `k` | `l` = iang/uang |
| `ziranma` | 同上 | `y` | `y` = uai/ing |
| `microsoft` | `o` + 韵母键，另收 `a/e` 首字母与 `ou` 原样 | `;` | `y` 兼作 `ü` |
| `sogou` | 同上 | `;` | `v` 只作 `ui` |

`j/q/x/y` 后的 `ü` 同时接受 `u` 与 `v` 键。韵母表见
`src/plugin.cpp` 的 `kXiaohe/kZiranma/kMicrosoft/kSogou`。

## 事件

| 事件 | 行为 |
|---|---|
| 字母键/`;`（第 1 键） | 记入 state，写 `consumed=true` |
| 字母键/`;`（第 2 键） | 合成音节，写 `syllable`，清 state |
| `action=backspace/escape` | 有半码时清 state 并消费；否则放行 |
| `action=space/enter`、选词事件 | 把半码作为前缀音节提交 |
| 其他 | 放行 |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-shuangpin-standard/consumed` | bool | 本次按键已被双拼消费 |
| `plugin/pime-shuangpin-standard/syllable` | text | 合成出的完整音节 |

消费者：`pime-pinyin-parse` 读取这两个键。`consumed=true` 表示 parse 不要再用
`event.text`。

读取：`plugin/pime-keymap-default/action`（退格/空格/回车时使用）。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `scheme` | enum | `xiaohe` | `xiaohe` / `ziranma` / `microsoft` / `sogou` |

## 预算

`sync_us: 50`；只做几张固定小表查找，无分配。

## 用例

`tests/cases/`：四方案合成、`;`→ing、零声母、退格清空、空格提交半码。
