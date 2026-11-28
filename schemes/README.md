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

## 往方案包里加自己的插件

每个插件的 `manifest.yaml` 都写了 `role` 和位置约束：

```yaml
role: rank
after: [preprocess, parse, translate, pime-pinyin-translate]
```

- `after` / `before` 的 role 是**白名单**：前面的 role 必须都在 `after`，后面的
  role 必须都在 `before`。`after: []` 表示必须最先，`before: []` 表示必须最后。
- 里面的**插件 id 是硬约束**：该插件如果在链中，就必须在声明的这一侧。
  例如上面的 `pime-pinyin-rank` 必须排在 `pime-pinyin-translate` 之后；插件是否
  存在由它自己的 `requires` 负责。
- `format` / `learn` / `service` 通常列出全部 role 作为 `after`，位置基本自由。

例如给拼音方案加一个自定义 `rank` 插件：

```yaml
# schemes/win/pinyin/chain.yaml
plugins:
  - { id: pime-keymap-default, role: preprocess }
  - { id: pime-pinyin-parse,   role: parse }
  - { id: pime-shape-parse,    role: parse }
  - { id: pime-pinyin-translate, role: translate }
  - { id: pime-pinyin-rank,    role: rank }
  - { id: my-rank,             role: rank }   # after: [preprocess, parse, translate, rank]
  # ...
```

再把 `my-rank` 的插件包放进 `--packages`（或在 `scheme.yaml` 的 `plugins`
里 pin 版本），重新跑 `pime_schemes`。`chainc` 会按 `after` 拒绝放错位置的
条目，不会产出装起来顺序错误的包。

已经装到 `%LOCALAPPDATA%\PIME\schemes\<id>\` 的包，把自己的插件包拷进它的
`plugins/`，并按同样规则改用户链（宿主设置里的改动写在 `custom.cbor`）。
