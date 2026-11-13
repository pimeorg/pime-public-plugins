# TASK

> 开工前检查：上一轮仍有 **1 项未勾** —— “CI 接入 NDK wasm32 工具链与真实包
> 构建”。本轮未处理，先提醒；如需在 CI 中复现本轮产物，需要先补这项。

## 本次：增强拼音插件能力（简拼 / 整句 / 真实词表）

环境：Windows 11 + MinGW-w64（native 用例）、Android NDK 28.2 clang/wasm-ld
（wasm32）、MuMu 12 Android 15 x86_64（实测）。

需求：用户提出“插件能力不够强，参考 WeaveText，把当前插件能力丰富下，比如
`ws` 能够拼出我是”。

- [x] 词表生成器 `tools/pinyin_dictgen`：rime-wanxiang → 紧凑词表
      （去声调、ü→v、音节表校验、词频裁剪、按首字母排序）
- [x] `data/pinyin/pinyin.dict` 升级为 20k 条（4k 单字 + 16k 词语）
- [x] `data/pinyin/SOURCES.md`：来源、提交、CC BY 4.0 署名、生成命令
- [x] 同步词表到 `plugins/translate/pime-pinyin-translate/assets/pinyin.dict`
- [x] parse：尾部半音节作为一段（`zh`/`h`），简拼按字母切段（`ws` → `w`,`s`）
- [x] translate：音节级匹配（完整 / 前缀 / 简拼 / 模糊音）
- [x] translate：首字母桶索引 + `Entry` 表，避免按键热路径线性扫全表
- [x] translate：音节网格有界束搜索，支持整句合成
- [x] translate：统一打分（log2 词频、语料归一化常数、多音节词内聚奖励）
- [x] freestanding 内存助手 `shared/cpp/include/pimeplug/memory.hpp`
- [x] 用例：`ws` → 我是、`nh` → 你好、`wsm` → 为什么/我什么、`zongguo` →
      中国、关闭模糊音、半音节前缀、简拼切分
- [x] native 用例全绿（`ctest --test-dir build/native`）
- [x] `check_plugins` 0 error / 2 warning；全部链 `chainc --check` 通过；
      `tests/conformance` ok
- [x] wasm 包重建（translate/parse），确认无 `env` 的 memset/memcpy 导入
- [x] Android 资产同步、APK 重建、MuMu 实测：`ws` 候选首位“我是”并提交成功；
      `woshi` → 我是；`nh` → 你好
- [x] 更新 README / CHANGELOG / PROGRESS / data 说明

## 遗留 / 已知限制

- [ ] CI 接入 NDK wasm32 工具链与真实包构建（上一轮遗留）
- 词表为 20k 条紧凑规模；更大词表需同时提高 `kDictBytes` / `kMaxEntries` 与
  manifest `memory_mb`
- 未实现真正的 n-gram 语言模型；整句排序使用词频 + 内聚奖励，长句歧义仍可能
  不如带上下文的 LM
- 首次按键会解析词表（约 430 KB，一次性）；后续按键走索引
- 版本仍为 1.0.0（项目尚未发布首个发行版）
