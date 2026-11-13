# pime-pinyin-translate

`translate` 插件：把音节串（或原始字母串）映射为候选词。

## 事件

只处理同步 `key` / `text` 事件；不产生命令，只写 workspace。

## 输入

| 键 | 生产者 | 说明 |
|---|---|---|
| `plugin/pime-pinyin-parse/raw` | pime-pinyin-parse | 原始字母串（去掉 `'` 后作为查询键） |
| `plugin/pime-pinyin-parse/syllables` | pime-pinyin-parse | 音节数组；存在时优先拼接成查询键 |

## 输出

写入 `plugin/pime-pinyin-translate/candidates`：`array<map>`，每项
`{ text: str, score: uint, source: "dict" }`，按分数降序，最多
`settings.max_candidates` 条。精确命中额外加 10000 分。

## 资产

`assets/pinyin.dict`：`<拼音串>\t<候选词>\t<词频>`，UTF-8，`#` 开头为注释。
这是 starter 词典；生产词典由 `data/locks/` 固定版本与 sha256 后构建期拉取。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `max_candidates` | int | 8 | 输出条数上限 |
| `fuzzy` | bool | true | 保留位：模糊音开关（当前由词典层承担） |

## 预算

`sync_us: 100`。词典线性扫描，starter 词典约 1KB；大词典应换成索引格式。

## 用例

`tests/cases/`：`nihao` 命中、无命中、数字/标点被忽略。
