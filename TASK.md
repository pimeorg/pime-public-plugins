# TASK

> 开工前检查：上一轮仍有 **1 项未勾** —— “CI 接入 NDK wasm32 工具链与真实包
> 构建”。本轮未处理，先提醒；如需在 CI 中复现本轮产物，需要先补这项。

## 本次：简拼短语与原始字母网格

环境：Windows 11 + MinGW-w64（native 用例）、Android NDK 28.2 clang/wasm-ld
（wasm32）、MuMu 12 Android 15 x86_64（实测）。

需求：用户提出“还不够，比如：hbg 就可以拼出还不够，明白吗？不要让我挤牙膏”。
目标不是补单个特例，而是让多字母简拼（尤其是三音节短语）能稳定出词。

- [x] 定位 `hbg` 失败原因：边按位置只看总分前 24 条，长短语被高频单字剪掉
- [x] translate 改为原始字母网格：条目每个音节吃掉 1..音节长度 个字母，
      每段必须是该音节的前缀；不再依赖 parse 的单一预切分
- [x] `syllables` 降为回退输入（无 `raw` 时拼回字母串）
- [x] 边按覆盖音节数分桶，每桶保留 8 条，长短语能进入束搜索
- [x] 内聚奖励改为 `1500 * (覆盖字母数 - 1)`，兼顾整词优势与半音节优先级
- [x] 束宽 8 → 12；`candidates_include` 用例断言
- [x] 用例：`hbg` → 还不够首位；`ysh` 中“有时候”可见；`xian` 中“西安”
      可见；`deg` 中“第二个”可见；`zongguo` 模糊音/关闭模糊音仍正确
- [x] 无命中用例改为 `iiii`（`zzzz` 在字母网格下可拆成四个 z 单字）
- [x] native 用例 54/54 通过；`ctest --test-dir build/native` 通过
- [x] `check_plugins` 0 error / 2 warning；`chainc --check` 通过；
      `tests/conformance` ok
- [x] wasm 包重建，确认没有 `env` 的 memset/memcpy 等未声明导入
- [x] Android 资产同步、APK 重建、`pm clear` 后 MuMu 实测：`hbg` 候选首位
      “还不够”，点击后提交成功
- [x] 更新 README / CHANGELOG / PROGRESS / Android TASK / Android PROGRESS

## 遗留 / 已知限制

- [ ] CI 接入 NDK wasm32 工具链与真实包构建（上一轮遗留）
- 词表为 20k 条紧凑规模；更大词表需同时提高 `kDictBytes` / `kMaxEntries` 与
  manifest `memory_mb`
- 未实现真正的 n-gram 语言模型；整句排序使用词频 + 内聚奖励，长句歧义仍可能
  不如带上下文的 LM（例如 `ysh` 首位仍是“也是”，“有时候”排第 7）
- 首次按键会解析词表（约 430 KB，一次性）；后续按键走索引
- Android 重装 APK 不会覆盖 `files/plugins`，实测前需 `pm clear dev.pime.ime`
  或提升插件版本
- 版本仍为 1.0.0（项目尚未发布首个发行版）
