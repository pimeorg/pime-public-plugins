# data/

共享数据的**规范源**。构建时由各插件把需要的文件复制进自己的 `assets/`，运行时
只读包内资产，不读本目录。

## 布局

| 路径 | 内容 |
|---|---|
| `pinyin/pinyin.dict` | 全拼紧凑词表（`<音节 空格 音节>\t<word>\t<freq>`，20k 行） |
| `pinyin/SOURCES.md` | 词表来源（rime-wanxiang，CC BY 4.0）、生成器与裁剪规则 |
| `wubi/wubi.dict` | 五笔 86 starter 词表（`<code>\t<word>\t<freq>`） |
| `cangjie/cangjie.dict` | 仓颉 starter 码表（`<code>	<word>	<freq>`） |
| `locks/` | 大文件来源与 sha256 的说明 |
| `models/` | 模型分发说明（权重不入库） |

拼音词表已是可用于实际输入的紧凑词表（常用单字 + 词语），不是几十行的占位
数据；再大的词表仍通过 `assets.lock` / `models.lock` 分发，见
`locks/README.md`。五笔/仓颉仍保持 starter 规模。

## 规则

- 规范源在本目录；插件 `assets/` 是构建产物的一部分，内容应与这里同步。
- 本目录只放文本/可 diff 的数据；二进制权重一律走 lock。
- 词表格式：UTF-8，Tab 分隔，`#` 起始为注释；数值字段为十进制无符号整数。
