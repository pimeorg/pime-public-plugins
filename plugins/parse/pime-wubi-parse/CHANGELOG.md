# Changelog

## 1.1.0

- 四码唯一顶屏：满四码且只有一条整码命中时直接提交。
- 满码再敲：码满后再敲字母，先提交当前首选（无命中则丢弃）并开始新组合。
- `z` 拼音反查：码可超过四码，最长 `max_pinyin`，不触发顶屏。
- 新增 `assets/wubi.dict`、`auto_commit` / `pinyin_lookup` / `max_pinyin` 设置。

## 1.0.0

- 首个版本：五笔码累积、退格/清空、`code` workspace 契约。
