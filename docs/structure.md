# pime-public-plugin 工程结构

> 本文只描述仓库结构、构建与发布边界；插件行为规格以 `docs/plugin.md` 与
> pime-core 为准。**契约以实现为准**：
> `pime-core/src/codec/manifest.cpp`、`pime-core/src/host/package.cpp`、
> `pime-core/schema/cbor/*.cddl`。

## 1 设计依据

从 core 实现得到的硬约束：

| 约束 | 来源 |
|---|---|
| 一个包只能有一个 runtime、一个 role | `manifest.yaml` 单值字段；`validate_manifest` |
| `role` 只能是 9 个值：`preprocess / parse / translate / rank / format / recognize / learn / predict / service` | `src/codec/manifest.cpp` `is_known_role` |
| 代构建配置只认 `{plugins:[{id, version?, alias?, role, settings}]}`，数组顺序即链顺序 | `src/core/generation.cpp` `generation_build` |
| 包内只允许 `manifest.yaml`、`plugin.wasm` / `plugin.lua` / `plugin.dll` / `plugin.so` / `plugin.dylib`、`assets/**`、`models/**`、`SIGNATURE` | `src/host/package.cpp` `allowed_file` |
| Lua 加载器移除 `require` / `dofile` / `loadfile` / `load` / `package`，只加载包根 `plugin.lua` | `src/host/lua/lua_instance.cpp` |
| 同一 `(id, version)` 在同一平台/架构必须同 `code_hash`；换 runtime 就是另一个包 | `docs/plugin.md` §7.2 |
| 原生插件必须 `trusted: true` | `validate_manifest` |
| 能力必须显式声明；未声明即拒绝 | `validate_manifest`、`permission` |
| 平台差异只在 core 与 manifest `platforms`；插件源码不写平台分支 | `docs/plugin.md` §11 |

### 1.1 已发现的文档/实现漂移

| 漂移 | 以谁为准 |
|---|---|
| core 的 `schema/manifest.schema.json` 里 `role` 枚举仍是旧值（`punct / transform / audio / ink / ui / base`），与 `docs/plugin.md` §8 和 `is_known_role` 不一致 | 以实现 + `docs/plugin.md` §8 为准 |
| core 的 `docs/plugin.md` §3 用 `composition / candidates / hide` 简写；实现和 `schema/cbor/command.cddl` 是 `set_composition / show_candidates / hide_candidates` | 以实现 + `command.cddl` 为准 |
| `docs/plugin.md` 写 `process` / `on_event`，实现一致；但命令字段名以 CDDL 为准 | 以 CDDL 为准 |

本仓库的 `tools/check_plugins` 固定按“实现 + CDDL”校验，避免把漂移带进插件。

## 2 核心决策

1. **一个插件 = 一个目录 = 一个包 = 一个 `id` + SemVer。**
   目录就是构建、测试、签名、发布的最小边界；每个目录可独立构建。
2. **一级目录 = core `role`（功能），二级目录 = 具体插件。**
   core 只认 role，链顺序也由 role 组织；这样不会出现“一个输入法一个巨石插件”，
   新增输入法只是在不同 role 下各加一个插件。
3. **产品不是目录，是链。**
   “拼音 / 五笔 / 手写 / 语音”是 `chains/` 里的组合配方，不是 `plugins/` 下的大目录。
4. **包自包含。**
   运行时不读包外文件；共享代码在构建期编译/合并进包，共享数据在构建期复制进包。
5. **运行时是插件属性，不是目录维度。**
   默认 WASM；低频控制与网络胶水用 Lua；native 只作为可信逃生舱。
6. **平台无分支。**
   插件源码不出现 `#ifdef _WIN32` 一类代码；平台差异由 core 和 manifest 的
   `platforms` 承担。

### 2.1 为什么一级按 role 分

- core 的 `is_known_role` 是唯一内建分类；链配置按数组顺序执行，role 是它的语义标签。
- 拼音 = `parse + translate + rank + format`，五笔 = `parse + translate + rank + format`；
  按 role 分可以复用 `rank` / `format` / `learn` / `predict`，避免每个输入法重复造轮子。
