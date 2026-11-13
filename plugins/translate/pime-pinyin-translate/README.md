# pime-pinyin-translate

`translate` 插件：把音节网格映射为候选词与整句。

## 事件

只处理同步 `key` / `text` 事件；不产生命令，只写 workspace。

## 输入

| 键 | 生产者 | 说明 |
|---|---|---|
| `plugin/pime-pinyin-parse/syllables` | pime-pinyin-parse | 音节数组；每项可以是完整音节、尾部半音节或单字母简拼 |
| `plugin/pime-pinyin-parse/raw` | pime-pinyin-parse | 无音节数组时的回退：整串按一个（前缀）音节查询 |

`pime-pinyin-parse` 的切分规则：完整音节最长匹配；末尾无法成音节的字母序列若
是某音节的前缀（`zh`、`h`），保持为一个段；其余单字母（`w`、`s`）作为简拼段。

## 匹配

词典键是空格分隔的音节（`wo shi`）。一个条目覆盖 `syllables` 中连续的一段：

- **完整**：段与音节相等（`wo` ↔ `wo`）。
- **前缀**：段是音节的前缀（`zh` ↔ `zhong`），按缺少的字母数罚分。
- **简拼**：单字母段匹配任意以它开头的音节（`w` ↔ `wo/wei/wu`）。
- **模糊音**（`settings.fuzzy`）：`zh/z`、`ch/c`、`sh/s`、`n/l`、`an/ang`、
  `en/eng`、`in/ing`、`ian/iang`、`uan/uang`、`ü/u`，额外罚分。

## 整句合成

在一个音节网格上做有界束搜索（宽度 8，每个位置最多 24 条边）：词典条目覆盖
连续音节，路径按词概率累加。分数使用 `log2(词频)`，每个词减去语料规模常数，
再给多音节词一点内聚奖励，因此“你好”这类词典词会排在“你 + 和”这类自由组合
之前；没有整词时仍能拼出“我是吗”这类组合。

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
提高到 10，覆盖 16 段的最坏输入。

## 用例

`tests/cases/`：`nihao`、`ws` 简拼、`nh`、`wsm` 整句、`zongguo` 模糊音、
关闭模糊音、半音节前缀、无命中。
