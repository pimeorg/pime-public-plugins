# AGENTS.md

pime-core 是跨平台输入法内核的**原生库**（dll / so / dylib / framework），不是 App。宿主（TSF、InputMethodKit、InputMethodService、IME Kit、IBus/Fcitx5）加载 core 并实现 HAL 回调。

## 不可违反

1. **业务全在插件**：分词、词典、LM、排序、候选、状态机、学习、标点、键位、schema 语义，core 一律不写。
2. **只用 C/C++**：内部 C++20，对外 C ABI；不引入其他语言运行时。
3. **依赖单向**：`hal → core → host / capabilities / phl / codec`，`host / capabilities / phl → platform`；反向依赖禁止。
4. **平台隔离**：平台头与系统调用只出现在 `src/platform/<os>/`；其他目录出现 `#ifdef _WIN32`、`<windows.h>` 等即违规（`backends/` 的构建适配除外）。
5. **第三方集中**：第三方头只允许在 `src/host/{wasm,lua}`、`src/capabilities/{net,crypto,audio}` 适配层和 `backends/` 内 include，其余经 wrapper。
6. **公共 ABI 纯净**：`include/pime/` 只放 host 侧稳定 C ABI；无 C++、STL、异常、平台头；布尔用 `int`。
7. **ABI 只追加**：struct 字段只追加，`size = sizeof`、`version = ABI major`；不跨边界传 `std::string/vector/function`、异常、RTTI；入口捕获全部异常转 `pime_status`/`NULL`。
8. **句柄不裸奔**：资源句柄 = 索引 + 代际（`0` 无效），不暴露裸指针，跨代使用返回 `STALE`。
9. **缓冲池化**：按最大可能帧长一次性分配，小帧复用大块；热路径不 `malloc/free`；释放归池。
10. **全平台一次到位**：Windows / macOS / iOS / Android / HarmonyOS / Linux 同时可构建，不先做“简化版”。
11. **不静默降级**：只有 `OK` 表示完整生效；任何降级、回退、忽略参数都必须返回明确错误（`UNSUPPORTED` / `STALE` / `NOT_FOUND` …）。
12. **不做防御式编程**：主体逻辑正确即可，不堆极端边界分支；不过度测试，不过分解读文档。

## 文档编写要求

- 记录在xxx环境下用户提出了xxx合理需求
- 记录xxx模块的大致设计思路
- 项目根目录下记录xxx当前项目进度
- 当一个任务开工时，先在项目根目录下重新生成TASK.md（检查TASK是否全勾，否则先提醒我），每完成一个TASK，其勾选项应该被勾选

## 构建与测试

- CMake + `CMakePresets.json`；主产物 `pime_core`，推理后端为独立模块，只暴露 `include/pime/backend.h`。
- 选项：`PIME_BUILD_WASM`、`PIME_BUILD_LUA`、`PIME_BUILD_BACKEND_*`、`PIME_BUILD_TESTS`、`PIME_SANITIZE_*`。
- 测试只覆盖关键契约：CBOR、清单、签名、ABI 布局/枚举、句柄代际、池化缓冲、插件链。
- 不提交生成头、构建产物、生产私钥、模型权重；`version.h` 由 CMake 生成。
- 公共头或导出符号变更必须显式过 `pimeabi`。

## 提交时机

当是个很大的目标时，则需要自主在做的时候就自主提交；当是个小任务时，必须用户说才提交。
