# pime-public-plugin 进度

更新：2026-11-13

## 环境与需求

用户在 Windows 11 + MinGW-w64 环境下提出两轮需求：

1. **工程结构**：基于 pime-core 的实现与文档设计插件仓库结构，要求插件逐个
   独立、按功能分类、与 core 的代/链/包/签名/能力模型一致。
2. **落地首批插件**：按文档把插件完整写出来（不遗漏），允许自主设计与对不合理
   之处自主决策，但不要过度设计/实现。
3. **真实产物与联调**：用 Android NDK 里的 clang/wasm-ld 产出真正的 `.wasm`
   （本机无 wasi-sdk），并在宿主上把五个第一方插件组成的拼音链跑通。

核心约束（来自 `AGENTS.md`）：业务全在插件、core 只提供宿主；只用 C/C++；依赖
单向；平台隔离；WASM 插件不链接第三方；Lua 单文件。

## 已完成

### 结构（第一轮）

| 内容 | 位置 |
|---|---|
| 仓库总结构、职责边界、构建/发布流程 | `docs/structure.md` |
| 新增插件流程、目录/清单模板、检查清单 | `docs/authoring.md` |
| 产品链格式、workspace 契约、锁定与回滚 | `docs/chains.md` |
| 仓库总览与快速开始 | `README.md` |
| 任务清单 | `TASK.md` |

### 骨架与工具（第二轮）

| 内容 | 位置 |
|---|---|
| 顶层 CMake、Presets、SDK 定位、插件发现/构建/打包 | `CMakeLists.txt`、`cmake/` |
| 确定性 CBOR 编解码 | `tools/cbor.py` |
| 目录/清单/源码禁用项/链/README 契约校验 + 索引生成 | `tools/check_plugins` |
| chain YAML → core CBOR；`--lock` / `--check` | `tools/chainc` |
| 多 Lua 源合并为单 `plugin.lua` | `tools/luabundle` |
| 按 lock 拉取/校验资产与模型 | `tools/fetch_data` |
| 共享 C++ 头（CBOR/UTF-8/命令/workspace/词典/资产/ml 张量） | `shared/cpp/include/pimeplug/` |
| 共享 Lua 模块（CBOR/UTF-8/命令） | `shared/lua/` |
| 规范数据源与锁说明 | `data/` |
| 信任根与吊销列表 | `trust/` |

### 插件（18 个，全部含 manifest / README / CHANGELOG / CMakeLists / 源码 / 用例）

| role | 插件 | runtime |
|---|---|---|
| preprocess | `pime-keymap-default` | WASM |
| preprocess | `pime-shuangpin-standard` | WASM |
| preprocess | `pime-punctuation-zh` | Lua |
| parse | `pime-pinyin-parse` | WASM |
| parse | `pime-wubi-parse` | WASM |
| parse | `pime-cangjie-parse` | WASM |
| translate | `pime-pinyin-translate` | WASM |
| translate | `pime-wubi-translate` | WASM |
| rank | `pime-pinyin-rank` | WASM |
| rank | `pime-rank-default` | WASM |
| format | `pime-candidates-format` | WASM |
| recognize | `pime-handwriting-ncnn` | WASM |
| recognize | `pime-speech-ncnn` | WASM |
| learn | `pime-learn-frequency` | Lua |
| predict | `pime-predict-context` | WASM |
| service | `pime-cloud-sync` | Lua |
| service | `pime-user-dict` | Lua |
| service | `pime-update` | Lua |

### 真实 WASM 产物（第三轮）

| 内容 | 位置 |
|---|---|
| wasm32 交叉工具链（Android NDK clang + wasm-ld） | `cmake/toolchains/wasm32-ndk.cmake`、`wasm32-ndk-project.cmake` |
| 全部 WASM 插件构建为可加载包（manifest + `plugin.wasm` + assets） | `build/wasm-ndk/package/` |
| 资产句柄宽度修复：`AssetHandle.id` 保留 64 位 `(generation<<32)|index` | `shared/cpp/include/pimeplug/asset.hpp` |

工具链不使用 `-shared`，改用 `-Wl,--allow-undefined`：插件必须自带内存与
`__stack_pointer`，未定义符号保持为 `env` 导入，由 core 注册的原生函数解析。
NDK 是本机已有的 wasm32 clang/链接器来源，因此不依赖 wasi-sdk。

