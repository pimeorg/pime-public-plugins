# shared/

构建期共享代码。**不产生运行时共享库**：C++ 是 header-only，编译进各插件；
Lua 模块由 `tools/luabundle` 合并进单 `plugin.lua`。

## C++（`shared/cpp/include/pimeplug/`）

| 头 | 用途 |
|---|---|
| `cbor.hpp` | definite-length CBOR 读写、`Span`/`MapView`/`ArrayView`/`Writer`/`Frame` |
| `utf8.hpp` | UTF-8 码点计数、编解码 |
| `commands.hpp` | 宿主命令构造（commit / set_composition / show_candidates / beep …） |
| `workspace.hpp` | 短键 → `plugin/<id>/<key>`、workspace 写入器 |
| `dict.hpp` | `<key>\t<text>\t<freq>` 词表读取 |
| `asset.hpp` | `pime_host_asset` 封装（open/size/read/close） |
| `ml.hpp` | `ml.infer` 张量编码与标签 id 解码 |

约束：不依赖 STL、异常、RTTI；不使用平台头；只依赖 `pime/plugin_api.h`。

## Lua（`shared/lua/`）

`cbor.lua`、`utf8.lua`、`commands.lua`。Lua 插件必须是单文件，合并方式：

```sh
python tools/luabundle --main plugins/<role>/<id>/plugin.lua \
       --module utf8=shared/lua/utf8.lua -o build/plugin.lua
```
