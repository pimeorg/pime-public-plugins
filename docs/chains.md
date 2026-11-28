# 产品链

> 插件是独立单元；产品是 `chains/` 里的组合配方。本文描述配方格式、
> workspace 契约与锁定发布。插件规格见 `docs/plugin.md`。

## 1 为什么需要链

core 只认“有序插件链”：

```text
pime_generation_build(core, cbor{plugins:[...]}) -> generation
pime_generation_activate(core, generation)
```

插件按 `id` 独立安装、独立版本、独立签名；把哪些插件、按什么顺序、用什么 settings
组合起来，是产品层的事。链就是这个产品层，不打包进任何插件。

## 2 目录

```text
chains/
├── pinyin/
│   ├── default.yaml          # 全拼默认
│   ├── default.lock.yaml     # 精确版本 + code_hash
│   ├── shuangpin.yaml
│   └── shuangpin.lock.yaml
├── wubi/
│   ├── 86.yaml
│   └── 86.lock.yaml
├── handwriting/
│   └── offline.yaml
└── speech/
    └── online.yaml
```

一级目录是产品族，二级是变体；链文件不放在 `plugins/` 下，避免和插件边界混淆。

## 3 配方格式

```yaml
api: 1
id: pinyin-default
description: 全拼默认链
core: ">=1.0.0"

plugins:
  - { id: pime-keymap-default,    role: preprocess }
  - { id: pime-pinyin-parse,      role: parse,
      settings: { fuzzy: true } }
  - { id: pime-pinyin-translate,  role: translate, version: "^1.2" }
  - { id: pime-pinyin-rank,       role: rank }
  - { id: pime-candidates-format, role: format }
  - { id: pime-learn-frequency,   role: learn }

workspace:
  - { key: plugin/pime-pinyin-parse/syllables,
      from: pime-pinyin-parse, to: [pime-pinyin-translate] }
  - { key: plugin/pime-pinyin-translate/candidates,
      from: pime-pinyin-translate,
      to: [pime-pinyin-rank, pime-candidates-format] }
```

字段：

| 字段 | 必填 | 说明 |
|---|---|---|
| `api` | 是 | 链文件格式版本；当前 `1` |
| `id` | 是 | 链唯一名；只用于仓库/发布，不发给 core |
| `description` | 否 | 说明 |
| `core` | 否 | 要求的 core SemVer；与插件 `min_core` 一起校验 |
| `plugins[]` | 是 | 插件记录；数组顺序即执行顺序，条目可为墓碑 |
| `plugins[].id` | 是 | 插件 id |
| `plugins[].role` | 是 | 必须与插件 manifest 一致 |
| `plugins[].after` | 否 | 直接前驱插件 id；由包管理器插入时写入，链头省略 |
| `plugins[].disabled` | 否 | `true` 表示墓碑：不参与执行，但保留顺序和锚点 |
| `plugins[].version` | 否 | SemVer 约束；省略取满足条件的最高版本 |
| `plugins[].alias` | 否 | 同 id 多版本并行时必须显式且唯一 |
| `plugins[].settings` | 否 | 只能包含 manifest `settings` 声明过的键 |
| `workspace[]` | 否 | 仓库级数据契约；不发给 core |
| `workspace[].key` | 是 | 完整键 `plugin/<producer-id>/<key>` |
| `workspace[].from` | 是 | 生产者插件 id |
| `workspace[].to` | 是 | 消费者插件 id 列表 |

## 4 编译成 core 配置

`tools/chainc` 读取 YAML，只输出 core 认识的字段：

```cbor
{
  "plugins": [
    { "id": "pime-keymap-default",    "role": "preprocess" },
    { "id": "pime-pinyin-parse",      "role": "parse",
      "settings": { "fuzzy": true } },
    { "id": "pime-pinyin-translate",  "role": "translate",
      "version": "^1.2" },
    { "id": "pime-pinyin-rank",       "role": "rank" },
    { "id": "pime-candidates-format", "role": "format" },
    { "id": "pime-learn-frequency",   "role": "learn" }
  ]
}
```

- 数组顺序 = core 执行顺序；`chainc` 跳过 `disabled: true` 的条目。
- `after` / `disabled`、`workspace`、`description`、`core` 等仓库字段不进入 CBOR。
- `chainc --lock` 生成 `*.lock.yaml`；`chainc --check` 校验锁与当前包一致。
- 输出是确定性的（键排序、CBOR canonical），便于 diff 与复现。

## 5 顺序约定

core 不强制 role 顺序；生态约定：

```text
preprocess → parse → translate → rank → format → learn / predict → service
```

- 链记录的 `plugins[]` 数组就是执行顺序，`chainc` 原样编译。
- 每个条目用 `after: <plugin-id>` 记录它的**直接前驱**。加插件只写新条目：
  包管理器把它插到前驱之后；前驱不存在或没有声明时，按 role 落到该 role 的
  最后一条之后，再不行就放到链尾。
- 插件 manifest 的 `after` 只是“导入时的默认前驱”，可省略；`after: null`
  表示链头。方案链条目里的 `after` 优先于 manifest。
- 删除插件时在链里保留条目并标 `disabled: true`（墓碑）：执行时跳过，但数组
  顺序和其它条目的锚点不变；以后往它后面加插件仍可复用这个槽位。
