# Changelog

## 1.0.0

- 首个版本：音节串前缀查词典、精确命中加权、workspace 候选输出。
- 音节级匹配：完整音节、尾部半音节前缀、单字母简拼（`ws` → 我是）。
- 模糊音：`zh/z`、`ch/c`、`sh/s`、`n/l`、`an/ang`、`en/eng`、`in/ing` 等。
- 音节网格上的有界束搜索，支持整句合成（`wsm` → 为什么，其次“我什么”）。
- 翻译改为原始字母网格：不再要求 parse 的单一预切分，同一串字母同时保留
  完整音节、半音节、简拼与多重边界（`xian` → 先/西安，`ysh` → 也是/有时候）。
- 边按覆盖音节数分桶（每桶 8 条），长短语不会被高频单字提前剪掉；
  `hbg` → 还不够 首位，`bzd` → 不知道，`zmy` → 怎么样。
- 内聚奖励改为随覆盖字母数增长（`1500 * (letters - 1)`），既保留整词优势，
  又不会让 `zh` 被拆成 `z`+`h` 的“之后”压过“这”。
- 束宽从 8 提高到 12；新增 `candidates_include` 用例断言。
- 词表升级为 20k 条带词频的 rime-wanxiang 紧凑词表（CC BY 4.0，见
  `data/pinyin/SOURCES.md`）；加载时建立首字母桶索引。
- 新增 `pimeplug/memory.hpp`：freestanding wasm 的弱 `memset/memcpy/memmove`，
  避免未解析的 `env` 导入。
