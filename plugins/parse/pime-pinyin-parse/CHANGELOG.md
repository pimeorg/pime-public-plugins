# Changelog

## 1.1.5

- workspace 新增 `abbreviation`：两段式简拼（完整音节 + 下一个声母，如
  `benj`、`key`、`zhongg`）为 true。中英混输据此让中文候选优先，`benj`
  不再被 `Benjamin`、`key` 不再被 `key` 本身挤到前面；`linux`（li+nu+x）
  这类已经拼出多个完整音节的输入仍算英文。

## 1.1.4

- workspace 新增 `complete`：原始字母串是否完整切成音节，供中英混输判断
  “像英文的输入”（`goo`/`hel`）与真拼音（`nihao`）；v-mode 恒为 false。

## 1.0.0

- 首个版本：全拼音节最大匹配、预编辑与 workspace 输出。
- 尾部半音节保持为一段（`zh`、`h`），供 translate 做前缀匹配；无法成为音节
  前缀的剩余串按单字母切分（`ws` → `w` + `s`）。

## 1.1.0

- 自动纠错：相邻字母颠倒、漏打一个字母、多打一个字母；只在原串无法完整读成
  音节、且纠正后能完整成音节时采用。
- 触点邻键纠正：`event.neighbour` 给出邻键字母，替换后能成音节时优先采用。
- 纯简拼（无声母串，如 `zsh`/`hbg`/`dhrs`）与已可读串（如 `xianr`）不纠错。
- 预编辑显示纠正后的拼写；workspace 新增 `input`（原串）与 `marks`（纠正标记）；
  `set_composition.segments[].type` 标注纠正/不完整段。
- 状态保存原串而非纠正串，后续按键与退格仍按用户实际输入处理。
- 组合串上限提升到 96 字节。
