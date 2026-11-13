# data/

共享数据的**规范源**。构建时由各插件把需要的文件复制进自己的 `assets/`，运行时
只读包内资产，不读本目录。

## 布局

| 路径 | 内容 |
|---|---|
| `pinyin/pinyin.dict` | 全拼 starter 词表（`<pinyin>\t<word>\t<freq>`） |
| `wubi/wubi.dict` | 五笔 86 starter 词表（`<code>\t<word>\t<freq>`） |
| `cangjie/` | 仓颉码表（首批未提供，parse 只累积码串） |
| `locks/` | 大文件来源与 sha256 的说明 |
| `models/` | 模型分发说明（权重不入库） |

starter 词表故意保持很小（几十行），用于跑通链路与用例；生产词表通过
`assets.lock` / `models.lock` 分发，见 `locks/README.md`。

## 规则

- 规范源在本目录；插件 `assets/` 是构建产物的一部分，内容应与这里同步。
- 本目录只放文本/可 diff 的数据；二进制权重一律走 lock。
- 词表格式：UTF-8，Tab 分隔，`#` 起始为注释；数值字段为十进制无符号整数。