- 产品视角由 `chains/` 提供：想找“拼音”就看 `chains/pinyin/default.yaml`，
  它列出这条链由哪些独立插件组成。
- 如果按输入法分目录，`pinyin/` 会自然长成一个同时做分词、转换、排序、格式化的巨石，
  与 core “core 不含业务、插件单一职责”的边界冲突。

## 3 顶层结构

```text
pime-public-plugin/
├── AGENTS.md
├── README.md
├── TASK.md
├── PROGRESS.md
├── .gitignore
├── .editorconfig
├── CMakeLists.txt                    # 聚合构建入口，不承载业务
├── CMakePresets.json
├── cmake/
│   ├── PimePlugins.cmake             # 发现 plugins/<role>/<id>，统一构建/打包
│   ├── PimePluginSdk.cmake           # 定位 core SDK，校验 ABI / min_core
│   ├── PimePluginWasm.cmake          # wasm32 工具链与 AOT 规则
│   └── toolchains/
│       ├── wasm32-wamr.cmake         # wasi-sdk 工具链
│       └── wasm32-ndk.cmake          # NDK clang + wasm-ld 工具链（本机可用）
│           wasm32-ndk-project.cmake  # project() 后置：模块链接标志
├── docs/
│   ├── plugin.md                     # 插件规格（core 文档副本）
│   ├── structure.md                  # 本文
│   ├── authoring.md                  # 新增插件流程、模板、检查清单
│   └── chains.md                     # 产品链、workspace 契约、锁定
├── plugins/
│   ├── README.md                     # 插件目录索引（由 check_plugins 生成/校验）
│   ├── _template/                    # 模板，不参与 lint 与构建
│   ├── preprocess/
│   │   ├── pime-keymap-default/
│   │   ├── pime-shuangpin-standard/
│   │   └── pime-punctuation-zh/
│   ├── parse/
│   │   ├── pime-pinyin-parse/
│   │   ├── pime-wubi-parse/
│   │   └── pime-cangjie-parse/
│   ├── translate/
│   │   ├── pime-pinyin-translate/
│   │   └── pime-wubi-translate/
│   ├── rank/
│   │   ├── pime-pinyin-rank/
│   │   └── pime-rank-default/
│   ├── format/
│   │   └── pime-candidates-format/
│   ├── recognize/
│   │   ├── pime-handwriting-ncnn/
│   │   └── pime-speech-ncnn/
│   ├── learn/
│   │   └── pime-learn-frequency/
│   ├── predict/
│   │   └── pime-predict-context/
│   └── service/
│       ├── pime-cloud-sync/
│       ├── pime-user-dict/
│       └── pime-update/
├── chains/
│   ├── README.md
│   ├── pinyin/
│   │   ├── default.yaml
│   │   ├── default.lock.yaml
│   │   ├── shuangpin.yaml
│   │   └── shuangpin.lock.yaml
│   ├── wubi/
│   │   ├── 86.yaml
│   │   └── 86.lock.yaml
│   ├── handwriting/
│   │   └── offline.yaml
│   └── speech/
│       └── online.yaml
├── schemes/
│   ├── README.md
│   └── win/                          # 各平台方案包：chain.yaml + scheme.yaml
│       ├── pinyin/                   # Windows 默认
│       ├── english/                  # Windows 默认
│       ├── shuangpin-xiaohe/
│       ├── shuangpin-ziranma/
│       ├── shuangpin-microsoft/
│       ├── shuangpin-sogou/
│       ├── wubi86/
│       └── cangjie/
├── shared/
│   ├── README.md
│   ├── cpp/
│   │   ├── include/pimeplug/
│   │   │   ├── cbor.hpp
│   │   │   ├── utf8.hpp
│   │   │   ├── commands.hpp
│   │   │   ├── dict.hpp
│   │   │   └── workspace.hpp
│   │   └── src/
│   ├── lua/
│   │   ├── cbor.lua
│   │   ├── utf8.lua
│   │   └── commands.lua
│   └── testdata/
├── data/
│   ├── README.md
│   ├── pinyin/                       # 共享词典/语言模型源文件
│   ├── wubi/
│   └── locks/                        # 大文件来源 + sha256
├── tools/
│   ├── README.md
│   ├── check_plugins/                # 目录/清单/包布局/链引用校验
│   ├── chainc/                       # chain YAML -> core CBOR 链配置
│   ├── luabundle/                    # 多 Lua 源 -> 单 plugin.lua（可选）
│   ├── pack_schemes.py               # 方案包组装：链编译 + 插件副本
│   └── fetch_data/                   # 按 hash 拉取大资产/模型
├── tests/
│   ├── README.md
│   ├── host/                         # 最小宿主，链接 pime_core
│   ├── conformance/                  # 所有包的扫描/加载/清单一致性
│   ├── chains/                       # 各产品链端到端
│   ├── bench/                        # budget 校验
│   └── fixtures/                     # 共享 CBOR/事件样例
├── ci/
│   ├── lint.yml
│   ├── build.yml
│   ├── test.yml
│   └── release.yml
├── trust/
│   ├── README.md
│   ├── roots.pem                     # 只放公钥根
│   └── revoked.txt
└── dist/                             # 生成物：包、签名、catalog，不提交
```

