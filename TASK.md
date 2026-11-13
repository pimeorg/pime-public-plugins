# TASK

> 开工前检查：上一轮仍有 **1 项未勾** —— “CI 接入 NDK wasm32 工具链与真实包
> 构建”。本轮未处理，先提醒；如需在 CI 中复现本轮产物，需要先补这项。

## 本次：多种输入场景实测与排序修复

环境：Windows 11 + MinGW-w64（native 用例）、Android NDK 28.2 clang/wasm-ld
（wasm32）、MuMu 12 Android 15 x86_64（实测）。

需求：用户提出“还不够，建议你自己测试多种情况”。要求主动构造全拼、简拼、
混合、整句、模糊音和设备侧输入，修复系统性问题，而不是等用户逐个报 case。

- [x] 构造 179 条手写用例（全拼 104、简拼 43、混合 23）并统计 top-1/top-8
- [x] 从词表抽样 2/3/4 音节词各 100 条，分别验证完整拼音与首字母
- [x] 构造 16 条全拼句子，验证多词合成
- [x] 定位 `dd` → “的的”：每词惩罚不足以抵消“的”的超高词频
- [x] 内聚奖励 `1500*(letters-1)` → `9000*(letters-1)`，`dd` → 等等 首位
- [x] 回归确认 `zh` → 这、`hbg` → 还不够、`ysh` → 也是 未被长词压过
- [x] 新增 `data/pinyin/extra.txt`：晚安、生日快乐、天安门、女孩、绿色等短语
- [x] 新增 `tools/pinyin_merge`：校验 413 音节表/汉字数，补充表权威、合并幂等
- [x] 词表合并为 20,066 条 / 434 KB，同步到插件资产
- [x] 新增用例：`dd` → 等等首位；`woshizhongguoren` 两词合成
- [x] native 用例 56/56 通过；`ctest --test-dir build/native` 通过
- [x] `check_plugins` 0 error / 2 warning；`chainc --check` 通过；
      `tests/conformance` ok
- [x] wasm 包重建，只有 `env.pime_host_asset` 一个导入，无 memset/memcpy 等未声明导入
- [x] Android 资产同步、APK 重建、`pm clear` 后 MuMu 实测 13 条输入：
      `hbg`、`wanan`、`shengrikuaile`、`tiananmen`、`nvhai`、`lvse`、
      `srkl`、`nhm`、`zgm`、`dhrs`、`nh`、`dd`、`wsm`、`xian` 全部符合预期
- [x] 更新 README / CHANGELOG / SOURCES / tools README / PROGRESS /
      Android TASK / Android PROGRESS
- [x] 提交 pime-public-plugin 与 pime-android

## 遗留 / 已知限制

- [ ] CI 接入 NDK wasm32 工具链与真实包构建（上一轮遗留）
- 纯简拼歧义无法在没有上下文 LM 时消解：`zsh` 首位“这是”、`gz` 首位
  “工作”、`sz` 首位“甚至”；目前保证高频目标词尽量进入前 8 条
- 词表为 20,066 条紧凑规模；更大词表需同时提高 `kDictBytes` / `kMaxEntries`
  与 manifest `memory_mb`
- 整句排序使用词频 + 内聚奖励，长句仍有歧义（`wozaijia` 首位“我再见”）
- 首次按键会解析词表（约 434 KB，一次性）；后续按键走索引
- Android 重装 APK 不会覆盖 `files/plugins`，实测前需 `pm clear dev.pime.ime`
  或提升插件版本
- 版本仍为 1.0.0（项目尚未发布首个发行版）
