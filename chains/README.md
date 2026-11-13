# 产品链

链是「有序插件组合」，不打包进任何插件；字段、workspace 契约与锁定规则见
`docs/chains.md`。本目录由 `tools/check_plugins` 校验，锁文件由
`tools/chainc --lock` 生成。

| 链 | 文件 | 说明 |
|---|---|---|
| 全拼默认 | `pinyin/default.yaml` | 键位 → 拼音解析 → 词表 → 排序 → 格式化 → 学习 |
| 双拼 | `pinyin/shuangpin.yaml` | 首键/次键合成音节后走同一条链路 |
| 五笔 86 | `wubi/86.yaml` | 五笔码 → 词表 → 默认排序 → 格式化 |
| 离线手写 | `handwriting/offline.yaml` | 笔迹 → ml.infer → 格式化 |
| 语音 | `speech/online.yaml` | 当前批次以离线识别（`pime-speech-ncnn`）兜底；在线 `net.wss` 插件未列入首批 |
| 标点 | `punctuation/zh.yaml` | 键位 → 中文标点替换 |

锁文件固定精确 `(id, version, code_hash)`；未构建的包写 `sha256:unbuilt`，
`chainc --check` 在包缺失时接受该占位值。