## 4 目录职责

| 目录 | 职责 | 不做什么 |
|---|---|---|
| `plugins/<role>/<id>/` | 单个插件的源码、清单、资产、测试；构建出 `pime-plugin-<id>-<version>/` | 不放其他插件、不放产品链配置 |
| `chains/` | 产品配方：有序插件列表、版本、settings、workspace 契约、锁定文件 | 不放插件源码 |
| `schemes/<platform>/<id>/` | 方案包配方：链配方 + 插件 (id, version) 清单，组装成宿主可直接安装的目录 | 不放插件源码、不手拼包 |
| `shared/` | 构建期共享的 CBOR/UTF-8/词典读取等代码 | 不产生运行时共享库 |
| `data/` | 共享数据的规范源；构建时复制进各包 `assets/` | 不作为运行时数据目录 |
| `tools/` | 仓库级校验、链编译、Lua 合并、资产拉取 | 不进入插件包 |
| `tests/host/` | 加载包、构建代、驱动事件的测试宿主 | 不实现输入法业务 |
| `tests/conformance/` | 每个包的清单/布局/加载契约 | 不做业务正确性测试 |
| `tests/chains/` | 每条产品链的端到端行为 | 不单独测某个插件内部 |
| `trust/` | 签名信任根与吊销列表（仅公钥） | 不放私钥 |
| `dist/` | 打包、签名、catalog 输出 | 不提交 |

## 5 插件目录模板

### 5.1 通用模板

```text
plugins/<role>/<plugin-id>/
├── manifest.yaml             # 唯一清单；id/role 与目录一致
├── README.md                 # 契约摘要：事件、workspace、设置、预算
├── CHANGELOG.md              # 按插件独立维护
├── CMakeLists.txt            # 调用 core SDK 的 pime_add_plugin
├── src/                      # wasm / native 实现（Lua 插件没有）
│   └── plugin.cpp
├── plugin.lua                # lua 实现：唯一入口、单文件（wasm/native 没有）
├── assets/                   # 包内资产；manifest.assets 精确列出
├── models/                   # 包内模型；manifest.models 精确列出；不提交
├── tests/
│   ├── cases/                # 输入事件 -> 期望 output
│   ├── golden/               # 冻结的 CBOR/输出
│   └── CMakeLists.txt
├── assets.lock               # 可选：共享资产来源 + sha256
├── models.lock               # 可选：模型来源 + sha256
└── package/                  # 构建产物，忽略
```

规则：

- 一个目录只允许一个 `manifest.yaml`。
- `src/` 与 `plugin.lua` 二选一，由 `runtime` 决定。
- `assets/`、`models/` 中每个文件都必须出现在 manifest 对应数组；反之亦然。
- `tests/cases/` 至少一个用例；没有测试的插件不进入发布。
- `package/` 只由构建生成，不提交。

