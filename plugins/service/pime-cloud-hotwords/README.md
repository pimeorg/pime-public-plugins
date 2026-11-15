# pime-cloud-hotwords

`service`（Lua）：Ed25519 签名热词，**只下载不上传**（对应 WeaveText `cloud.rs`）。

## 事件

| 事件 | 行为 |
|---|---|
| `timer {id="pime-cloud-hotwords/refresh"}` | 定时发起拉取 |
| `cloudhotwords.refresh` | 立即拉取 |
| `net.data {id, data}` | 累积响应体（上限 512KB） |
| `net.close {id}` | 校验签名；通过则写 store |
| `net.error {id}` | 丢弃并 `notify(3)` |

## 线协议

```text
响应体 = "<hex ed25519 签名>\n<payload>"
签名覆盖 <payload> 字节
```

校验用 `pime.crypto.verify(public_key, "ed25519", payload, signature)`；不通过不写
store，也不发候选。

## 存储

| 键 | 值 |
|---|---|
| `hotwords/latest` | 已验签的 payload |
| `hotwords/updated_ms` | 写入时间 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 总开关 |
| `url` | string | "" | HTTPS 端点；空则不拉取 |
| `public_key` | string | "" | 验签公钥 |
| `interval_ms` | int | 21600000 | 刷新周期（6 小时） |

## 与排序的接线

验签后的 payload 由宿主以 `hotword.update { payload }` 事件转给
`pime-pinyin-rank`（格式：`<词>	<权重>` 每行一条），排序插件据此做热词偏置；
`hotword.clear` 清空。本插件自身不直接改候选。

## 隐私

只发 GET，不带用户词、学习记录、上下文；没有上传路径。
