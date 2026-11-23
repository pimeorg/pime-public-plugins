# pime-asr-tencent

`recognize` 插件：腾讯云实时语音识别 V2。

## 事件

| 事件 | 行为 |
|---|---|
| `voice`，`action = 0`（开始） | 结束上一轮，打开 16 kHz 单声道采集，签名后连服务，握手前先缓存音频 |
| `voice`，`action = 1`（结束） | 停采集，发 `{"type":"end"}`，等 `final = 1` |
| `voice`，`action = 2`（取消） | 停采集、断连接，丢弃本轮已识别文本 |

`sentence_type` 1 为确定结果并追加到已提交文本，0 为中间结果显示在组合串；
`final = 1` 时提交并断开。

## 协议

URL 查询串按键字典序拼接（值为原文），`HMAC-SHA1(SecretKey, 原文)` → Base64 →
URL 编码得到 `signature`。握手后服务端先回 `{"code":0}`，二进制帧为 PCM，
结束发 `{"type":"end"}`。

## 设置

| 键 | 类型 | 说明 |
|---|---|---|
| `app_id` | string | 必填 |
| `secret_id` | string | 必填 |
| `secret_key` | secret | 必填，签名用 |
| `engine_model_type` | string | 默认 `16k_zh_en_2.0` |
| `hotword_list` | string | 热词，默认空 |

## 能力

`audio.capture`、`net.wss`、`thread`、`crypto.*`、`time`、`random`、`log`。

## 测试

`tests/` 的用例驱动按 `runtime: wasm` 编译插件源码，不覆盖 Lua 插件。本插件在
Android 上验证了密钥缺失路径（记 `未配置 AppID / SecretId / SecretKey` 后返回，
不联网）；签名与联网路径需要真实密钥，尚未实测。
