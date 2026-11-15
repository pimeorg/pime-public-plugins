# pime-public-plugin

pime 的公共插件仓库。插件是独立的 `input -> output` 数据处理单元，运行在
[pime-core](../pime-core) 提供的 WASM / Lua / native 宿主里；core 不含任何
输入法业务，分词、词典、LM、排序、候选、学习、标点、键位、schema 语义都在这里。

## 结构

```text
plugins/<role>/<plugin-id>/   一个目录 = 一个插件 = 一个签名包
chains/<product>/<variant>.yaml  产品 = 有序插件链
shared/                       构建期共享代码（进包，不产生运行时共享库）
data/                         共享数据规范源（构建时复制进包）
tools/                        目录/清单/链校验，链编译，资产拉取
tests/                        最小宿主、一致性、端到端、预算
```

一级目录是 core 的 9 个 role：

```text
preprocess / parse / translate / rank / format / recognize / learn / predict / service
```

“拼音 / 五笔 / 手写 / 语音”不是目录，而是 `chains/` 里的组合配方。
例如 `chains/pinyin/default.yaml` 把 `pime-pinyin-parse`、`pime-pinyin-translate`、
`pime-pinyin-rank`、`pime-candidates-format` 串成一条产品链。

详细规则见 [`docs/structure.md`](docs/structure.md)：

- [`docs/plugin.md`](docs/plugin.md)：插件规格（生命周期、数据契约、能力、清单、安全）
- [`docs/structure.md`](docs/structure.md)：仓库结构、命名、构建、发布边界
- [`docs/authoring.md`](docs/authoring.md)：新增插件流程、模板、检查清单
- [`docs/chains.md`](docs/chains.md)：产品链、workspace 契约、锁定与回滚

## 构建

需要 pime-core 的 SDK 与已构建的 `pime_core`：

```sh
# Windows + MinGW：必须显式指定生成器，否则 CMake 可能选到 NMake
cmake -S . -B build/native -G "MinGW Makefiles"       -DPIME_CORE_DIR=../pime-core -DPIME_BUILD_TESTS=ON
cmake --build build/native -j
ctest --test-dir build/native
```

WASM 产物用 Android NDK 自带的 clang + wasm-ld 交叉编译：

```sh
cmake -S . -B build/wasm-ndk \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/wasm32-ndk.cmake \
      -DANDROID_NDK_HOME=/path/to/ndk \
      -DPIME_CORE_DIR=../pime-core
cmake --build build/wasm-ndk -j
# 每个插件的可加载包: build/wasm-ndk/package/<id>-<version>/
```

没有该工具链时 WASM 插件只参与用例模拟器（`tests/host/sim_main.cpp`），
不产出 `.wasm`。

单个插件可独立构建：

```sh
cmake -S plugins/parse/pime-pinyin-parse -B build/pime-pinyin-parse \
      -DPIME_CORE_DIR=../pime-core
cmake --build build/pime-pinyin-parse
```

打包与签名沿用 core 的 `pime_add_plugin` / `pimepack`：

```sh
pimepack pack build/.../package/pime-plugin-pime-pinyin-parse-1.0.0 -o dist/packages
pimepack sign dist/packages/pime-plugin-pime-pinyin-parse-1.0.0 \
         --key <private.pem> --key-id pime-official-1
```

## 校验与测试

```sh
python tools/check_plugins          # 目录/清单/链/README 契约
python tools/chainc chains/pinyin/default.yaml --check
python tests/run_cases.py --sim-dir build/native/sim   # 单插件行为用例
python tests/conformance/run.py    # 汇总校验
```

`tests/host/sim_main.cpp` 把 WASM 插件的 C++ 源码按原生编译，链接
`tests/host/sim_stubs.cpp` 的宿主桩，用真实 CBOR 帧驱动 `pime_process`；
Lua 插件目前没有运行时模拟器，只做静态检查。

## 状态

首批 21 个插件、8 条产品链、信任根与校验工具均已落地。WASM 插件已用 NDK
工具链产出真实 `.wasm`，并在 pime-core + WAMR 宿主上跑通默认拼音链；
`tests/host/main.cpp`（链接真实 core）仍按 core 包是否可见决定是否编译。
进度见 [`PROGRESS.md`](PROGRESS.md)，任务见 [`TASK.md`](TASK.md)。
