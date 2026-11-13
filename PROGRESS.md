# pime-public-plugin 进度

更新：2026-11-13（拼音能力增强）

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

Android 模拟器把候选点击链路的问题逼了出来：parse 最初在 `candidate` 事件里
直接把 `buf` 清零，同一次链调用里 translate/format 已经拿不到码串，提交静默
失败；只保留旧状态又会让下一次输入拼上旧码。现在的契约是“提交阶段”：parse 在
`candidate` / `space` / `enter` 时**仍写出提交前的 workspace 码串**（translate/
format 需要它定位候选），但 **`state` 写空且不发 `set_composition`**；组合串被
清空（退格/esc）时补发 `set_composition("")` 结束宿主组合态。同时 `keymap`
只写 workspace `action`，不再把所有控制键标记 `handled`，否则空组合退格会被吞掉。
三个 parse 插件（全拼/五笔/仓颉）与双拼 preprocess 同步修改，并新增
`005-candidate-commit`、`006-space-commit`、`007-backspace-to-empty` 用例。

### 拼音能力增强（2026-11-13）

用户在 Android 实测后提出：插件能力不够强，参考 WeaveText，让 `ws` 能拼出
“我是”。此前 parse 把 `ws` 切成 `["w","s"]`，translate 直接把音节拼成键
`"ws"` 做前缀查表，词典里没有这个键，于是没有任何候选。WeaveText 的做法是
在音节图上做束搜索，并支持简拼边；本轮按这个思路在插件链内实现，不引入 Rust
或新的运行时。

设计：

1. **词表格式改为音节分隔**：`wo shi\t我是\t225509`。翻译插件不需要再分词，
   一个条目可以覆盖多个输入段。`tools/pinyin_dictgen` 从 rime-wanxiang 的
   `zi.dict.yaml` / `jichu.dict.yaml` 生成去声调、ü→v、通过 413 音节表校验的
   紧凑词表（4k 单字 + 16k 词语，约 430 KB），来源与 CC BY 4.0 署名见
   `data/pinyin/SOURCES.md`。
2. **parse 半音节**：完整音节最长匹配后，若剩余整串是某音节前缀（`zh`、`h`），
   保持为一段；否则按单字母切（`ws` → `w`,`s`）。translate 把前缀段按缺字母数
   罚分匹配任意音节。
3. **translate 音节网格 + 束搜索**：段网格上，词典条目覆盖连续音节；匹配支持
   完整、前缀、单字母简拼、模糊音（`zh/z`、`ch/c`、`sh/s`、`n/l`、`an/ang`、
   `en/eng`、`in/ing`、`ian/iang`、`uan/uang`、`ü/u`）。束宽 8，每个位置最多
   24 条边；分数 = `log2(词频) - 语料归一化常数 + 多音节内聚奖励 - 简拼/模糊
   罚分`。归一化让“一个整词”与“两个单字”可比，避免单字词频之和总是压过词典词。
4. **加载索引**：词表加载时按首字母建连续桶并缓存 `Entry`（键、词、词频分、
   音节数、首音节长度），按键路径只扫相关桶；`g_dict` 扩到 1 MB。
5. **freestanding wasm 内存助手**：新代码触发 clang 生成 `memset/memcpy` 调用，
   `-nostdlib` 下变成 `env` 未定义导入，WAMR 加载时警告、首次按键返回
   `CORRUPT`。新增 `shared/cpp/include/pimeplug/memory.hpp`，在 `__wasm32__` 下
   提供弱定义；native 构建仍链接 libc，不受影响。

实测（MuMu 12 / Android 15 / x86_64）：

- `ws` → 候选 `我是 我说 晚上 为啥 无数`，首位“我是”；点击后搜索框提交“我是”。
- `woshi` → 首位“我是”；`nh` → 首位“你好”。
- 首次加载 20k 词表无 WAMR 警告；按键链路正常。

### 简拼短语与原始字母网格（2026-11-13 第二轮）

用户在上一轮实测后继续提出：“还不够，比如：hbg 就可以拼出还不够，明白吗？
不要让我挤牙膏”。要求不是只补一个特例，而是让多字母简拼能稳定命中短语。

定位到两个结构性原因：

1. 边剪枝按位置只看总分前 24 条。`hai bu gou` 这类三音节短语的归一化分数
   低于大量高频单字/双字边，在进入束搜索前就被丢掉，所以 `hbg` 根本没有
   “还不够”这条路径。
2. parse 的贪婪切分只给一种边界。`ysh` 会被切成 `y + sh`，`deg` 切成
   `de + g`，`xian` 切成 `xian`；虽然这些切分对显示合理，但会挡掉
   `y + s + h`（有时候）、`d + e + g`（第二个）、`xi + an`（西安）等解释。

设计：

1. **translate 改用原始字母网格**：节点是 `raw` 的每个字母位置；词典条目的
   每个音节依次吃掉 1..音节长度 个字母，每段必须是该音节的前缀。这样完整
   音节、半音节、简拼和多重边界都在同一张图里，不需要先选切分。`syllables`
   只在没有 `raw` 时作为回退（拼回字母串）。
2. **按音节数分桶保留边**：每个位置按条目覆盖的音节数分桶，每桶保留前 8 条。
   高频单字不再挤掉长短语；`hbg` 的三音节路径能进入束搜索。
3. **内聚奖励改为按覆盖字母数**：`1500 * (letters - 1)`。三音节整词仍能压过
   三个单字（还不够 vs 和+不+过），但 `zh` 不会被拆成 `z+h` 的“之后”反超
   “这”；`xian` 的“先”也仍排在“西安”之前。
4. 束宽 8 → 12；新增 `candidates_include` 断言，允许用例只断言“某个候选
   必须出现”，不锁死完整排序。

验证：

- native 用例 54/54 通过。新增 `hbg` → 还不够首位；`ysh` 首位也是且
  “有时候”可见；`xian` 首位想且“西安”可见；`deg` 中“第二个”可见；
  `zongguo` 关闭模糊音仍首位总过。
- 抽样 150 个词频最高的三音节词（取首字母组合，去重）：146 个在原
  `max_candidates=8` 内可见，131 个首位命中；修复前 `ysh/zsh/nsh/deg` 这类
  被贪婪切分挡住的组合不可见。
- MuMu 12 实测：清空应用数据后（插件包只在首次启动复制，重装 APK 不会覆盖
  `files/plugins`），输入 `hbg`，候选栏首位“还不够”，点击后搜索框提交成功。

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
- 拼音只有词频 + 内聚奖励，没有真正的 n-gram 上下文 LM；长句歧义排序不如
  WeaveText 的 gram 模型。
- 拼音词表为 20k 条紧凑规模；五笔/仓颉仍是 starter 规模。
- 仓库许可证待定；拼音词表数据来源的 CC BY 4.0 署名见
  `data/pinyin/SOURCES.md`。
