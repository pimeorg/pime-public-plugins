# Changelog

## 1.1.0

- 补全：短码列出剩余编码作为注释，按 `freq - 200 * 剩余码长` 排序。
- `z` 拼音反查：`assets/pinyin.dict` + 五笔反向表，注释显示完整五笔码。
- 拼写建议：无命中时给出 Damerau–Levenshtein 距离 ≤ 2 的邻居。
- 新增 `completion` / `completion_limit` / `corrections` / `pinyin_lookup` 设置。

## 1.0.0

- 首个版本：五笔码查表、整码加权、前缀候选。
