# 插件索引

> 由 `python tools/check_plugins --write-index` 生成，不要手改。

| role | 可排在其前 | 可排在其后 | id | runtime | version | 说明 |
|---|---|---|---|---|---|---|
| `format` | 任意 | `learn`, `predict`, `service` | `pime-candidates-format` | wasm | 1.5.1 | 候选与组合收尾：展示候选、空格/数字选词、Esc 取消 |
| `learn` | `preprocess`, `parse`, `translate`, `rank`, `format`, `recognize`, `learn`, `predict` | 任意 | `pime-app-mode` | wasm | 1.0.0 | 学习：按应用记忆中/英输入模式 |
| `learn` | `preprocess`, `parse`, `translate`, `rank`, `format`, `recognize`, `learn`, `predict` | 任意 | `pime-learn-frequency` | lua | 1.0.0 | 学习：按提交记录词频，写入插件私有 KV |
| `learn` | `preprocess`, `parse`, `translate`, `rank`, `format`, `recognize`, `learn`, `predict` | 任意 | `pime-learn-user-dict` | wasm | 1.0.1 | 用户学习：追加日志、衰减、近期选择、退格撤销、连续造词、用户二元组 |
| `parse` | `preprocess` | 任意 | `pime-cangjie-parse` | wasm | 1.0.1 | 仓颉：累积字母码并写入 workspace |
| `parse` | `preprocess` | 任意 | `pime-english-parse` | wasm | 1.0.1 | 英文：累积字母与撇号，保留原码大小写 |
| `parse` | `preprocess` | 任意 | `pime-pinyin-parse` | wasm | 1.1.5 | 全拼解析：音节切分、自动纠错、触点邻键纠正与预编辑 |
| `parse` | `preprocess`, `parse`, `pime-pinyin-parse` | 任意 | `pime-shape-parse` | wasm | 1.0.0 | 解析：识别 u 拆字前缀与反引号字形筛选前缀 |
| `parse` | `preprocess` | 任意 | `pime-t9-parse` | wasm | 1.0.1 | 九键/14 键：累积键码（1 为音节分隔）并写入 workspace |
| `parse` | `preprocess` | 任意 | `pime-wubi-parse` | wasm | 1.1.1 | 五笔：累积字母码并写入 workspace 供词表翻译 |
| `predict` | `preprocess`, `parse`, `translate`, `rank`, `predict` | 任意 | `pime-clip-code` | wasm | 1.0.0 | 预测：从光标周边/剪贴板提取 4-8 位短信验证码 |
| `predict` | `preprocess`, `parse`, `translate`, `rank` | 任意 | `pime-emoji-suggest` | wasm | 1.0.0 | 预测：按上下文末词联想表情 |
| `predict` | `preprocess`, `parse`, `translate`, `rank`, `format`, `learn`, `predict`, `pime-learn-user-dict` | 任意 | `pime-predict-context` | wasm | 1.1.0 | 预测：用户二元组、接续表、字符搭配模型、末字续字四档联想 |
| `preprocess` | （必须最先） | 任意 | `pime-keymap-default` | wasm | 1.3.0 | 默认键位：把控制键映射为链内动作，文本键放行 |
| `preprocess` | `preprocess` | 任意 | `pime-punctuation-zh` | wasm | 1.2.0 | 中文标点：半角标点转全角，引号样式可配置 |
| `preprocess` | `preprocess`, `pime-keymap-default` | 任意 | `pime-shuangpin-standard` | wasm | 1.1.1 | 双拼四方案（小鹤/自然码/微软/搜狗）：两键合成音节 |
| `rank` | `preprocess`, `parse`, `translate`, `pime-pinyin-translate` | 任意 | `pime-pinyin-rank` | wasm | 1.4.1 | 拼音候选重排：短词加权后按分数降序 |
| `rank` | `preprocess`, `parse`, `translate` | 任意 | `pime-rank-default` | wasm | 1.0.0 | 通用重排：按分数降序、过滤低分、截断 |
| `recognize` | `preprocess` | 任意 | `pime-asr-funasr` | lua | 1.0.0 | 语音识别：阿里百炼 FunASR 实时流式识别（DashScope WebSocket） |
| `recognize` | `preprocess` | 任意 | `pime-asr-tencent` | lua | 1.0.0 | 语音识别：腾讯云实时语音识别 V2（WebSocket） |
| `recognize` | `preprocess` | 任意 | `pime-asr-volc` | lua | 1.0.0 | 语音识别：火山引擎 bigmodel_async 在线流式识别 |
| `recognize` | `preprocess` | 任意 | `pime-handwriting-ncnn` | wasm | 1.1.0 | 手写识别：笔迹点经 ml.infer 得到候选（离线） |
| `recognize` | `preprocess` | 任意 | `pime-speech-ncnn` | wasm | 1.1.0 | 语音识别：PCM 经 ml.infer 得到候选（离线） |
| `recognize` | `preprocess` | 任意 | `pime-speech-sherpa` | native | 1.0.0 | 语音识别：WeaveText 同款 sherpa-onnx 离线识别（流式模型，边说边出字） |
| `service` | 任意 | 任意 | `pime-cloud-hotwords` | lua | 1.0.0 | 服务：Ed25519 签名热词，只下载不上传 |
| `service` | 任意 | 任意 | `pime-cloud-sync` | lua | 1.0.0 | 服务：词频表增量同步（HTTP，可关闭） |
| `service` | 任意 | 任意 | `pime-dict-packs` | wasm | 1.0.0 | 服务：专业词库包按需启停 |
| `service` | 任意 | 任意 | `pime-model-manager` | wasm | 1.0.0 | 服务：语音/手写/语言模型目录、启停与声明哈希校验 |
| `service` | 任意 | 任意 | `pime-personal-merge` | wasm | 1.0.0 | 服务：个人资料跨设备合并（Lamport 版本） |
| `service` | 任意 | 任意 | `pime-translate-online` | lua | 1.0.0 | 服务：在线翻译能力接入 |
| `service` | 任意 | 任意 | `pime-update` | lua | 1.0.0 | 服务：检查新版本（HTTP 源，包签名由 core 校验） |
| `service` | 任意 | 任意 | `pime-user-dict` | lua | 1.0.0 | 服务：用户词库记录、查询与删除 |
| `translate` | `preprocess`, `parse`, `pime-cangjie-parse` | 任意 | `pime-cangjie-translate` | wasm | 1.0.0 | 仓颉词典翻译：字母码查词表，整码在前、补全在后 |
| `translate` | `preprocess`, `parse`, `translate`, `rank`, `pime-english-parse` | 任意 | `pime-english-translate` | wasm | 1.0.6 | 英文翻译：原码在前，前缀补全与拼写建议；中文链内追加英文候选 |
| `translate` | `preprocess`, `parse`, `translate`, `rank` | 任意 | `pime-hans-hant` | wasm | 1.0.0 | 简→繁转换：词组优先最长匹配，重写候选文本 |
| `translate` | `preprocess`, `parse`, `pime-pinyin-parse` | 任意 | `pime-pinyin-translate` | wasm | 1.4.0 | 全拼词典翻译：音节串查词典，产出候选与分数 |
| `translate` | `preprocess`, `parse`, `translate`, `rank`, `pime-shape-parse` | 任意 | `pime-shape-translate` | wasm | 1.0.0 | 拆字查字与字形筛选：u 模式部件码、反引号同组字 |
| `translate` | `preprocess`, `parse`, `translate`, `rank` | 任意 | `pime-shortcuts` | wasm | 1.0.1 | 快捷短语与 {date} {time} {weekday} 模板展开 |
| `translate` | `preprocess`, `parse`, `translate`, `rank` | 任意 | `pime-special-candidates` | wasm | 1.0.0 | 特殊候选：v 数字/算式、日期时间星期、正文算式直接出结果 |
| `translate` | `preprocess`, `parse`, `pime-t9-parse` | 任意 | `pime-t9-translate` | wasm | 1.0.0 | 九键/14 键翻译：键码串按音节编码匹配词表，产出候选 |
| `translate` | `preprocess`, `parse`, `pime-wubi-parse` | 任意 | `pime-wubi-translate` | wasm | 1.1.0 | 五笔词典翻译：字母码查词表，产出候选与分数 |
