# Changelog

## 1.0.0

- 首个版本：音节串前缀查词典、精确命中加权、workspace 候选输出。
- 音节级匹配：完整音节、尾部半音节前缀、单字母简拼（`ws` → 我是）。
- 模糊音：`zh/z`、`ch/c`、`sh/s`、`n/l`、`an/ang`、`en/eng`、`in/ing` 等。
- 音节网格上的有界束搜索，支持整句合成（`wsm` → 为什么，其次“我什么”）。
- 词表升级为 20k 条带词频的 rime-wanxiang 紧凑词表（CC BY 4.0，见
  `data/pinyin/SOURCES.md`）；加载时建立首字母桶索引。
- 新增 `pimeplug/memory.hpp`：freestanding wasm 的弱 `memset/memcpy/memmove`，
  避免未解析的 `env` 导入。
