# 英文词表

`english.dict` 随 `pime-english-translate` 发布（同一份文件在
`plugins/translate/pime-english-translate/assets/`），格式为
`<key>\t<text>\t<freq>`：

- `key` 是小写且**去掉撇号**，因为匹配时用户输入也会去撇号（`dont` 能补全
  `don't`）；
- `text` 保留自己的拼写，所以 `GitHub`、`WiFi`、`API` 不会被小写化；
- 只收单 token（字母与撇号）——解析器不会累积空格或连字符；
- 频次 ≥ 50 的保留，另加所有带大写或撇号的条目（专名与技术词频次常偏低）。

当前文件 36,345 条、758,967 字节，在插件 1 MB 的读取上限内。词表按 key
排序，translate 线性扫描（top-N 替换，不依赖扫描顺序）。

## 来源与生成

来源：rime-wanxiang `dicts/en.dict.yaml`（CC BY 4.0，
<https://github.com/amzxyz/rime-wanxiang>）。重新生成：

```bash
python tools/english_dict SOURCE/en.dict.yaml -o data/english/english.dict
cp data/english/english.dict plugins/translate/pime-english-translate/assets/english.dict
```

需要更大词表时不要换掉这份文件格式：保持 key 去撇号、text 保留拼写，
并把体积控制在 `pime-english-translate` 的 `kMaxDictBytes` 以内。
