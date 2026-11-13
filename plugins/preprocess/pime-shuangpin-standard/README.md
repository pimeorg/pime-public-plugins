# pime-shuangpin-standard

`preprocess` 插件：把两次按键合成一个完整拼音音节，交给 `pime-pinyin-parse`
（parse 不再处理原始按键文本）。

## 键位方案

音节 = 首键（声母）+ 次键（韵母）。`v / i / u` 作为声母时分别表示 `zh / ch / sh`。
零声母音节把首字母当作首键（例如 `aj → an`、`ah → ang`）。

韵母码表（starter 表，`src/plugin.cpp` 的 `kFinals`，可按目标方案调整）：

```text
a→a   b→ou  c→iao d→ai  e→e   f→en  g→eng h→ang
i→i   j→an  k→ao  l→in  m→ian n→iang o→o  p→ie
q→iu  r→uan s→ong t→ue  u→u   v→v   w→ua  x→ia
y→ing z→ei
```

单键元音（`a` / `e` / `o`）连按两次得到该元音。

## 事件

| 事件 | 行为 |
|---|---|
| 字母键（第 1 键） | 记入 state，写 `consumed=true` |
| 字母键（第 2 键） | 合成音节，写 `syllable`，清 state |
| `action=backspace` | 清 state |
| 其他 | 放行 |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-shuangpin-standard/consumed` | bool | 本次按键已被双拼消费 |
| `plugin/pime-shuangpin-standard/syllable` | text | 第 2 键时写入的完整音节 |

消费者：`pime-pinyin-parse` 读取这两个键。`consumed=true` 表示 parse 不要再用
`event.text`。

读取：`plugin/pime-keymap-default/action`（`backspace` 时清空半码）。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `show_half_code` | bool | true | 保留位：半码是否进入预编辑 |

## 预算

`sync_us: 50`；只做两张小表查找。

## 用例

`tests/cases/`：首键存储、两键合成、退格清空。
