# pime-speech-ncnn

`recognize`（WASM）：离线语音识别，PCM 经 `ml.infer` 得到候选。

## 事件

| 事件 | 行为 |
|---|---|
| `audio.frame {pcm}` | 累积 PCM（s16le、单声道、16 kHz） |
| `audio.end` | 端点检测 → 两遍识别 → 输出候选 |
| `hotword.update {payload}` | 载入个人热词（`<词>\t<权重>` 每行） |
| `hotword.clear` | 清空热词 |

## 算法

1. **端点检测**：按 10ms 帧计算 s16le RMS，低于 `vad_threshold` 的帧从两端裁掉；
   全是静音时**不产生候选**（`passes=0`、`speech_ms=0`）。
2. **两遍识别**：对裁剪后的音频跑两遍（草稿 + 终稿），以第二遍为准，`passes=2`。
3. **个人热词**：`hot/<词>` 命中时加权并注释「热词」；热词若以识别结果**为前缀**，
   追加为候选。

## Workspace

| 方向 | 键 |
|---|---|
| 写 | `plugin/pime-speech-ncnn/candidates`、`plugin/pime-speech-ncnn/passes`、`plugin/pime-speech-ncnn/speech_ms`、`plugin/pime-speech-ncnn/hotwords`（命中数） |

## 存储

| 键 | 值 |
|---|---|
| `hot/<词>` | 热词权重 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `vad_threshold` | int | 300 | RMS 端点阈值 |
| `two_pass` | bool | true | 是否两遍识别 |
| `hotword_boost` | int | 12000 | 热词基础加分 |
| `max_candidates` | int | 8 | 候选上限 |

## 资产与模型

- `assets/labels.txt`：`<label id>\t<char>`。
- `models/speech.ncnn`：按 `models.lock` 拉取，不入库。

## 预算

`sync_us: 300`；PCM 上限 32 KB，候选 8 条。

## 用例

`tests/cases/`：收帧、识别、静音不产候选、两遍计数、热词加权与前缀扩展、热词清空、
高阈值裁掉全部语音。
