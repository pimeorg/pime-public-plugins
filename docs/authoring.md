# 新增插件

> 结构规则见 `docs/structure.md`；行为规格见 `docs/plugin.md`。
> 命令名、role、能力名以实现和 `pime-core/schema/cbor/*.cddl` 为准。

## 1 五步流程

1. **定 role**：这个插件在链里做哪一件事？只能选一个：
   `preprocess / parse / translate / rank / format / recognize / learn / predict / service`。
2. **定 runtime**：同步链默认 WASM；低频控制/网络胶水用 Lua；native 只给可信逃生舱。
3. **建目录**：`plugins/<role>/<plugin-id>/`，目录名就是 manifest `id`。
4. **写清单 + 实现 + 用例**：按下面的模板；`assets` / `models` 精确列出。
5. **过检查**：`check_plugins`、`pimelint`、单插件测试、所属链测试。

## 2 目录模板

```text
plugins/<role>/<plugin-id>/
├── manifest.yaml
├── README.md
├── CHANGELOG.md
├── CMakeLists.txt
├── src/plugin.cpp            # WASM / native
├── plugin.lua                # Lua：单文件，二选一
├── assets/
├── models/                   # 不提交，按 lock 拉取
├── tests/
│   ├── cases/
│   ├── golden/
│   └── CMakeLists.txt
├── assets.lock
└── models.lock
```

不要把其他插件的源码、二进制或资产直接复制进来；共享代码放 `shared/`，
共享数据放 `data/`，构建时再进入本包。

## 3 清单模板

### 3.1 WASM

```yaml
id: pime-pinyin-parse
version: 1.0.0
abi: { major: 1, minor: 0 }
min_core: "1.0.0"
state_schema: 1
store_schema: 1
runtime: wasm
role: parse
platforms: [windows-x64, windows-arm64, macos-x64, macos-arm64,
            linux-x64, linux-arm64, android-arm64, ios-arm64, harmony-arm64]
entry: pime_process
capabilities: [input.read, output.emit, config.read, asset.read,
               workspace, time, log]
requires: []
assets: [pinyin.bin]
limits:
  memory_mb: 32
  instructions: 10000000
  time_ms: 5
  state_kb: 64
  workspace_kb: 256
  candidates_max: 200
  scratch_bytes: 16384
  max_frame_bytes: 65536
budget: { sync_us: 50, async_ms: 20 }
settings:
  - { key: fuzzy, type: bool, default: true }
```

### 3.2 Lua

```yaml
id: pime-punctuation-zh
version: 1.0.0
abi: { major: 1, minor: 0 }
min_core: "1.0.0"
state_schema: 1
store_schema: 1
runtime: lua
role: preprocess
platforms: [windows-x64, windows-arm64, macos-x64, macos-arm64,
            linux-x64, linux-arm64, android-arm64, ios-arm64, harmony-arm64]
entry: plugin.lua
capabilities: [input.read, output.emit, config.read, log]
requires: []
limits:
  memory_mb: 16
  instructions: 2000000
  time_ms: 5
  state_kb: 64
  workspace_kb: 64
  candidates_max: 20
budget: { sync_us: 50, async_ms: 20 }
```

### 3.3 native（例外）

```yaml
runtime: native
trusted: true
platforms: [windows-x64, macos-arm64, linux-x64]
```

- 仅在 `PIME_BUILD_TRUSTED_NATIVE=ON` 时构建。
- 必须单独签名；默认不进入公开分发包。

## 4 role 怎么选

| role | 做的事 | 典型输入/输出 |
|---|---|---|
| `preprocess` | 键位、双拼、标点、组合键预处理 | key/text → workspace 或 commands |
| `parse` | 把输入切成音节/码元/词法单元 | key/text → workspace |
| `translate` | 音节/码元 → 候选文本 | workspace → workspace |
| `rank` | 候选打分与排序 | workspace → workspace |
| `format` | 生成组合文本、候选列表、命令 | workspace → commands |
| `recognize` | 手写/语音识别 | touch/audio → ml/net → commands |
| `learn` | 频率、用户词、纠错学习 | 事件/workspace → store/workspace |
| `predict` | 上下文预测、下一词 | surrounding → commands |
| `service` | 云同步、更新、用户词典服务 | 事件/net → store/commands |

一个插件只做一件事。需要“做一整套拼音”时，写多个插件并用 `chains/` 组合。

## 5 能力最小化

