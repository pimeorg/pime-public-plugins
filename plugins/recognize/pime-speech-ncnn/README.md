# pime-speech-ncnn

`recognize`（WASM）：离线语音识别。宿主负责采集/解压 PCM，插件把 PCM 送入
`ml.infer`，再把标签 id 映射为候选。

## 事件

| 事件 | 行为 |
|---|---|
| `audio.frame {pcm}` | 追加 PCM 字节，最多 16000 字节 |
| `audio.end` | 编码张量 → `ml.run` → 标签映射 → 候选；清空 PCM |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-speech-ncnn/candidates` | array<map> | `{text, score, source:"speech"}` |

## 模型

- `models: speech`，engine `ncnn`，输入 `u8 [1,N,1,1]`，输出 `u32` 标签 id。
- 权重不入库，由 `models.lock` 分发；`hash` 当前为 `sha256:unfetched`。
- backend 缺失 → `PIME_UNSUPPORTED`；标签资产缺失 → `PIME_NOT_FOUND`。

## 资产

`assets/labels.txt`：`<id>\t<char>`。

## 能力

`ml.infer`、`audio.capture`、`audio.opus`、`asset.read`、`workspace`。在线语音
请改走 `net.wss`，本插件只覆盖离线路径。

## 用例

`tests/cases/`：单帧累积、`audio.end` 识别。
