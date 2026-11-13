# trust/

包签名信任根。core 的 `pime_core_config.trust_store` 指向本目录，读取：

- `roots.pem`：Ed25519 公钥根，一个或多个 `BEGIN PUBLIC KEY` 块。
- `revoked.txt`：每行一个被吊销的 `key_id`，`#` 起始为注释；命中即拒绝加载。

## 当前状态

`roots.pem` 是**开发用根**（由本地 `openssl genpkey -algorithm ED25519` 生成，
私钥未入库）。发布前必须：

1. 在离线环境重新生成生产根，公钥写入 `roots.pem`；
2. 私钥只进签名流水线，不入库；
3. 用新根重签所有包，并在 `manifest.signature.key_id` 中写入对应 id。

## 签名范围

`SIGNATURE` 为 Ed25519 detached 签名，覆盖：

```text
规范化 manifest（不含 signature 段）
+
按路径排序的 "path\0size\0sha256\n" 列表（不含 SIGNATURE 自身）
```

校验顺序：清单 → ABI major → 平台 → 依赖 → 签名 → 可加载。签名失败、`key_id`
未命中根、或命中吊销列表时拒绝加载；不做任何降级。
