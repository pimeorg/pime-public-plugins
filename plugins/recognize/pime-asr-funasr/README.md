# pime-asr-funasr

`recognize` 插件：阿里百炼 FunASR 实时流式识别（DashScope WebSocket）。

## 事件

| 事件 | 行为 |
|---|---|
| `voice`，`action = 0`（开始） | 结束上一轮，打开 16 kHz 单声道采集，在自有线程里 `run-task`，握手前先缓存音频 |
| `voice`，`action = 1`（结束） | 停采集，发 `finish-task`，等最终结果 |
| `voice`，`action = 2`（取消） | 停采集、断连接，丢弃本轮已识别文本 |

`sentence_end` 为真时把文本追加到已提交文本，否则作为中间结果显示在组合串；
`task-finished` 提交并断开，`task-failed` 记错误后收尾。

## 协议

`run-task → task-started → 二进制 PCM → finish-task → result-generated →
task-finished`。鉴权走 `Authorization: Bearer <api_key>`。

## 设置

| 键 | 类型 | 说明 |
|---|---|---|
| `api_key` | secret | 必填 |
| `model` | string | 默认 `qwen-audio-3.1-asr-flash-streaming`；选「自定义」时用 `custom_model` |
| `custom_model` | string | 自定义模型名 |
| `endpoint` | string | 默认 DashScope 线上地址；离线联调时指向模拟服务 |

## 能力

`audio.capture`、`net.wss`、`thread`、`time`、`log`。

## 测试

`tests/` 的用例驱动按 `runtime: wasm` 编译插件源码，不覆盖 Lua 插件。本插件在
Android 上用 `pime-android/tools/mock_asr_server.py`（DashScope 协议，监听
`0.0.0.0:8765`）端到端验证：设置页填 `ws://10.0.2.2:8765/` 后长按空格，模拟服务
回 `你好世界` 并提交进输入框；不填密钥时只记错误、不联网。