### 5.2 WASM 插件

```text
plugins/parse/pime-pinyin-parse/
├── manifest.yaml
├── CMakeLists.txt
├── src/plugin.cpp
├── assets/pinyin.bin
├── tests/cases/001-key-a.yaml
└── README.md
```

- 同步链首选 WASM（AOT，单次 ≤2µs）。
- CBOR 输入/输出；`pime_scratch_ptr` / `pime_scratch_cap` 按 manifest
  `scratch_bytes` 声明最大帧长。
- 不链接 WASI 文件/网络；能力只走 host 函数。
- 共享 C++ 代码只能是 header-only 或无 STL/异常/分配的实现，直接编译进本插件。

### 5.3 Lua 插件

```text
plugins/preprocess/pime-punctuation-zh/
├── manifest.yaml
├── CMakeLists.txt            # 只做打包，不编译
├── plugin.lua                # 单文件；不能 require
├── tests/cases/001-punct.yaml
└── README.md
```

- core 移除 `require` / `dofile` / `loadfile` / `load` / `package`，
  **Lua 插件必须是单文件**。
- 需要复用 `shared/lua/` 时，由 `tools/luabundle` 在构建期合并成一个
  `plugin.lua`；包内仍然只有一个 Lua 文件。
- 适合控制、配置、网络/存储胶水、低频 service；同步链需实测预算。

### 5.4 native 插件

```text
plugins/recognize/pime-handwriting-native/
├── manifest.yaml             # runtime: native, trusted: true
├── CMakeLists.txt            # 仅在 PIME_BUILD_TRUSTED_NATIVE=ON 时加入
├── src/plugin.cpp
└── README.md
```

- 只给可信插件（自带 Runtime、平台集成等逃生舱）。
- 必须签名、显式 `trusted: true`、默认构建关闭。
- 不作为生态默认；能用 WASM/Lua 实现的不要用 native。

## 6 命名与映射

| 对象 | 规则 | 示例 |
|---|---|---|
| 一级目录 | 必须是 9 个 role 之一 | `parse/` |
| 插件目录 | 必须等于 manifest `id` | `pime-pinyin-parse/` |
| `id` | `^[a-z0-9][a-z0-9._-]{0,63}$`，以 `pime-` 开头 | `pime-pinyin-parse` |
| `role` | 必须等于父目录名 | `role: parse` |
| `after` | 已知 role 列表；链里排它前面的 role 必须都在其中；`[]` 表示最先 | `after: [preprocess]` |
| 包目录 | `pime-plugin-<id>-<version>`（core 约定） | `pime-plugin-pime-pinyin-parse-1.0.0` |
| 发布标签 | `<id>@<semver>` | `pime-pinyin-parse@1.0.0` |
| alias | 默认 `id`；同 id 并行版本必须显式 | `pinyin-next` |

`tools/check_plugins` 强制校验：

1. `plugins/` 下深度恰好为 2（`role/id`），`_` 开头目录跳过。
2. 目录名 = `manifest.id`；父目录名 = `manifest.role`。
3. `role` ∈ 9 个已知值。
4. `runtime` ∈ `wasm / lua / native`；native 必须 `trusted: true`。
5. `src/` 与 `plugin.lua` 二选一，且与 runtime 匹配。
6. `assets` / `models` 与实际文件精确一致。
7. `capabilities` 全部已知；`requires` 形如 `name@constraint`。
8. `min_core` / `abi` 与当前 SDK 兼容。
9. 无重复 `id`。
10. 包目录只含 core 允许的路径。
11. 插件源码不含平台宏、第三方网络/加密/音频头。
12. 链引用的 `(id, role)` 均存在。
13. `after` 是已知 role 且不重复；链里排在插件前面的 role 都在它的 `after` 中。

## 7 运行时选择