只声明真正用到的能力；未声明而调用会返回 `PIME_DENIED`。

- 读配置：`config.read`；读资产：`asset.read`；链内共享：`workspace`。
- 持久化：`store.kv`；定时：`timer`；线程：`thread` / `emit` / `channel` / `sync` / `shm`。
- 网络：`net.http` / `net.sse` / `net.ws` / `net.wss`；明文另加 `net.insecure` 授权。
- 加密：`crypto.*`；音频：`audio.pcm` / `audio.opus` / `audio.capture` / `audio.playback`。
- 推理：`ml.infer`；上下文缓存：`ctx`；跨插件共享：`ctx.share`。
- 日志/指标：`log` / `metric`。

不要声明“以后可能用”的能力。

## 6 workspace 契约

- 写入自动加 `plugin/<self-id>/` 前缀；读自己用短键，读别人用完整键。
- 只在同一次链调用内有效；异步 `on_event` 的 workspace 为空，需要跨调用就放 state。
- 键名、值类型、生产者/消费者写入 `chains/*.yaml` 的 `workspace:` 段，并在
  插件 README 中重复声明。
- 失败调用丢弃本次 `workspace_delta`；不要依赖失败插件的写入。

## 7 测试用例

`tests/cases/*.yaml`：

```yaml
name: key-a-commits
event:
  type: key
  keycode: 97
  modifiers: 0
  action: 0
  text: a
context:
  locale: zh-CN
  page_size: 5
expect:
  handled: true
  commands:
    - { type: commit, text: a }
```

要求：

- 至少覆盖：正常路径、状态迁移、`handled=false` 放行、错误/边界输入。
- 异步插件覆盖：事件关联 id、代不匹配丢弃、取消。
- 命令名用 `set_composition` / `commit` / `show_candidates` / `hide_candidates` /
  `beep` / `clear_ink` / `notify` / `audio_out` / `log` / `metric`。
- 预算敏感插件附 `budget` 测试；不要用“平均值”掩盖长尾。

## 8 CMakeLists 骨架

```cmake
cmake_minimum_required(VERSION 3.25)
project(pime_pinyin_parse LANGUAGES CXX)

include(${PIME_SDK_DIR}/cmake/PimePlugin.cmake)

pime_add_plugin(pime_pinyin_parse
    RUNTIME wasm
    MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/manifest.yaml
    SOURCES  src/plugin.cpp
    ASSETS   assets/pinyin.bin
    PACKAGE_DIR ${CMAKE_BINARY_DIR}/package)
```

- `PIME_SDK_DIR` 由仓库的 `PimePluginSdk.cmake` 定位（默认 `${PIME_CORE_DIR}/sdk`）。
- Lua 插件同样调用 `pime_add_plugin`，`RUNTIME lua`，`SOURCES plugin.lua`。
- native 插件在顶层用 `PIME_BUILD_TRUSTED_NATIVE` 控制是否 `add_subdirectory`。

## 9 检查清单

- [ ] 目录 = `plugins/<role>/<id>`；`role` 与父目录一致。
- [ ] `id` 全局唯一，符合 core 正则。
- [ ] `runtime` 与入口文件一致；Lua 只有一个 `plugin.lua`。
- [ ] `assets` / `models` 与文件精确一致；大文件有 lock。
- [ ] `capabilities` 最小化且全部已知。
- [ ] `requires` 只写能力或插件 id@约束；无源码/二进制依赖。
- [ ] `platforms` 与 CI 矩阵一致；native 有 `trusted: true`。
- [ ] 同步链插件无平台宏、无第三方网络/加密/音频头、无 `malloc` 热路径。
- [ ] 至少一个测试用例；所属链端到端通过。
- [ ] README 写明事件、workspace 键、settings、预算。
- [ ] 版本、`state_schema` / `store_schema` 变更符合 SemVer。

## 10 反模式

- ❌ 一个插件同时 parse + translate + rank + format。
- ❌ Lua 插件拆成多个文件或用 `require`（core 已移除）。
- ❌ 把一个输入法的插件堆在 `plugins/pinyin/` 下（应按 role 分）。
- ❌ 插件直接读另一个插件的 `assets/` 或包目录。
- ❌ 插件里写 `#ifdef _WIN32` 或自行开 socket / `dlopen`。
- ❌ 未声明能力却调用；用“降级返回 OK”掩盖。
- ❌ 把模型权重、构建产物、私钥提交进仓库。
