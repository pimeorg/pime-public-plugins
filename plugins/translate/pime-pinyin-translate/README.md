# pime-pinyin-translate

`translate` 插件：把原始字母网格映射为候选词与整句。

## 事件

只处理同步 `key` / `text` 事件；不产生命令，只写 workspace。

## 输入

| 键 | 生产者 | 说明 |
|---|---|---|
| `plugin/pime-pinyin-parse/raw` | pime-pinyin-parse | 用户输入的原始字母串（主输入）；翻译不依赖单一预切分 |
| `plugin/pime-pinyin-parse/corrected` | pime-pinyin-parse | 纠错读法（可选）；与字面读法比较后择优 |
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

在原始字母网格上做有界束搜索（宽度 16）：词典条目覆盖连续字母，路径按词概率
累加。边按“覆盖的音节数”分桶，每桶保留分数最高的 24 条，避免高频单字把长短语
提前挤掉（`hbg` → `hai bu gou` → 还不够）。分数使用 `log2(词频)`，每个词减去
语料规模常数，再给覆盖字母数更多的词一点内聚奖励：

```
score = log2(freq) - 26500 - 简拼/模糊罚分 + 9000 * (覆盖字母数 - 1)
```

内聚奖励按“字母”而不是“音节”增长，因此少用词、长词的路径优先：`dd` 会给出
“等等”而不是“的 + 的”，`srkl` 会给出“生日快乐”；同时 `zh` 仍优先解释为一个
半音节（这），不会被拆成 `z` + `h` 的“之后”。没有整词时也能拼出“我是中国人”
这类多词组合。

## 字符搭配与次优整句

词与词的接缝处再加一层字符二元搭配分：路径最后一个字与下一个词的第一个字
在 `pinyin.collocations` 中有记录时，按搭配词频给奖励（`collocation_weight`
缩放，封顶 9000，不会压过整词内聚奖励）。这样“你好 + 嘛”会比“年 + 好吗”
更自然地排到前面；把 `collocation_weight` 设为 `0` 即可退回纯词频模型。

束搜索的第二条多词路径标记为 `source: "sentence"`，与普通 `compose` 区分，
UI 可以把它当作“次优整句”单独提供。

## 输出

写入 `plugin/pime-pinyin-translate/candidates`：`array<map>`，每项
`{ text: str, score: uint, source: str, words: uint }`，按分数降序，最多
`settings.max_candidates` 条。`source` 取值：

| 值 | 含义 |
|---|---|
| `dict` | 单个词典词 |
| `compose` | 多个词合成的整句 |
| `sentence` | 次优整句（第二条多词路径） |
| `raw` | 原字母透传（英文/非拼音输入） |
| `corrected` | 纠正读法的候选（仅在纠正胜出时出现） |

纠正读法只有在比字面读法高 `3000` 分以上才取代字面读法；取代时仍会把字面
首选附在末尾（`source=raw`/原 source），用户依然可以选回自己输入的那个读法。

`words` 是路径包含的词数，供 rank/format 判断整句与词组。

当输入不是完整拼音串（如 `hello`、`iiii`）且 `english_passthrough` 开启时，
原字母作为最后一条候选追加，并为其预留一个名额，保证英文可以直接上屏；
纯拼音输入不会出现这种候选。

## 资产

`assets/pinyin.dict`：`<空格分隔音节>\t<候选词>\t<词频>`，UTF-8，`#` 开头为注释。
紧凑词表由 `data/pinyin/pinyin.dict` 同步而来，其中 rime-wanxiang 的基础词表
（CC BY 4.0）再合并 `data/pinyin/extra.txt` 的常用短语；补充表也承担无上下文
时的排序选择（`zsh` → 早上好、`gz` → 广州、`sz` → 深圳、句末“吧”，以及
“我在家/一起去吧/一起吃饭吧”短语）。来源、合并规则与许可见
`data/pinyin/SOURCES.md`。

加载时只解析一次：按首字母建立连续桶，并把条目缓存为 `Entry` 表；按键路径不再
线性扫描整个文本文件。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 8 | 输出条数上限（1–32） |
| `fuzzy` | bool | true | 模糊音开关 |
| `collocation_weight` | int | 500 | 字符搭配分缩放（0 关闭，最大 2000） |
| `english_passthrough` | bool | true | 非纯拼音输入保留原字母候选 |

## 预算

`sync_us: 300`。词表约 434 KB / 20,069 条；首次调用会解析词表（一次性），之后
按首字母桶匹配并做束搜索。manifest 将 `instructions` 提高到 20M、`time_ms`
提高到 10，覆盖 64 个字母的最坏输入。

## 用例

`tests/cases/`：`nihao`、`ws` 简拼、`nh`、`wsm` 整句、`hbg` → 还不够、
`dd` → 等等（叠词压过“的的”）、`woshizhongguoren` 多词合成、`zongguo`
模糊音/关闭模糊音、半音节前缀、无命中；网格保留多重切分的 `ysh`（有时候
可见）、`xian`（西安可见）、`deg`（第二个可见）；以及无上下文排序的
`zsh` → 早上好、`gz` → 广州、`sz` → 深圳、`yiqichifanba` → 一起吃饭吧、
`wozaijia` → 我在家；字符搭配开关（`nihaoma` 第三候选 `你好嘛` vs `年好吗`）、
英文原字母候选（`hello`、`iiii`）与关闭透传。