| role | 默认 runtime | 理由 |
|---|---|---|
| `preprocess` | WASM | 同步链，键位/双拼/标点需要低延迟 |
| `parse` | WASM | 同步链，切分/码表 |
| `translate` | WASM | 同步链，词典与 LM |
| `rank` | WASM | 同步链，打分 |
| `format` | WASM | 同步链，候选/组合文本 |
| `recognize` | WASM + `ml.infer` | 手写/语音异步识别 |
| `learn` | Lua 或 WASM | 低频、可异步；按实现复杂度选 |
| `predict` | WASM | 可能进同步链；异步预测可改 Lua |
| `service` | Lua | 网络/存储/签名胶水，低频 |

两种 runtime 都提供同一功能时，它们是**两个插件、两个包**（`(id, version)` 的
`code_hash` 必须稳定，runtime 变了就是另一个包），例如：

- `pime-punctuation-zh`（WASM，性能版）
- `pime-punctuation-zh-lua`（Lua，易维护版）

链里只选其中一个，不要同时挂。

## 8 依赖与共享

- 插件依赖插件：只在 `requires` 写 `pime-xxx@^1.2`，通过 workspace / `ctx` 通信；
  **禁止** include 或链接另一个插件的源码/二进制。
- 插件依赖 core：只使用 `capabilities` 中声明的能力；不得直接开 socket、`dlopen`、
  读密钥文件。
- `shared/cpp/`：构建期源码共享。同步链插件要求无 STL、无异常、无 RTTI、无隐式分配。
- `shared/lua/`：构建期合并进单文件 `plugin.lua`；运行时不加载外部模块。
- 共享数据：`data/` 是规范源，构建时复制到各包 `assets/`；运行时不跨包读取。
  同一个词典被多个包使用时，允许在各自包内各存一份，签名与自包含优先。

## 9 数据、资产与模型

| 位置 | 是否进包 | 是否提交 | 规则 |
|---|---|---|---|
| 插件 `assets/` | 是 | 小文本/映射表提交；大文件由 lock 拉取 | manifest `assets` 精确列出 |
| 插件 `models/` | 是 | **从不提交** | manifest `models` 列出；`models.lock` 记录 URL + sha256 |
| `data/` | 否 | 规范源提交；大文件走 lock | 构建时复制进 `assets/` |
| `data/locks/` | 否 | 提交 | 固定版本、URL、sha256 |
| `dist/` | 产物 | 否 | 包、签名、catalog |

约束：

- 包内文件路径不得越出包根；符号链接拒绝（core 已校验）。
- 模型权重、生产私钥、构建产物不提交。
- `assets` / `models` 的哈希记录在 lock 中，CI 校验下载内容。

## 10 链与 workspace 契约

产品链是 `chains/` 下的 YAML，不是插件。示意：

```yaml
api: 1
id: pinyin-default
description: 全拼默认链
core: ">=1.0.0"
plugins:
  - { id: pime-keymap-default,       role: preprocess }
  - { id: pime-pinyin-parse,         role: parse, settings: { fuzzy: true } }
  - { id: pime-pinyin-translate,     role: translate }
  - { id: pime-pinyin-rank,          role: rank }
  - { id: pime-candidates-format,    role: format }
  - { id: pime-learn-frequency,      role: learn }
workspace:
  - { key: plugin/pime-pinyin-parse/syllables,
      from: pime-pinyin-parse, to: [pime-pinyin-translate] }
  - { key: plugin/pime-pinyin-translate/candidates,
      from: pime-pinyin-translate,
      to: [pime-pinyin-rank, pime-candidates-format, pime-learn-frequency] }
```

- `tools/chainc` 只把 core 认识的字段编译成 CBOR：
  `{plugins:[{id, version?, alias?, role, settings}]}`；`workspace` 等仓库元数据
  留在 YAML，用于校验和测试，不发给 core。
- workspace 键由 core 自动加 `plugin/<id>/` 前缀；跨插件读取用完整键。
- 链文件声明的 `from` / `to` 必须与实际读写的插件一致；`check_plugins` 校验。
- 链顺序即 core 执行顺序；建议按 `preprocess → parse → translate → rank →
  format → learn/predict → service` 排列，异步 service 放最后。
