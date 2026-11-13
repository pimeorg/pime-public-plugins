# tools/

仓库校验与构建辅助脚本（Python 3，需要 PyYAML）。

| 脚本 | 用途 |
|---|---|
| `cbor.py` | 确定性 CBOR 编解码；供其他脚本导入 |
| `check_plugins` | 目录/清单/包布局/源码禁用项/链引用校验；`--write-index` 生成 `plugins/README.md` |
| `chainc` | chain YAML → core CBOR；`--lock` 写 `<chain>.lock.yaml`，`--check` 校验 |
| `luabundle` | 多 Lua 源合并为单 `plugin.lua` |
| `fetch_data` | 按 `assets.lock` / `models.lock` 下载并校验 sha256 |
| `pinyin_dictgen` | rime-wanxiang → `data/pinyin/pinyin.dict`（音节分隔、去声调、按词频裁剪） |

示例：

```sh
python tools/check_plugins
python tools/chainc chains/pinyin/default.yaml --check
python tools/chainc chains/pinyin/default.yaml -o build/pinyin.cbor
python tools/fetch_data --check
```

`check_plugins` 以 core 实现与 `schema/cbor/*.cddl` 为准；发现文档漂移时以代码
为准并在 `PROGRESS.md` 记录。