- 同步事件链只放同步插件；`recognize` / `service` 的异步结果经 `on_event` 回来，
  不阻塞同步链。
- 同一 role 可挂多个插件；同名/同功能覆盖必须显式写在链里。

```yaml
# manifest.yaml：默认前驱，只影响“导入到别的方案”时的落点
role: rank
after: pime-pinyin-translate
```

```yaml
# schemes/win/pinyin/chain.yaml：数组顺序是记录，after 是一致性锚点
plugins:
  - { id: pime-pinyin-translate, after: pime-shape-parse, role: translate }
  - { id: pime-pinyin-rank,      after: pime-pinyin-translate, role: rank }
  - { id: pime-special-candidates, after: pime-pinyin-rank, role: translate }
```

## 6 workspace 契约

workspace 是链内共享的有界键值区：

- 写入自动加 `plugin/<self-id>/` 前缀；读自己用短键，读别人用完整键。
- 只在同一次同步链调用内有效；调用结束清空。
- 异步 `on_event` 的 workspace 为空；跨调用状态放 `state`，或用事件 id 关联。
- 失败插件的 `workspace_delta` 丢弃；下游不能假设失败插件写过。

链文件必须声明跨插件键：

```yaml
workspace:
  - { key: plugin/pime-pinyin-parse/syllables,
      from: pime-pinyin-parse, to: [pime-pinyin-translate] }
```

`check_plugins` 校验：

1. `key` 前缀与 `from` 一致。
2. `from` / `to` 都在 `plugins[]` 中。
3. 生产者的 README 声明写该键；消费者的 README 声明读该键。
4. 消费者在生产者之后（按数组顺序）。

## 7 锁定与发布

```yaml
# chains/pinyin/default.lock.yaml
api: 1
chain: pinyin-default
core: 1.0.0
plugins:
  - { id: pime-keymap-default,    version: 1.0.0, code_hash: sha256:... }
  - { id: pime-pinyin-parse,      version: 1.2.0, code_hash: sha256:... }
  - { id: pime-pinyin-translate,  version: 1.3.1, code_hash: sha256:... }
  - { id: pime-pinyin-rank,       version: 1.1.0, code_hash: sha256:... }
  - { id: pime-candidates-format, version: 1.0.0, code_hash: sha256:... }
  - { id: pime-learn-frequency,   version: 1.0.2, code_hash: sha256:... }
```

- 锁固定 `(id, version, code_hash)` 与 core 版本；发布、回滚、CI 都以锁为准。
- 升级：改配方 → 重新生成锁 → CI 构建新代 → 激活；旧代保留到显式 `retire`。
- 回滚：重新激活旧锁对应的代；不要删除旧版本包。
- 并行版本：同 id 不同版本用不同 `alias`，在锁中体现。
- `code_hash` 由 core 安装时计算；锁中的值必须与构建产物一致。

## 8 示例

### 8.1 拼音

```yaml
id: pinyin-default
plugins:
  - { id: pime-keymap-default,    role: preprocess }
  - { id: pime-pinyin-parse,      role: parse }
  - { id: pime-pinyin-translate,  role: translate }
  - { id: pime-pinyin-rank,       role: rank }
  - { id: pime-candidates-format, role: format }
```

### 8.2 双拼

```yaml
id: pinyin-shuangpin
plugins:
  - { id: pime-keymap-default,      role: preprocess }
  - { id: pime-shuangpin-standard,  role: preprocess }
  - { id: pime-pinyin-parse,        role: parse }
  - { id: pime-pinyin-translate,    role: translate }
  - { id: pime-pinyin-rank,         role: rank }
  - { id: pime-candidates-format,   role: format }
```

双拼只替换 preprocess 段；parse 之后完全复用全拼链。

### 8.3 标点

```yaml
id: punctuation-zh
plugins:
  - { id: pime-punctuation-zh, role: preprocess }
  - { id: pime-candidates-format, role: format }
```

### 8.4 离线手写

```yaml
id: handwriting-offline
plugins:
  - { id: pime-handwriting-ncnn, role: recognize }
  - { id: pime-candidates-format, role: format }
```

### 8.5 在线语音

```yaml
id: speech-online
plugins:
  - { id: pime-speech-ncnn,   role: recognize }
  - { id: pime-cloud-sync,    role: service }
  - { id: pime-candidates-format, role: format }
```

## 9 失败与回滚

- 任一插件缺失/版本不满足/role 不匹配 → 代构建失败，返回明确错误；不静默跳过。
- 新代激活失败 → 重新激活旧代；旧包不卸载。
- 异步结果带 generation；旧代结果丢弃。
- 回滚不要求数据回退；`state_schema` / `store_schema` 不兼容时按插件声明的
  降级/pin 规则处理。

## 10 检查清单

- [ ] 链 `id` 唯一；文件名与 `id` 一致。
- [ ] 每个插件的 `id` / `role` 存在于 `plugins/`，且 role 与 manifest 一致。
- [ ] 每个条目的 `after` 指向数组中更早的插件（或墓碑）；异步插件不阻塞同步链。
- [ ] `settings` 键都在插件 manifest 中声明。
- [ ] 跨插件 workspace 键都有 `from` / `to` 声明，且消费者在生产者之后。
- [ ] 锁文件存在，`code_hash` 与构建产物一致。
- [ ] 链端到端测试通过；预算未超。
