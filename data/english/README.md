# 英文词表

`english.dict` 是 starter 规模（常用词 + 少量补全示例），格式为
`<word>	<word>	<freq>`。键为小写；匹配时撇号会被去掉（`dont` 能补全
`don't`）。

- `pime-english-parse` 累积字母与撇号。
- `pime-english-translate` 给出原码、前缀补全与拼写建议，并按输入大小写回写。

生产词表按同样格式提供；更大的词表应走 `data/locks/` 分发。