Android 实测还暴露了清单的平台列表问题：原先只写 `android-arm64`，在 x86_64
模拟器上 `Core::plugin_load` 的平台过滤直接返回 `PIME_NOT_FOUND`。18 个清单与
模板、文档示例已补上 `android-x64`、`ios-x64`、`harmony-x64`。

### 链与测试

| 内容 | 位置 |
|---|---|
| 全拼默认 / 双拼 / 五笔 86 / 离线手写 / 语音 / 中文标点 | `chains/`（含 `.lock.yaml`） |
| 原生模拟器：编译 WASM 插件源码 + 宿主桩，跑真实 CBOR 帧 | `tests/host/sim_main.cpp`、`sim_stubs.cpp` |
| 用例驱动（YAML → CBOR → 子集比对） | `tests/run_cases.py` |
| 最小宿主（链接真实 core，可选） | `tests/host/main.cpp` |
| 汇总一致性检查 | `tests/conformance/run.py` |
| 模板 | `plugins/_template/` |

## 设计思路

### 插件边界

一个目录 = 一个插件 = 一个签名包；链只做组合。WASM 插件是 header-only 的
`pimeplug` 工具层 + 单个 `plugin.cpp`，不依赖 STL/异常/RTTI；Lua 插件是单
`plugin.lua`，只用 core 注入的 `pime.*`。

### workspace 契约

写入用短键，core 自动加 `plugin/<id>/` 前缀；读取用完整键。跨插件数据流在链
文件里显式声明 `from`/`to`，并由 `check_plugins` 校验 README 是否记录了这些键。

### 资源与降级

资产按需加载并缓存；缺失返回 `PIME_NOT_FOUND`，模型后端缺失返回
`PIME_UNSUPPORTED`，不返回 `OK`。只在返回 `OK` 时认为完整生效。

### 测试

用例把插件源码按原生编译成 `sim_<id>`，链接宿主桩（资产可读、ml 返回固定张量），
用 CBOR 帧验证行为；Lua 插件只做静态检查与用例描述。真实 `.wasm` 由 NDK 工具链
产出后，在 `pime-core` + WAMR 的 Windows 宿主上跑完整链验证（见下）。

## 真实链验证（2026-11-13）

宿主：`pime-core`（WAMR 2.4.1 快速解释器）+ 本仓库五个 WASM 包
（keymap-default、pinyin-parse、pinyin-translate、pinyin-rank、candidates-format）。

结果：五个包 `load: OK`，`build/activate: OK`；连打 `nihao` 时组合串逐键推进
（n → ni → nih → niha → nihao），候选从 `[0]你 [1]你好 …` 收敛到 `[0]你好`，
选择 0 后 `commit: 你好`。

联调确认了 `AssetHandle` 必须保持 64 位：core 的资产句柄是
`(generation << 32) | index`，SDK 早期截成 32 位后所有读操作都返回 `STALE`。

## 发现的 core 文档/实现漂移

1. `pime-core/schema/manifest.schema.json` 的 `role` 枚举仍是旧值
   （`punct / transform / audio / ink / ui / base`），与 `docs/plugin.md` §8 和
   `src/codec/manifest.cpp` `is_known_role` 不一致。
2. `docs/plugin.md` §3 的命令名是简写（`composition / candidates / hide`），
   实现与 `schema/cbor/command.cddl` 是
   `set_composition / show_candidates / hide_candidates`。
3. 文档的 output 帧写 `workspace: [ {k, v} ]`（数组），core 实际按 map 合并；
   本仓库插件输出 map。
4. `LuaInstance::init` 先 `pime_init` 再 `permission_.declare`，因此宿主能力调用
   必须懒加载（首次 `process`），不能在 init 里发起资产/网络请求。
5. core 的 `pime_add_plugin` 没有给 WASM 目标加 core 头路径；本仓库在
   `pime_add_cpp_plugin` 里补上。

## 待完成 / 已知限制

- WASM 构建走 NDK 工具链（`cmake/toolchains/wasm32-ndk*.cmake`）；wasi-sdk 不再是
  必需前置。未在 CI 中配置该工具链。
- 无 Lua 运行时模拟器：Lua 插件只做静态检查。
- 手写/语音模型权重未入库，manifest `hash` 为 `sha256:unfetched`。
- 仓颉只有 parse，没有 translate（首批未列）。
- 在线语音（`net.wss`）插件未列入首批，`chains/speech/online.yaml` 以离线识别兜底。
- native 插件与 iOS/HarmonyOS 平台专用打包未展开。
- 许可证待定。