- `*.lock.yaml` 固定 `(id, version, code_hash)` 与 core 版本；发布与回滚以锁为准。

## 11 构建、打包、签名

顶层：

```sh
cmake -S . -B build -DPIME_CORE_DIR=../pime-core -DPIME_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build
```

单个插件可独立构建：

```sh
cmake -S plugins/parse/pime-pinyin-parse -B build/pime-pinyin-parse \
      -DPIME_CORE_DIR=../pime-core
cmake --build build/pime-pinyin-parse
```

打包（沿用 core 的 `pime_add_plugin` 与 `pimepack`）：

```sh
pimepack pack  build/.../package/pime-plugin-pime-pinyin-parse-1.0.0 \
               -o dist/packages/
pimepack sign  dist/packages/pime-plugin-pime-pinyin-parse-1.0.0 \
               --key <private.pem> --key-id pime-official-1
pimepack verify dist/packages/... --root trust/roots.pem
```

规则：

- 包目录只由 core SDK 的 `pime_add_plugin` 组装，不手工拼。
- `dist/`、`package/`、`*.wasm`、`*.dll`、`*.so`、`*.dylib`、`SIGNATURE` 不提交。
- 签名在打包完成后进行；`SIGNATURE` 覆盖规范化 manifest + 排序文件哈希。
- iOS / HarmonyOS 只随包签名，不下载可执行代码；发布流水线按平台限制。

## 12 测试与 CI

| 层 | 内容 |
|---|---|
| 单插件 | `tests/cases/*.yaml`：事件输入 → 期望 output；`tests/golden/` 冻结 CBOR |
| 一致性 | 所有包：manifest 与目录一致、包路径合法、可被 core 扫描/加载 |
| 链 | 每条 `chains/*.yaml`：构建代 → 激活 → 喂事件 → 校验 sink 输出 |
| 预算 | `pimebench` 对比 manifest `budget.sync_us` / `async_ms` |
| 发布 | 打包、签名、验签、catalog 生成 |

CI 任务：

1. `lint`：`check_plugins` + `pimelint` + 文档一致性。
2. `build`：按 runtime 矩阵构建；WASM 用固定工具链，Lua 只做语法检查。
3. `test`：conformance + chains；网络/音频/ML 用例用显式开关隔离。
4. `release`：签名、验签、产物上传。

测试宿主 `tests/host/` 链接 `pime_core`，职责只有三件：加载包、按链构建代、
把 case 转成 CBOR 并对比输出。它不实现任何输入法逻辑。

## 13 版本与发布

- 每个插件独立 SemVer；`MAJOR` 改 state/store/config/能力，`MINOR` 兼容新增，
  `PATCH` 修复。
- `state_schema` / `store_schema` 随数据迁移递增；降级必须显式 pin 且能读旧 schema。
- 链锁定文件固定精确 `(id, version, code_hash)`；升级 = 新锁 + 新代，
  回滚 = 重新激活旧锁对应的代。
- 发布标签：`<plugin-id>@<version>`；产物按平台分目录，WASM/Lua 包可跨平台复用。
- 私钥、模型权重、构建产物永不入库；`trust/` 只放公钥根与吊销列表。

## 14 首批规划

