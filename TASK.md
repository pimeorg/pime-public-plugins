# TASK

> 开工前检查：上一轮遗留的 5 项中 4 项已落地；CI 按用户要求撤回，重新列为待办。

## 本轮：单前驱 + 方案链记录

环境：Windows 11；Python 3.13 + PyYAML。
需求：插件顺序要能保证；加第 1 个、第 2 个插件都不能回头改已有插件；不需要
排序 UI；删插件后记录仍然正确。

- [x] 41 个插件 manifest 与 `_template`：删除 `after` / `before` role 白名单，
      改成单个默认前驱 `after: <plugin-id>`（`null` = 链头，省略 = role 兜底）
- [x] 11 条产品链与 8 个 Windows 方案链：按现有顺序给每个条目补 `after`，行为零变化
- [x] `chainc`：数组即执行顺序；跳过 `disabled` 墓碑；只校验锚点更早、存在、无环
- [x] `check_plugins`：同步校验；索引展示 role 与默认前驱
- [x] 新增 `tools/scheme_plugins.py add/remove`：只改目标条目，按前驱/role 兜底插入
- [x] 文档：README、`docs/plugin.md`、`docs/chains.md`、`docs/authoring.md`、
      `docs/structure.md`、`schemes/README.md`、`plugins/README.md`

## 上一轮：方案包组装（Windows 宿主）

环境：Windows 11 + Visual Studio 2022 / NDK wasm32；Python 3.13 + PyYAML。
需求：用户指出方案包（链 + 插件副本）应由插件仓库组织，最终随本仓库 release
发布；现在不发 release，先产出本地组装产物，Windows 宿主从 `dist/schemes` 取包。

- [x] `schemes/win/<id>/`：`chain.yaml` + `scheme.yaml`（pinyin/english 默认；
      双拼四方案、五笔 86、仓颉可选）
- [x] `tools/pack_schemes.py`：chainc 编译链 + 拷插件副本；缺包直接失败
- [x] `pime_schemes` 目标，产物 `dist/schemes/`
- [x] 文档：`schemes/README.md`、README、`docs/structure.md`
- [x] 宿主 pime-win 改为从 `../pime-public-plugin/dist/schemes` 取默认两包

## 上一轮：收尾 WeaveText 迁移的 5 项遗留

环境：Windows 11 + MinGW-w64（native 用例）、Python 3 + PyYAML（仓库校验）、
pime-core 1.0.0（ABI 1.0）。本机无 Android NDK，wasm 产物需在具备 NDK 的环境验证。

需求：用户要求“继续，还有 5 项”，即上一轮 TASK 遗留的 5 项。

### 方案与解码

- [x] 全拼音节图增强：模糊音、简拼、悬空简拼、错拼纠正、触点邻键纠正、96 字母上限
- [x] 全拼整句：词图 + 束搜索 + 字符搭配模型 + 次优整句
- [x] 双拼四方案：小鹤、自然码、微软、搜狗
- [x] 九键 / 14 键：数字键码 → 歧义音节图 → 候选
- [x] 五笔 86：四码唯一顶屏、补全、`z` 拼音反查、拼写建议
- [x] 仓颉：码长上限、补全
- [x] 英文：前缀补全、Damerau–Levenshtein 建议、保留原码与大小写
- [x] 英文词表：rime-wanxiang 生成 36,345 条（键去撇号、text 保留拼写，
      `tools/english_dict`）；候选 top-N 收集后再排序，`hel` 不再漏 `hello`
- [x] 中英混输：合法拼音优先；大写英文片段逐候选追加，整串原码保留

### 排序、学习与联想

- [x] 排序：搭配模型、长词内聚、学习词频、近期选择、领域偏置
- [x] 排序：热词偏置（`hotword.update` 事件 + `hot/<词>`，云端验签后由宿主转发）
- [x] 用户学习：追加日志、衰减、近期选择、退格撤销、连续造词、用户二元组
- [x] 学习词作为候选重现（rank 的 `usr/<raw>\x1f<text>`，含连续造词）
- [x] 按应用记忆中/英模式
- [x] 联想四档：用户二元组、接续表、搭配模型续字、末字接续
- [x] 表情/颜文字联想
- [x] 快捷短语与 `{date}` `{time}` `{weekday}` 模板

### 输出与特殊输入

- [x] 繁简转换（词组优先最长匹配）
- [x] `v` 数字/算式：结果、大写金额、中文数字、千分位
- [x] `rq` / `sj` / `xq` / 今天明天昨天现在
- [x] 正文 `算式=` 直接出结果
- [x] 成对标点、中文标点全集（成对映射 + 多字符选项）
- [x] 候选格式：注释、标签、注音、分页、原码提交
- [x] `u` 模式拆字查字、反引号字形筛选
- [x] 短信验证码提取

### 识别与服务

- [x] 手写：笔迹归一化、多字连写/叠写分组、纠正样本学习
- [x] 语音：端点检测、两遍识别、个人热词
- [x] 专业词库包管理
- [x] 模型目录与声明哈希校验（`pime-model-manager`）
- [x] 云端热词：签名校验、只下载不上传
- [x] 个人资料跨设备合并（Lamport 版本）
- [x] 在线翻译能力接入

### 工程

- [x] 新增/增强插件的 manifest、README、CHANGELOG、用例
- [x] 产品链更新与 lock
- [x] `check_plugins`、`chainc --check`、native 用例、conformance 全绿
- [x] `reset` 契约：parse / shuangpin / format 清空组合状态，补 7 条用例
- [x] 更新 README / PROGRESS / 能力矩阵
- [x] 分批提交
- [x] 中英混输：`pinyin-parse.complete`、`english-translate` 合并视图、
      `candidates-format` 优先级、`pinyin-rank` 跳过学习；补 4 条用例
- [ ] CI 接入 NDK wasm32 工具链与真实包构建（已按用户要求撤回，见遗留）

## 当前规模

- 插件 **37** 个，产品链 **11** 条（含 1 条可选服务链）。
- native 用例 **281** 条全部通过；`check_plugins` 0 error / 2 warning
  （手写、语音模型权重未入库）。

## 遗留 / 已知限制

- [ ] CI 接入 NDK wasm32 工具链与真实包构建：本轮提交过的
      `.github/workflows/ci.yml` 已按用户要求撤回，仓库当前没有 CI；需要时重新设计
- Lua 插件无运行时模拟器，`pime-cloud-hotwords` / `pime-translate-online` 只做
  静态检查，网络与签名路径由宿主联调覆盖
- 热词/用户词/学习数据都落在各插件自己的 `store.kv`；跨插件共享靠
  workspace 事件（`hotword.update`）而不是共享存储
- 文本数据以仓库内可复现的小型资产为源；大词表、OpenCC、Unihan 形状表由
  `tools/` 生成或按 lock 拉取，不提交生成物
- 语音/手写模型权重不提交，按 `models.lock` 拉取
- [x] 云端 ASR 三插件（火山 / 腾讯 / 阿里百炼）作为 recognize Lua 插件入库
- [x] `check_plugins` 支持 `compress.gzip`、`secret` 与 `pime.audio.*` 不误判
