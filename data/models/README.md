# 模型分发

模型权重不入库。分发方式：

1. 在上游发布 `*.ncnn`（param+bin 打包）与 `labels.txt`；
2. 在各 recognize 插件的 `models.lock` 记录 `path` / `url` / `sha256`；
3. CI 运行 `python tools/fetch_data --check`；
4. 打包时权重随包签名，`manifest.models[].hash` 必须是真实 `sha256:<hex>`。

当前 `pime-handwriting-ncnn` / `pime-speech-ncnn` 的 hash 为占位
`sha256:unfetched`，`check_plugins` 会给出提示；发布流水线必须在签名前替换。