| role | 插件 | runtime | 关键能力 / 依赖 |
|---|---|---|---|
| `preprocess` | `pime-keymap-default` | WASM | `input.read`、`config.read` |
| `preprocess` | `pime-shuangpin-standard` | WASM | `input.read`、`workspace` |
| `preprocess` | `pime-punctuation-zh` | Lua | `input.read`、`output.emit`、`config.read` |
| `parse` | `pime-pinyin-parse` | WASM | `input.read`、`workspace`、`asset.read` |
| `parse` | `pime-wubi-parse` | WASM | 同上 |
| `translate` | `pime-pinyin-translate` | WASM | `workspace`、`asset.read`、`config.read` |
| `translate` | `pime-wubi-translate` | WASM | 同上 |
| `rank` | `pime-pinyin-rank` | WASM | `workspace`、`config.read`、`store.kv` |
| `rank` | `pime-rank-default` | WASM | `workspace` |
| `format` | `pime-candidates-format` | WASM | `workspace`、`output.emit` |
| `recognize` | `pime-handwriting-ncnn` | WASM | `ml.infer`、`asset.read` |
| `recognize` | `pime-speech-ncnn` | WASM | `ml.infer`、`audio.capture`、`audio.opus` |
| `learn` | `pime-learn-frequency` | Lua | `store.kv`、`workspace`、`timer` |
| `predict` | `pime-predict-context` | WASM | `surrounding.read`、`workspace` |
| `service` | `pime-cloud-sync` | Lua | `net.http`、`net.sse`、`crypto.*`、`store.kv` |
| `service` | `pime-user-dict` | Lua | `store.kv`、`workspace` |
| `service` | `pime-update` | Lua | `net.http`、`crypto.*`、`config.read` |

实施顺序建议：先做 `format` / `keymap` / `pinyin-parse`，再补 translate/rank，
最后做 recognize/speech 与服务类。

## 15 扩展方式

| 需求 | 做法 |
|---|---|
| 新增一种输入法 | 在 `parse` / `translate` / `rank` 下各加插件；必要时复用 `format`；在 `chains/` 加配方 |
| 新增一种标点/键位 | 在 `preprocess` 下加一个插件，加入相关链 |
| 新增一种识别 | 在 `recognize` 下加插件，声明 `ml.infer` 或 `net.wss` |
| 新增云服务 | 在 `service` 下加 Lua 插件，声明 `net.*` / `crypto.*` |
| 调整产品组合 | 只改 `chains/` 与锁，不改插件 |
| 新增 role | 不允许在仓库层做；需要 core ABI/语义变更 |
| 新增 runtime | 不允许在仓库层做；需要 core 支持 |
| 拆分/合并插件 | 新 `id` + 新版本；旧链锁保留，按代回滚 |

## 16 实现状态（2026-11-13）

首批已落地：24 个插件、10 条链（含锁）、`shared/` 工具头、`tools/` 四个脚本、
`tests/host` 模拟器与最小宿主、`trust/` 根与吊销列表、`data/` 规范源。
逐项进度见 `PROGRESS.md`，任务勾选见 `TASK.md`。

与上文设计不一致、以本文实现为准的几点：

1. **output workspace 是 map**：插件输出 `{ "workspace": { short_key: value } }`，
   core 按 `plugin/<id>/` 前缀合并；`docs/plugin.md` §5.1 的数组形式未实现。
2. **没有 `ci/`、`tests/chains`、`tests/bench`、`tests/fixtures`、`tests/golden`**：
   单插件用例 + `tests/conformance/run.py` 已能覆盖当前契约；链级端到端测试待
   真实 core 包与 WASM 工具链。
3. **`tests/host/main.cpp` 可选**：仅在 `find_package(pime_core CONFIG)` 成功时
   构建；`tests/host/sim_*` 不需要 core。
4. **Lua 插件无运行时模拟器**：`tests/run_cases.py` 只驱动 WASM 插件的原生
   模拟器，Lua 插件靠 `check_plugins` 与用例描述。
5. **`data/` 仅含 starter 词表**：生产词表/模型按 `data/locks/README.md` 的
   lock 机制分发。
6. **链 id 等于文件名**：`check_plugins` 要求 `id` 与 `<stem>` 一致，且全局唯一，
   因此链 id 使用 `default` / `shuangpin` / `86` / `offline` / `online` / `zh`，
   而不是文档示例里的 `pinyin-default`。
7. **仓颉只有 parse**：首批未列 `pime-cangjie-translate`，`code` 契约已就绪。
8. **在线语音**：首批没有 `net.wss` 识别插件，`chains/speech/online.yaml` 用
   离线 `pime-speech-ncnn` 兜底，并在链描述中注明。
