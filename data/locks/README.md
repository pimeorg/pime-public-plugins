# data/locks

大资产与模型只记录来源与哈希，不入库。

## 格式

`plugins/<role>/<id>/assets.lock` 或 `models.lock`：

```yaml
assets:
  - { path: pinyin.bin, url: "https://data.pime.dev/pinyin-1.0.bin",
      sha256: "<hex>" }
models:
  - { path: handwriting.ncnn, url: "https://models.pime.dev/handwriting-1.0.ncnn",
      sha256: "<hex>" }
```

`url` 为 HTTPS；`sha256` 为下载后的内容哈希（hex，小写）。缺失或哈希不符时
`tools/fetch_data` 直接失败，不写入半成品。

## 流程

```sh
python tools/fetch_data --check     # 只校验，不下载
python tools/fetch_data             # 下载缺失项并校验
```

`models/` 下的权重永不提交；首批 `pime-handwriting-ncnn` 与
`pime-speech-ncnn` 的 manifest 中 `hash` 为 `sha256:unfetched`，正式发布前
必须由分发方替换为真实哈希并附 lock 文件。
