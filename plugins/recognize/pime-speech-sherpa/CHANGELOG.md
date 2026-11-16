# CHANGELOG

## 1.0.0

- 初版：sherpa-onnx 流式 Zipformer2-CTC 离线中文识别（native、trusted）。
- `voice start / stop / cancel` 事件；采集 16 kHz 单声道 PCM，边说边输出
  `set_composition`，结束后 `commit` 整句。
- 首次 `start` 延迟加载运行时库与模型，识别器跨轮复用。
- 开发自测：`wav_file` 设置回放 wav，无需麦克风。
