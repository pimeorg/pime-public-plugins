# pinyin.dict 来源与生成

`pinyin.dict` 是 `pime-pinyin-translate` 随包发布的紧凑词表。它不是 starter
占位数据：为了支持简拼（`ws` → 我是）与整句合成，需要一份覆盖常用单字和词语、
带词频的词表。

## 来源

- 数据：**rime-wanxiang**（雾凇拼音配置）的 `zi.dict.yaml`（单字）与
  `jichu.dict.yaml`（词语）。
- 仓库：<https://github.com/amzxyz/rime_wanxiang>
- 本仓库生成时使用的提交：`c7f27dece03feadb2e7ab5313d932d9674456a4e`
- 许可证：**CC BY 4.0**（Attribution 4.0 International）。原始数据版权归
  rime-wanxiang 作者所有；本仓库只做格式转换与裁剪，并在本文件与插件 README
  中署名。

## 处理

`tools/pinyin_dictgen` 做以下转换：

1. 语音节去声调（`wǒ` → `wo`），`ü` 写作 `v`（与 `pime-pinyin-parse` 的
   413 音节表一致）。
2. 只保留音节表内的读音；词语的音节数与汉字数不一致的条目丢弃。
3. `(词, 读音)` 合并不同声调的重复行，取最大词频。
4. 取词频最高的 4000 个单字读音与 16000 个词语，按首字母排序。
5. 输出 `<空格分隔音节>\t<词>\t<词频>`，键形如 `wo shi`，便于翻译插件做
   音节级精确/前缀/简拼匹配，而无需在运行时重新分词。

生成命令（源文件不入库）：

```sh
python -I tools/pinyin_dictgen \
    --zi /path/to/rime_wanxiang/dicts/zi.dict.yaml \
    --jichu /path/to/rime_wanxiang/dicts/jichu.dict.yaml \
    --out data/pinyin/pinyin.dict
```

## 常用短语补充

裁剪后的 20k 条词表会漏掉一批聊天里高频、但在通用语料里排名靠后的短语
（`晚安`、`生日快乐`、`天安门`、`女孩`、`绿色` 等）。这些词不写死在代码里，
而是放在 `data/pinyin/extra.txt`，由 `tools/pinyin_merge` 合并：

```sh
python -I tools/pinyin_merge \
    --base data/pinyin/pinyin.dict \
    --extra data/pinyin/extra.txt \
    --out data/pinyin/pinyin.dict
```

补充表与基础词表同格式。规则：

- `(读音, 词)` 唯一；补充表是权威值，可提高、也可下调已有词频（例如避免
  `女孩` 压过 `你好`）。
- 每个音节必须存在于 parse 插件的 413 音节表，音节数必须等于汉字数。
- 输出按 `(首字母, 读音, -词频)` 排序，保证翻译插件按首字母建连续桶。

合并是幂等的，当前产物 20,066 行 / 434 KB。然后同步到插件资产：

```sh
cp data/pinyin/pinyin.dict \
   plugins/translate/pime-pinyin-translate/assets/pinyin.dict
```

若需要更大词表，增大 `--singles` / `--words`，并同时提高插件的
`kDictBytes` / `kMaxEntries`（`src/plugin.cpp`）与 manifest 的 `memory_mb`。
