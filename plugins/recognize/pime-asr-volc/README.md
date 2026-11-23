# pime-asr-volc

`recognize` 插件：火山引擎 bigmodel_async 在线流式语音识别。

## 事件

| 事件 | 行为 |
|---|---|
| `voice`，`action = 0`（开始） | 结束上一轮，打开 16 kHz 单声道采集，在自有线程里连服务、发配置帧、持续推 PCM |
| `voice`，`action = 1`（结束） | 停采集，发末包（序号为负、flags = 3），等最终结果 |
| `voice`，`action = 2`（取消） | 停采集、断连接，丢弃本轮已识别文本 |

服务端帧由 `pime.on_event` 收到后转给 worker；结果通过 `pime.emit` 异步上屏
（`set_composition` 显示中间结果，末包后 `commit`）。

## 协议

4 字节头 + int32 序号 + uint32 长度 + gzip 负载（JSON 配置 / PCM / 结果）。
消息类型 0x1 配置、0x2 音频、0x9 结果、0xF 错误；最后一包序号为负、`flags = 0x3`。

## 设置

| 键 | 类型 | 说明 |
|---|---|---|
| `api_key` | secret | 优先；填了就不用 app/access key |
| `app_key` / `access_key` | string / secret | 无 `api_key` 时用 |
| `resource_id` | string | 默认 `volc.seedasr.sauc.duration` |
| `endpoint` | string | 默认火山线上地址；离线联调时指向模拟服务 |

## 能力

`audio.capture`、`net.wss`、`thread`、`compress.gzip`、`time`、`random`、`log`。

## 测试

`tests/` 的用例驱动是按 `runtime: wasm` 编译插件源码跑的，Lua 插件不在其覆盖内。
本插件在 Android 上用 `pime-android/tools/mock_asr_server.py`（DashScope 协议）与
本机自测：填 `endpoint` 指向模拟服务后长按空格，识别文本提交成功；不填密钥时
只记 `未配置 API Key` 并返回，不联网。
