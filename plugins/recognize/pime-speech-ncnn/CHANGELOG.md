# CHANGELOG

## 1.1.0

- 新增：10ms 帧 RMS 端点检测，静音不产生候选。
- 新增：两遍识别（草稿 + 终稿），输出 `passes` / `speech_ms`。
- 新增：个人热词加权与「热词为识别前缀」扩展（`hotword.update` / `clear`）。

## 1.0.0

- 初版：累积 PCM、调用 ml.infer、id → 字符候选。
