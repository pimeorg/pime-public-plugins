# pime-pinyin-translate

`translate` 插件：把原始字母网格映射为候选词与整句。

## 事件

只处理同步 `key` / `text` 事件；不产生命令，只写 workspace。

## 输入

| 键 | 生产者 | 说明 |
|---|---|---|
| `plugin/pime-pinyin-parse/raw` | pime-pinyin-parse | 原始字母串（主输入）；翻译不依赖单一预切分 |
| `plugin/pime-pinyin-parse/syllables` | pime-pinyin-parse | 回退：没有 `raw` 时把各段拼回字母串 |

`pime-pinyin-parse` 仍会输出显示用的切分（完整音节、尾部半音节、单字母），但
translate 只把它当提示，不把它当作唯一的音节边界。

## 匹配

词典键是空格分隔的音节（`wo shi`）。网格的节点是原始字母的每个位置；一个条目
可以从当前位置出发，把它的每个音节依次匹配到一段连续字母上，每段必须是该音节
的前缀：

- **完整**：`wo` ↔ `wo`。
- **半音节**：`zh` ↔ `zhong`，按缺少的字母数罚分。
- **简拼**：一个字母 ↔ 任意以它开头的音节（`w` ↔ `wo/wei/wu`）。
- **模糊音**（`settings.fuzzy`）：`zh/z`、`ch/c`、`sh/s`、`n/l`、`an/ang`、
  `en/eng`、`in/ing`、`ian/iang`、`uan/uang`、`ü/u`，额外罚分。

因为不先固定切分，同一串字母可以同时保留多种解释：`xian` 既能到“先”（完整
音节），也能到“西安”（`xi` + `an`）；`ysh` 既能到“也是”（`y` + `sh`），也能到
“有时候”（`y` + `s` + `h`）；`deg` 既能到“德国”（`de` + `g`），也能到“第二个”
（`d` + `e` + `g`）。哪条路径胜出由下面的打分决定。

## 整句合成

在原始字母网格上做有界束搜索（宽度 12）：词典条目覆盖连续字母，路径按词概率
累加。边按“覆盖的音节数”分桶，每桶保留分数最高的 8 条，避免高频单字把长短语
提前挤掉（`hbg` → `hai bu gou` → 还不够）。分数使用 `log2(词频)`，每个词减去
语料规模常数，再给覆盖字母数更多的词一点内聚奖励：

```
score = log2(freq) - 26500 - 简拼/模糊罚分 + 1500 * (覆盖字母数 - 1)
```

因此“你好”这类整词排在“你 + 和”之前，而 `zh` 仍优先解释为一个半音节（这），
不会被拆成 `z` + `h` 的“之后”；没有整词时也能拼出“我是吗”这类组合。

## 输出

写入 `plugin/pime-pinyin-translate/candidates`：`array<map>`，每项
`{ text: str, score: uint, source: "dict" | "compose" }`，按分数降序，最多
`settings.max_candidates` 条。`source` 为 `compose` 表示由多个词合成。

## 资产

`assets/pinyin.dict`：`<空格分隔音节>\t<候选词>\t<词频>`，UTF-8，`#` 开头为注释。
紧凑词表由 `data/pinyin/pinyin.dict` 同步而来，来源与许可见
`data/pinyin/SOURCES.md`（rime-wanxiang，CC BY 4.0）。

加载时只解析一次：按首字母建立连续桶，并把条目缓存为 `Entry` 表；按键路径不再
线性扫描整个文本文件。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 8 | 输出条数上限（1–32） |
| `fuzzy` | bool | true | 模糊音开关 |

## 预算

`sync_us: 300`。词表约 430 KB / 20k 条；首次调用会解析词表（一次性），之后
按首字母桶匹配并做束搜索。manifest 将 `instructions` 提高到 20M、`time_ms`
提高到 10，覆盖 64 个字母的最坏输入。

## 用例

`tests/cases/`：`nihao`、`ws` 简拼、`nh`、`wsm` 整句、`hbg` → 还不够、
`zongguo` 模糊音/关闭模糊音、半音节前缀、无命中；以及网格保留多重切分的
`ysh`（有时候可见）、`xian`（西安可见）、`deg`（第二个可见）。
