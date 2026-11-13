# pime-cloud-sync

`service`（Lua）：把 `pime-learn-frequency` 的 `freq/*` 词频表增量上传，并合并
服务端结果。默认关闭，读取配置后才发请求。

## 事件

| 事件 | 行为 |
|---|---|
| `sync.start` | 汇总 KV（最多 1000 条）为 `word\tcount` 文本，POST 到 `endpoint` |
| `net.result` | 取回响应正文，按行合并（只增不减），写 `merged` 计数 |

响应与请求同格式：`text/plain`，每行 `<word>\t<count>`。

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-cloud-sync/status` | text | `empty` / `pending` / `rejected` / `synced` |
| `plugin/pime-cloud-sync/request` | uint | 在途请求 id |
| `plugin/pime-cloud-sync/merged` | uint | 本次合并条数 |

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | false | 关闭时 `sync.start` 不产生请求 |
| `endpoint` | string | `https://sync.pime.dev/v1/freq` | 同步端点 |
| `timeout_ms` | int | 5000 | 请求超时 |

## 权限

`net.http` 需宿主开关；请求被拒绝时 `status=rejected`，不静默成功。

## Store

读写 `freq/<word>` → uint，与 `pime-learn-frequency` 共用命名空间约定。

## 用例

`tests/cases/`：默认关闭不发请求、收到结果后合并状态。
