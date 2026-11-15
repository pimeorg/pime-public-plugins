# pime-t9-translate

`translate` 插件：把九键 / 14 键键码串解码成候选词。

## 思路

词表与全拼共用（`pinyin.dict`，键是空格分隔的音节）。解码不把每个数字展开成
字母，而是反过来：把词典里每个音节编码成同一套键码，再与输入串比对。

- 一个音节吃掉 1..len(code) 个键码，必须逐位匹配（`94` 是 zhong 的 `zh`，
  `94664` 是 zhong）；
- 整串必须恰好消费完，否则该词不成立；
- 音节被截断（少打了键码）时按缺位罚分，完全不截断时给精确奖励。

`1` 是音节分隔：整串按 `1` 切成若干段，各自解码后按名次拼接（第 i 条 =
每段第 i 名，越界则取该段第一名）。

## 打分

```
score = 词频 + (无截断 ? 3000 : 0) - 1500 * 缺失键码数
```

九键每键 3~4 个字母，歧义比全拼大，因此截断代价高于全拼的简拼罚分。

## Workspace

| 方向 | 键 | 类型 |
|---|---|---|
| 读 | `plugin/pime-t9-parse/raw` | text（键码串，含 `1`） |
| 写 | `plugin/pime-t9-translate/candidates` | array<map> |

候选：`{ text, score, source: "t9", words }`，`words` 是音节数。

消费者：`pime-candidates-format`。

## 资产

`assets/pinyin.dict`：与全拼翻译相同的音节词表（`<音节 音节…>\t<词>\t<词频>`）。
资产缺失时 `pime_process` 返回 `PIME_NOT_FOUND`，不静默降级。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `grouping` | text | nine | `nine` 或 `fourteen` |
| `max_candidates` | int | 8 | 输出上限（1..32） |

`grouping` 必须与 `pime-t9-parse` 一致；两者不一致时解码会全部落空，这是配置
错误而不是静默降级。

## 预算

`sync_us: 200`；每个键码位置线性扫描一次词表（20k 条），首次调用读入资产。

## 用例

`tests/cases/`：`94664486` → 中国、`96` → 我、单音节截断、分隔符拼接、无匹配、
14 键、关闭开关。
