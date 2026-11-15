# WeaveText 输入法能力 → pime 插件映射

来源：`T:\temp\WeaveText`（Rust 内核 `core/`）。本文是迁移的追踪表：每一项
WeaveText 能力对应哪个 pime 插件、落在哪个 role、状态如何。

> 原则：只迁移**输入法核心能力**。Android/macOS 界面、Compose 设置页、语音包
> 下载 UI、WeaveLink 传输通道、GitHub 插件市场 UI 属于宿主/伴随服务，不进入
> 插件；其中与输入行为相关的部分（模型目录、签名热词、个人资料合并）以
> `service` 插件保留接口。
>
> 状态：**增强** = 在既有插件上补齐；**新增** = 本轮/前几轮新建插件；
> **待做** = 尚未实现；**不迁移** = 明确不做。

## 1 输入方案与解码

| WeaveText | 能力 | pime 插件 | role | 状态 |
|---|---|---|---|---|
| `graph.rs` | 全拼音节图、模糊音、简拼、悬空简拼 | `pime-pinyin-parse` | parse | 增强 |
| `graph.rs` | 错拼纠正（颠倒/漏/多/换）、触点邻键纠正 | `pime-pinyin-parse` | parse | 新增 |
| `decoder.rs` | 词图、束搜索、整句、次优整句 | `pime-pinyin-translate` | translate | 增强 |
| `gram.rs` | 字符搭配模型（n-gram） | `pime-pinyin-translate` / `pime-pinyin-rank` | translate/rank | 新增 |
| `decoder.rs` | 中英混输、原码候选 | `pime-pinyin-translate` / `pime-english-translate` | translate | 增强（整串原码；分段英文片段待做） |
| `shuangpin.rs` | 小鹤/自然码/微软/搜狗双拼 | `pime-shuangpin-standard` | preprocess | 增强 |
| `t9.rs` | 九键、14 键歧义解码 | `pime-t9-parse` + `pime-t9-translate` | parse/translate | 新增 |
| `table.rs` | 五笔/英文码表、精确+补全+纠错 | `pime-wubi-translate` | translate | 增强 |
| `table.rs` | 五笔 `z` 拼音反查 | `pime-wubi-translate` | translate | 新增 |
| `session.rs` | 96 字母组合上限、提交语义 | `pime-pinyin-parse` | parse | 增强 |
| `weave-dict/shape.rs` | 字形筛选、`u` 模式拆字 | `pime-shape-parse` + `pime-shape-translate` | parse/translate | 新增 |
| `weave-dict/hand*.rs` | 手写模板/笔画归一化/多字 | `pime-handwriting-ncnn` | recognize | 接口已有，增强待做 |
| `asr.rs` | 端点检测、两遍识别、热词 | `pime-speech-ncnn` | recognize | 接口已有，增强待做 |

## 2 排序、学习、联想

| WeaveText | 能力 | pime 插件 | role | 状态 |
|---|---|---|---|---|
| `decoder.rs` | 长词内聚、词频、次优句 | `pime-pinyin-rank` | rank | 增强 |
| `userdict.rs` | 只追加日志、衰减、近期选择 | `pime-learn-user-dict` | learn | 新增 |
| `userdict.rs` | 退格撤销、连续造词、用户二元组 | `pime-learn-user-dict` + `pime-pinyin-rank` | learn/rank | 新增 |
| `session.rs` | 按应用记住中/英 | `pime-app-mode` | learn | 新增 |
| `predict.rs` | 联想四档：用户对/接续表/搭配续字/末字 | `pime-predict-context` | predict | 增强 |
| `convert.rs` | 表情联想、颜文字 | `pime-emoji-suggest` | predict | 新增 |
| `personal.rs` | 已选/降权、Lamport 合并 | `pime-personal-merge` | service | 新增 |
| — | 快捷短语与日期模板 | `pime-shortcuts` | translate | 新增 |
| — | 候选注音 | `pime-candidates-format` | format | 增强 |
| `cloud.rs` | 热词偏置进入排序 | `pime-pinyin-rank` | rank | 待做（热词服务已就绪，跨插件存储不共享，需要共享用户词典层） |

## 3 输出与特殊输入

| WeaveText | 能力 | pime 插件 | role | 状态 |
|---|---|---|---|---|
| `convert.rs` | 简→繁（词组最长匹配） | `pime-hans-hant` | translate | 新增 |
| `special.rs` | `v` 数字、算式、大写金额、中文数字 | `pime-special-candidates` | translate | 新增 |
| `special.rs` | `rq/sj/xq`、今天/明天/昨天/现在 | `pime-special-candidates` | translate | 新增 |
| `session/editing.rs` | 正文 `算式=` 出结果 | `pime-special-candidates` | translate | 新增 |
| — | 成对标点、中文标点全集 | `pime-punctuation-zh` | preprocess | 增强（成对映射；光标居中受命令 ABI 限制） |
| `session.rs` | 候选标签、分页、原码提交 | `pime-candidates-format` | format | 增强 |
| — | 短信验证码提取 | `pime-clip-code` | predict | 新增 |

## 4 数据与服务

| WeaveText | 能力 | pime 插件 | role | 状态 |
|---|---|---|---|---|
| `dictpacks.json` | 14 个专业词库按需启停 | `pime-dict-packs` | service | 新增 |
| `cloud.rs` | Ed25519 签名热词、只下载 | `pime-cloud-hotwords` | service | 新增 |
| `models/catalog.json` | 语音/手写模型目录与校验 | `pime-model-manager` | service | 待做（当前由构建期 `models.lock` + core 校验承担） |
| `translate/` | 在线翻译 | `pime-translate-online` | service | 新增 |
| `weave-link` | 跨设备文字/文件 | — | — | 不迁移：伴随服务，非输入行为 |

## 5 宿主承担、不进入插件的部分

- 键盘绘制、主题、悬浮/分体布局、单手、触感与音效。
- 候选栏手势、剪贴板面板、光标面板。
- 语音录制、权限、模型文件下载 IO。
- WeaveLink 的 mDNS/SPAKE2/Noise/QUIC。
- Android/macOS 设置页与应用集成。

这些能力通过 core 的 HAL/commands 暴露给插件，但插件本身不实现 UI。

## 6 架构约束带来的取舍

1. **存储按插件隔离**：`store.kv` 不能跨插件读。用户学习的跨词统计在
   `pime-learn-user-dict`，同码串召回与最近选择加分在 `pime-pinyin-rank`
   （只有它既看得到选择、又在格式化之前产出候选）。连续造词写进学习插件，
   要作为候选重现需要共享用户词典层（与热词偏置同一前置）。
2. **命令 ABI 无“提交后移动光标”**：成对标点只做成对映射；自动补全括号并居中
   光标不做近似插入。
3. **Lua 无运行时模拟器**：`pime-cloud-hotwords` / `pime-translate-online` 只做
   静态检查，由宿主联调覆盖网络与签名路径。
