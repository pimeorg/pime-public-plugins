# 插件索引

> 由 `python tools/check_plugins --write-index` 生成，不要手改。

| role | id | runtime | version | 说明 |
|---|---|---|---|---|
| `format` | `pime-candidates-format` | wasm | 1.1.0 | 候选与组合收尾：展示候选、空格/数字选词、Esc 取消 |
| `learn` | `pime-learn-frequency` | lua | 1.0.0 | 学习：按提交记录词频，写入插件私有 KV |
| `parse` | `pime-cangjie-parse` | wasm | 1.0.0 | 仓颉：累积字母码并写入 workspace |
| `parse` | `pime-english-parse` | wasm | 1.0.0 | 英文：累积字母与撇号，保留原码大小写 |
| `parse` | `pime-pinyin-parse` | wasm | 1.1.0 | 全拼解析：音节切分、自动纠错、触点邻键纠正与预编辑 |
| `parse` | `pime-t9-parse` | wasm | 1.0.0 | 九键/14 键：累积键码（1 为音节分隔）并写入 workspace |
| `parse` | `pime-wubi-parse` | wasm | 1.1.0 | 五笔：累积字母码并写入 workspace 供词表翻译 |
| `predict` | `pime-predict-context` | wasm | 1.0.0 | 预测：按光标前文本匹配短语表，产出下一词候选 |
| `preprocess` | `pime-keymap-default` | wasm | 1.2.0 | 默认键位：把控制键映射为链内动作，文本键放行 |
| `preprocess` | `pime-punctuation-zh` | lua | 1.1.0 | 中文标点：半角标点转全角，引号样式可配置 |
| `preprocess` | `pime-shuangpin-standard` | wasm | 1.1.0 | 双拼四方案（小鹤/自然码/微软/搜狗）：两键合成音节 |
| `rank` | `pime-pinyin-rank` | wasm | 1.1.0 | 拼音候选重排：短词加权后按分数降序 |
| `rank` | `pime-rank-default` | wasm | 1.0.0 | 通用重排：按分数降序、过滤低分、截断 |
| `recognize` | `pime-handwriting-ncnn` | wasm | 1.0.0 | 手写识别：笔迹点经 ml.infer 得到候选（离线） |
| `recognize` | `pime-speech-ncnn` | wasm | 1.0.0 | 语音识别：PCM 经 ml.infer 得到候选（离线） |
| `service` | `pime-cloud-sync` | lua | 1.0.0 | 服务：词频表增量同步（HTTP，可关闭） |
| `service` | `pime-update` | lua | 1.0.0 | 服务：检查新版本（HTTP 源，包签名由 core 校验） |
| `service` | `pime-user-dict` | lua | 1.0.0 | 服务：用户词库记录、查询与删除 |
| `translate` | `pime-cangjie-translate` | wasm | 1.0.0 | 仓颉词典翻译：字母码查词表，整码在前、补全在后 |
| `translate` | `pime-english-translate` | wasm | 1.0.0 | 英文翻译：原码在前，前缀补全与拼写建议，保留输入大小写 |
| `translate` | `pime-pinyin-translate` | wasm | 1.1.0 | 全拼词典翻译：音节串查词典，产出候选与分数 |
| `translate` | `pime-special-candidates` | wasm | 1.0.0 | 特殊候选：v 数字/算式、日期时间星期、正文算式直接出结果 |
| `translate` | `pime-t9-translate` | wasm | 1.0.0 | 九键/14 键翻译：键码串按音节编码匹配词表，产出候选 |
| `translate` | `pime-wubi-translate` | wasm | 1.1.0 | 五笔词典翻译：字母码查词表，产出候选与分数 |
