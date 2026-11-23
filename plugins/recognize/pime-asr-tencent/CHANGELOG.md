# pime-asr-tencent

## 1.0.0

- 腾讯实时语音识别 V2：URL 内 HMAC-SHA1 签名、`{"code":0}` 握手、PCM 上行、
  `{"type":"end"}` 收尾、`sentence_type` / `final` 结果处理。
- 密钥缺失时不发起连接，只记错误日志。
