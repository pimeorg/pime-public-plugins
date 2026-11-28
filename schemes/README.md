# 方案包

方案包是宿主可直接安装的一个目录：一条链 + 它引用的插件副本 + 一份清单。
插件在 `plugins/`，链配方在 `chains/`；方案包把它们按平台组合起来，是发布
产物，不是源码目录。

```text
schemes/win/<id>/
├── scheme.yaml    # id、名称、版本、链文件名、插件 (id, version) 清单
└── chain.yaml     # 链配方，由 tools/chainc 编译成 <chain>
```

组装后的包自包含，运行时不读包外文件：

```text
dist/schemes/<id>/
├── scheme.yaml
├── <id>.cbor
└── plugins/pime-plugin-<plugin-id>-<version>/...
```

## 构建

先按平台构建插件包（Windows 用 wasm-ndk 工具链），再跑组装目标：

```sh
cmake -S . -B build/wasm-ndk -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/wasm32-ndk.cmake
cmake --build build/wasm-ndk --target pime_schemes
```

产物在 `dist/schemes/`。也可以直接调用脚本并挑选方案：

```sh
python tools/pack_schemes.py --packages build/wasm-ndk/package \
                             --out dist/schemes --only pinyin english
```

清单里 pin 的 `(id, version)` 必须已经在 `--packages` 里构建好；缺包会直接
失败，不会产出装不起来的方案包。

## Windows 方案

| id | 说明 | 默认安装 |
|---|---|---|
| `pinyin` | 全拼 | 是 |
| `english` | 英文补全 | 是 |
| `shuangpin-xiaohe` | 双拼 · 小鹤 | 否 |
| `shuangpin-ziranma` | 双拼 · 自然码 | 否 |
| `shuangpin-microsoft` | 双拼 · 微软 | 否 |
| `shuangpin-sogou` | 双拼 · 搜狗 | 否 |
| `wubi86` | 五笔 86 | 否 |
| `cangjie` | 仓颉 | 否 |

默认安装的判定属于宿主：pime-win 只从 `dist/schemes/` 取 `pinyin` 和
`english`，其余方案包由用户拷进 `%LOCALAPPDATA%\PIME\schemes\` 或在设置里
导入。方案包以后随本仓库的 release 发布，现在只做本地组装。
