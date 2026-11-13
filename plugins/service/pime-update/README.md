# pime-update

`service`（Lua）：从文本 feed 检查新版本。只做「查」，不做「装」——包的签名与
可信根由 core 的包加载器校验（见 `trust/`）。

## Feed 格式

`text/plain`，每行 `key=value`：

```text
version=1.2.0
url=https://updates.pime.dev/pime-plugin-1.2.0.pimepkg
sha256=<hex>
```

## 事件

| 事件 | 行为 |
|---|---|
| `update.check` | 读 `event.current_version`；缺失则 `status=need-version`，否则 GET `feed_url` |
| `net.result` | 解析 feed，与当前版本比较，写 `latest` / `url` / `status` |

`status`：`need-version` / `pending` / `rejected` / `bad-feed` / `available` /
`up-to-date`。

## Workspace

`plugin/pime-update/{status, request, latest, url}`。

## Settings

| key | 类型 | 默认 | 说明 |
|---|---|---|---|
| `enabled` | bool | true | 关闭后不发起检查 |
| `feed_url` | string | `https://updates.pime.dev/plugin-feed.txt` | 版本源 |
| `timeout_ms` | int | 5000 | 请求超时 |

## 用例

`tests/cases/`：缺当前版本、无在途请求时收到 `net.result`。
