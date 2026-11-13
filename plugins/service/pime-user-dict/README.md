# pime-user-dict

`service`（Lua）：用户词库，落在插件私有 KV；随提交自动积累，也可由宿主显式
增删。

## 事件

| 事件 | 行为 |
|---|---|
| `user_dict.add {word}` | 词频 +1；非法词（空/超 64 字节）返回 `handled=false` |
| `user_dict.remove {word}` | 删除词条 |
| `user_dict.list` | 把前 `max_words` 条写入 workspace `words` |
| 其他 | 读取 `plugin/pime-candidates-format/committed` 自动入库 |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-user-dict/status` | text | `added` / `removed` / `listed` / `invalid` |
| `plugin/pime-user-dict/word` | text | 最近操作的词 |
| `plugin/pime-user-dict/count` | uint | 该词次数 |
| `plugin/pime-user-dict/words` | array<map> | `list` 输出：`{word, count}` |
| `plugin/pime-user-dict/total` | uint | `list` 返回条数 |

## Store

`words/<word>` → uint。与 `pime-learn-frequency` 的 `freq/<word>` 分开命名，
避免两个插件互相覆盖。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不处理 |
| `max_words` | int | 32 | `list` 返回上限（1..128） |

## 用例

`tests/cases/`：新增、删除、列举。
