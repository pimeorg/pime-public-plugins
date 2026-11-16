# pime-speech-sherpa

`recognize`（native，可信）：离线流式语音识别。声音在设备上由 sherpa-onnx
流式 Zipformer2-CTC 识别，边说边出字，结束后整句提交。与 WeaveText 的
Android ASR 使用同一套 sherpa-onnx C API 和同一份模型。

## 事件

| 事件 | 行为 |
|---|---|
| `voice { action: 0 }` | 打开麦克风采集（或回放 `wav_file`），逐帧送入识别器；增量结果以 `set_composition` 输出 |
| `voice { action: 1 }` | 结束输入、解码尾音，返回 `commit` 命令提交整句文字 |
| `voice { action: 2 }` | 取消本轮，丢弃未提交文字 |

其他事件返回 `handled: false`，不改变任何状态。

## 识别流程

1. `start`：首次使用时 `dlopen` 宿主提供的 `libonnxruntime.so`、
   `libsherpa-onnx-c-api.so`，创建 online recognizer 与 stream。
2. 插件线程通过 `audio.capture` 的 `start / read / stop` 读 16 kHz 单声道
   s16le PCM，每次约 100 ms；转 float 后 `accept_waveform` + 解码。
3. 端点检测（静音 2.4 s / 1.2 s / 最长 20 s）切句；切句前的文字累积到
   本轮最终文本，未切句的增量照常预编辑。
4. `stop`：`input_finished` 后补解码，把累计文本随 `commit` 命令返回；
   `cancel` 则清空 stream，不提交。
5. 每次 `stop`/`cancel` 后可立刻再 `start`；识别器在插件生命周期内复用。

## 设置

| 键 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `runtime_dir` | string | 空 | 存放 `libsherpa-onnx-c-api.so` / `libonnxruntime.so` 的目录；空则按系统搜索路径 |
| `model_dir` | string | 空 | 存放 `model.int8.onnx` 与 `tokens.txt` 的目录 |
| `threads` | int | 2 | 推理线程数，1..8 |
| `sample_rate` | int | 16000 | 仅用于校验/记录；当前识别器固定 16 kHz |
| `wav_file` | string | 空 | 开发自测：回放指定 wav（16 kHz 单声道 16-bit），不打开麦克风 |

## 宿主依赖

- 运行时库 `libsherpa-onnx-c-api.so`、`libonnxruntime.so`（arm64-v8a /
  x86_64）由宿主随 APK 打包，路径通过 `runtime_dir` 传入。
- 模型 `sherpa-onnx-streaming-zipformer-small-ctc-zh-int8-2025-04-01`
  的 `model.int8.onnx`（约 25 MB）与 `tokens.txt` 由宿主下载/安装到
  `model_dir`，插件不联网。

## 能力与预算

`capabilities: audio.capture, audio.pcm, asset.read, log`；`trusted: true`。
`init` 只保存设置，识别器在第一次 `voice start` 时才加载，避免拖慢键盘启动。
音频只在内存中流转，插件没有 `net.*` 能力。

## 构建

native 插件仅在宿主显式打开可信逃生舱时构建：

```cmake
-DPIME_BUILD_TRUSTED_NATIVE=ON
-DPIME_CORE_DIR=/path/to/pime-core
```

产物 `libpime_speech_sherpa.so` 由宿主复制为插件包内的 `plugin.so`（只读，
满足 Android 10+ 对可执行代码的要求）。

## 用例

`tests/cases/`：非 `voice` 事件透传、未开始时的 `cancel`。
