# TASK

## 本次：落地骨架、工具与首批插件

> 上一轮 `工程结构设计` 已完成；`后续：骨架与工具` 的 8 项此前均未勾选，
> 本轮直接接管并展开为下列清单（完成一项勾一项）。

### A 骨架

- [x] 顶层 `CMakeLists.txt`、`CMakePresets.json`
- [x] `cmake/PimePluginSdk.cmake`：定位 core SDK、读取 ABI / core 版本
- [x] `cmake/PimePlugins.cmake`：发现 `plugins/<role>/<id>`、统一构建/打包
- [x] `cmake/PimePluginWasm.cmake` + `cmake/toolchains/wasm32-wamr.cmake`
- [x] `.editorconfig`

### B 工具

- [x] `tools/cbor.py`：确定性 CBOR 编解码（无第三方依赖）
- [x] `tools/check_plugins`：目录/清单/包布局/源码禁用项/链引用校验 + 索引生成
- [x] `tools/chainc`：chain YAML -> core CBOR；`--lock` / `--check`
- [x] `tools/luabundle`：多 Lua 源合并为单 `plugin.lua`
- [x] `tools/fetch_data`：按 lock 拉取资产/模型并校验 sha256

### C 共享代码与数据

- [x] `shared/cpp/include/pimeplug/`：`cbor.hpp` `utf8.hpp` `commands.hpp`
      `workspace.hpp` `dict.hpp` `asset.hpp`（header-only，无 STL/异常）
- [x] `shared/lua/`：`cbor.lua` `utf8.lua` `commands.lua`
- [x] `data/`：拼音/五笔/仓颉词典规范源与 locks 说明

### D 插件（18 个，role 目录 + 独立包）

- [x] `preprocess/pime-keymap-default`（WASM）
- [x] `preprocess/pime-shuangpin-standard`（WASM）
- [x] `preprocess/pime-punctuation-zh`（Lua）
- [x] `parse/pime-pinyin-parse`（WASM）
- [x] `parse/pime-wubi-parse`（WASM）
- [x] `parse/pime-cangjie-parse`（WASM）
- [x] `translate/pime-pinyin-translate`（WASM）
- [x] `translate/pime-wubi-translate`（WASM）
- [x] `rank/pime-pinyin-rank`（WASM）
- [x] `rank/pime-rank-default`（WASM）
- [x] `format/pime-candidates-format`（WASM）
- [x] `recognize/pime-handwriting-ncnn`（WASM + `ml.infer`）
- [x] `recognize/pime-speech-ncnn`（WASM + `ml.infer`/`audio.*`）
- [x] `learn/pime-learn-frequency`（Lua）
- [x] `predict/pime-predict-context`（WASM）
- [x] `service/pime-cloud-sync`（Lua）
- [x] `service/pime-user-dict`（Lua）
- [x] `service/pime-update`（Lua）
- [x] 每个插件：manifest / README / CHANGELOG / CMakeLists / 源码 / 至少一个用例

### E 链、测试、信任与文档

- [x] `chains/`：pinyin default+shuangpin、wubi 86、handwriting offline、speech online、punctuation
- [x] `chains/*.lock.yaml`（`chainc --lock`）
- [x] `tests/host`：最小宿主（包加载 → 建代 → 事件 → 输出）
- [x] `tests/conformance` + `tests/chains`：扫描与端到端
- [x] `trust/roots.pem`、`trust/revoked.txt` 与签名说明
- [x] `plugins/README.md` 索引
- [x] 更新 `README.md`、`PROGRESS.md`、`docs/structure.md` 中与实现不一致处
- [x] 全量校验：`check_plugins`、`chainc --check`、CMake configure、C++ 语法检查

### F 本轮结果与限制

- 全部 40 个单插件用例通过（`tests/run_cases.py`），`check_plugins` 0 error（2 条
  模型占位警告），6 条链 `chainc --check` 通过。
- 无 wasi-sdk：未产出 `.wasm`；`tests/host/main.cpp` 仅在能找到 `pime::core`
  包时编译。
- 手写/语音权重未入库，`hash` 为 `sha256:unfetched`。
- 仓颉仅有 parse；在线语音首批未实现，`chains/speech/online.yaml` 以离线识别兜底。

### G 真实 WASM 产物与联调（2026-11-13）

- [x] wasm32 NDK 工具链（clang + wasm-ld，`--allow-undefined`，非 `-shared`）
- [x] 全部 WASM 插件产出可加载包到 `build/wasm-ndk/package/`
- [x] 修复 `AssetHandle` 截断 64 位句柄导致资产读 `STALE`
- [x] 宿主（core + WAMR）加载五包、build/activate、`nihao` → `你好`
- [x] 清单平台列表补充 `android-x64` / `ios-x64` / `harmony-x64`（x86_64 模拟器）
- [x] 提交阶段契约：parse 在 candidate/space/enter 保留 workspace 码串、清空 `state`
- [x] 组合串清空时 parse 补发 `set_composition("")`；keymap 不再吞控制键
- [x] 新增提交/退格用例（005/006/007），全量 43 个用例通过
- [ ] CI 接入 NDK wasm32 工具链与真实包构建
