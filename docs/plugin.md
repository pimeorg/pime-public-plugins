# pime 插件

## 1 模型

插件是数据处理单元：`input -> output`。默认插件不接触平台，不链接 libcurl/OpenSSL，只通过 core 的 host API 使用能力；native 逃生舱例外（见 4.1）。

两种运行时：**WASM** 与 **Lua**。共用同一清单、能力、数据 schema、错误码。
模型推理通过 core 的 `ml.infer` 能力，插件只带模型，不打包 Runtime。

## 2 生命周期

```
init → process* → on_event* → shutdown
```

热插拔：

```
discover → validate → load → init → build generation
        → activate → retire → unload
```

卸载条件：无活跃代/保留代、无活跃实例、无在途 async、无租约句柄、无插件线程、无 channel / shared_buffer 引用；否则标 `retired` 延迟卸载。
升级/降级 = 构建并激活新代；旧代保留到显式 `retire`，失败时可重新激活旧代回滚。
`state` / `store` 带 plugin version 与 schema；迁移由插件在 `init` 完成，core 原样回传、不清空；降级需显式 pin 且插件可读旧 schema。
代切换时 core 取消该代的 timer / net / ml 作业，关闭并 join 该代插件线程，迟到回调按 generation 丢弃。
`DATA` 上下文按 TTL 跨卸载保留；`INSTANCE` 随代销毁；`RESOURCE` 交还 owner。

## 3 数据契约

一次调用：

```
input  = { event, state_in, workspace, context, config, assets }
output = { state_out, workspace_delta[], handled, commands[] }
```

- `event`：key / text / touch / candidate / surrounding / context / focus / secure / reset / timer / net.* / net.state / audio.* / audio.focus / audio.route / ml.* / permission。
- `reset`：宿主取消当前组合（焦点丢失、粘贴前、显式复位）。持有组合缓冲的插件
  必须清空自己的会话状态，并发 `composition("")` + `hide`；学习、模式等跨组合
  状态不清。
- `state`：插件私有字节，头部含 plugin version 与 `state_schema`；core 按会话保存并原样回传；默认上限 64KB，清单可调至 1MB。
- `workspace`：同一次链调用内共享的有界键值区，默认 256KB；所有插件可读，写入自动加自身前缀 `plugin/<id>/`；`set` 先记入本次调用的 `workspace_delta`，插件成功即合并（下游可见），失败丢弃。
- `workspace.set(k)` 写入 `plugin/<id>/k`；读自身可用短键，读其他插件用完整键 `plugin/<id>/k`。
- 异步 `on_event` 的 workspace 为空；请求上下文放 state，或用事件 id 关联。
- workspace 只覆盖同步链；异步插件在 `on_event` 内自行完成后续处理。
- `context`：app id（需授权）、字段类型、输入类型、locale、候选页大小、宿主能力位。
- `config`、`assets`：只读，来自不可变快照。
- `handled`：任一插件置 true 或产生命令，core 认为按键已消费；全 false 才放行。
- `commands`：
  - `composition(text, cursor, segments[])`：预编辑文本、光标位置、分段高亮。
  - `commit(text)`：提交。
  - `candidates(list)`：完整有序候选，最多 `candidates_max` 条，宿主自行分页；候选可带 `comment`、`label`。
  - `hide`、`notify(code)`、`beep`、`clear_ink`。

异步事件忽略 `handled`，`commands` 照常应用。`process` 与 `on_event` 返回同一 output；异步结果带 `generation`，代不匹配丢弃。UTF-8 贯穿全部数据，结构带 `size/version`，禁止宿主指针。

### 3.1 缓冲与所有权

- `buffer` 是 core 分配的池化句柄，插件不拥有内存。
- 输入 buffer 在 `process` 返回后失效；需要保留就 `retain`，用完 `release`。
- 输出 buffer 由 core 提供；插件不得保存裸指针。
- WASM：core 写入插件导出的持久 scratch 区；scratch 按**最大可能帧长**声明（manifest `scratch_bytes`），core 只写 capacity，不按实际长度重分配。
- Lua：`pime.buffer` userdata 由 core 池化分配，同样按最大帧长取槽。
- 句柄是索引 + 代际；重复释放、跨代使用、长度越界一律拒绝。
- PCM / Opus / 网络接口优先接收 buffer 句柄，避免复制。
- 资产大文件用 `asset.open/read` 分段读取；native/Lua 可 `map` 零拷贝，WASM 读入 scratch。
- workspace 值也用 buffer 句柄；跨插件键只读，写入自动加自身前缀。
- 跨代句柄立即失效；插件不得跨 generation 缓存 buffer。

## 4 能力

| 能力 | 用途 | 规则 |
|---|---|---|
| `context.read` | 前台上下文 | app id 需授权；字段类型、locale、页大小 |
| `surrounding.read` | 光标前后文本 | 宿主能力可选；按敏感数据处理 |
| `config.read` | 读配置 | 类型化 get/list、快照 id；只读 |
| `asset.read` | 读资产 | list/open/size/read/map；路径白名单 |
| `store.kv` | 插件私有 KV | get/set/del/batch/scan/quota/flush；命名空间隔离 |
| `workspace` | 链内共享 | 读任意、写自身前缀；调用结束失效 |
| `thread` | 插件自有线程 | create/close/join/post/park/unpark/info；保留到 close 或卸载；无数量上限 |
| `emit` | 线程安全输出 | 从任意插件线程向 owner `pump` 投递 output |
| `ctx` | 命名上下文缓存 | DATA/INSTANCE/RESOURCE；acquire/release/pin/unpin/drop/info；引用计数 + TTL |
| `channel` | 线程消息 | 多生产/多消费；`0` 无界；`DRAIN`/`ABORT`；INLINE/BUFFER/SHM |
| `sync` | 互斥与条件变量 | 默认非递归；`RECURSIVE` 显式；持有者死亡 `POISONED` |
| `shm` | 共享内存 | core 持有、引用计数；native `map`；WASM/Lua `read`/`write` |
| `time` | 时间 | 单调/墙钟/时区 |
| `timer` | 定时器 | after/every/cancel；异步事件 |
| `caps.query` | 能力查询 | 版本与可选能力 |
| `random` | 随机源 | OpenSSL CSPRNG |
| `log` / `metric` | 结构化日志与指标 | 分级、限速、命名空间隔离；release 不记录输入内容 |
| `net.http` | HTTP/HTTPS | 请求/响应与流式响应，异步，主机白名单，TLS 强校验 |
| `net.sse` | SSE | `text/event-stream` 长连接，断线重连，空闲超时 |
| `net.ws` / `net.wss` | WebSocket | 全双工消息/二进制帧，心跳、重连、背压 |
| `net.insecure` | 明文 HTTP/WS | 单独授权 |
| `crypto.*` | OpenSSL 全量 | digest / MAC / cipher / AEAD / PKEY / KDF / X.509 / CSR / 随机；算法名直通 OpenSSL |
| `audio.pcm` | PCM | 重采样、声道、位深、混音、增益 |
| `audio.opus` | Opus | 编码/解码、FEC/PLC、分帧 |
| `audio.capture` / `playback` | 麦克风/扬声器 | 只能经 HAL 流；宿主申请权限 |
| `ml.infer` | 模型推理 | load/run/run_async/cancel/free；不打包 Runtime；模型为签名资产；声明 engine/version/格式 |

插件不得直接打开 socket、加载动态库、读取密钥文件。

### 4.1 模型与推理

三层：core 提供 `ml.infer` ABI；backend 提供引擎 Runtime；插件提供模型与清单。

- 插件清单声明 `requires: [ml.infer@1]`；模型段声明 `engine / format / hash / input / output`，`input/output` 为数组，支持多张量。
- WASM/Lua 插件**不打包 Runtime**；Runtime 由 App/backend 提供。
- 复用键 `(engine, version, build_hash, arch, abi)`：同键共享一个 backend；不同键最多并存 2 个版本，超出拒绝。
- 模型按内容哈希共享只读权重；每个插件独立 session。
- 共享推理线程池；按插件做时间、内存、并发配额。
- 手写/语音等大模型用 `run_async`；结果经 `ml.result` / `ml.error` 事件回到 `on_event`，在那里产出候选或提交。
- 张量统一为 `{dtype, shape[4], data}`，输入输出均为数组；WASM 用线性内存，Lua 用 `pime.buffer` 句柄；不暴露引擎原生类型。
- backend 缺失或版本不匹配 → 只禁用该功能，不崩溃。
- 在线手写/语音直接走 `net.wss`，不需要 `ml.infer`。
- Native 插件自带 Runtime 只作为可信插件的逃生舱：必须签名、显式标记、计入版本上限，不作为生态默认。

输入法依赖：

| 输入方式 | 额外库 | 实际需要 |
|---|---|---|
| 双拼 | 否 | 映射表 + 解析器 |
| 五笔 | 否 | 码表 + 词典索引 |
| 手写（离线） | 是 | `ml.infer` + 模型 |
| 手写（在线） | 否 | `net.wss` + 笔迹采集 |
| 语音（离线） | 是 | `ml.infer` + 模型 |
| 语音（在线） | 否 | `net.wss` + PCM/Opus |

### 4.2 线程、上下文与同步

- `thread.create(desc)` 返回线程句柄；`desc` 为 CBOR map，含入口导出名、栈、优先级、名称。`post` / `recv` 消息为 CBOR map 或 buffer 句柄，大小受 `max_frame_bytes` 限制；`park` / `unpark` 等待与唤醒；`close` 请求退出，`join` 等待结束，`info` 返回状态、优先级与队列深度。
- 线程归插件所有，core 不按空闲回收；卸载时代退休先 `close` 再 `join`。WASM 线程是新实例并重新执行 `pime_init`；Lua 线程是新 `lua_State`，入口由 `desc.entry` 指定。沙箱线程超时未退出由 core 终止实例；native 插件线程必须自行退出，否则插件被禁用并延迟卸载。
- 不提供 detach。优先级仅 `background` / `normal` / `interactive`；`critical` / `audio` 不可申请。栈、亲和性等不支持时返回 `UNSUPPORTED`，不改写参数。
- 任意插件线程用 `emit(session_id, output)` 投递命令；owner 线程 `pump` 按会话串行应用；队列满返回 `QUEUE_FULL`。
- `ctx.acquire(name, kind, schema, flags)`：`kind = data | instance | resource`；`flags` 含 `create`、`required`、`pin`、`share`。命中引用计数 +1；未指定 `create` 视为 `required`，未命中返回 `NOT_FOUND`；schema / code hash 不符返回 `STALE`。
- `ctx.release` 到 0 后按 TTL 保留；`pin` 永不淘汰；`drop` 有租约返回 `BUSY`。`ctx.info` 返回类型、大小、引用、pin、TTL。
- `DATA` 跨卸载保留；`INSTANCE` 是预热实例或池，`ctx.call` 执行入口，无空闲实例时新建；`RESOURCE` 共享 core 句柄，生命周期由 owner 决定。
- `channel.create(capacity)`：`0` 无界，`>0` 有界；消息为 `INLINE`（小消息）、`BUFFER`（池化句柄）或 `SHM`（共享内存引用）；`send` / `recv` 带超时；满返回 `FULL` / `TIMEOUT`；`close` 默认 `DRAIN`，`ABORT` 显式。
- `mutex` / `condvar`：`lock` / `trylock` / `unlock`，`wait` / `timedwait` / `signal` / `broadcast`；默认非递归，`RECURSIVE` 显式；持有线程死亡后操作返回 `POISONED`。
- `shared_buffer`：`create(size)` / `open(handle)`；native `map` / `unmap` 零拷贝；WASM/Lua `read` / `write`；有 map 时 `resize` 返回 `BUSY`。
- 全部句柄为索引 + 代际；跨代 `STALE`，关闭后 `CLOSED`；不设数量上限，资源耗尽返回 `NO_MEMORY` / `RESOURCE`。
- 错误码与 `docs/core.md` 同名；只有 `OK` 表示请求完整生效，任何降级都不得返回 `OK`。

## 5 WASM

- WAMR AOT；线性内存；无 WASI 文件/网络。
- 导出：

```c
pime_status pime_init(const uint8_t* config, uint32_t len);
pime_status pime_process(const uint8_t* in, uint32_t in_len,
                         uint8_t* out, uint32_t cap, uint32_t* out_len);
pime_status pime_on_event(const uint8_t* evt, uint32_t len,
                          uint8_t* out, uint32_t cap, uint32_t* out_len);
void        pime_shutdown(void);
```

### 5.1 编组

- WASM 边界统一 **CBOR（RFC 8949）**，definite-length，UTF-8 字符串；buffer 句柄是 uint64 `(generation << 32) | index`，`0` 无效。
- Lua 表与 CBOR map 一一对应；同一份逻辑 schema 在两种运行时完全一致。
- 文本索引统一为 Unicode scalar value（码点），不是 UTF-8 字节，也不是 grapheme。
- 输入由 core 写入插件 scratch；输出由 core 提供。`out` 不足返回 `PIME_BUFFER_TOO_SMALL`，`out_len` 写所需字节数，core 用最大帧长重试一次。
- 插件不得保存输入/输出指针；返回后失效。

```text
input  = { "event": map, "state": bytes, "workspace": map,
           "context": map, "config": map, "assets": map }
output = { "state": bytes, "workspace": [ { "k": str, "v": any } ],
           "handled": bool, "commands": [ map ], "generation": uint }
```

- `event` 必有 `type`，其余字段按第 3 节；命令必有 `type`：`composition / commit / candidates / hide / notify / beep / clear_ink`。
- `pime_process` 的 `in` 是完整 input 帧；`pime_on_event` 的 `evt` 也是 input 帧（workspace 可空），两者返回同一 output 帧。
- `pime_init` 的 config 是 `{ "settings": map, "assets": map, "abi": { "major": uint, "minor": uint } }`。
- 大块数据（PCM、Opus、网络帧、张量）用 buffer 句柄；workspace 值是标量、字符串或 buffer 句柄。
- 插件必须导出：

```c
uint32_t pime_scratch_ptr(void);   /* 线性内存偏移 */
uint32_t pime_scratch_cap(void);   /* 必须 >= manifest.scratch_bytes */
```

- core 只写 `cap` 范围，不按实际长度重分配；插件返回前必须消费完。

- 能力通过 host 函数：

```c
uint32_t pime_host_net_request(const uint8_t* req, uint32_t len); /* 返回请求 id */
uint32_t pime_host_net_result(uint32_t id, uint8_t* out, uint32_t cap);
uint32_t pime_host_net_open(const uint8_t* proto, const uint8_t* url, uint32_t url_len);
pime_status pime_host_net_send(uint32_t id, const uint8_t* data, uint32_t len);
pime_status pime_host_net_close(uint32_t id);
pime_status pime_host_crypto(const uint8_t* req, uint8_t* out, uint32_t cap,
                             uint32_t* out_len);
pime_status pime_host_audio(const uint8_t* req, uint8_t* out, uint32_t cap,
                            uint32_t* out_len);
uint32_t    pime_host_ml_load(const uint8_t* model_id, uint32_t len); /* 返回 session */
pime_status pime_host_ml_load_ex(const uint8_t* desc, uint32_t len,
                                 uint32_t* session);   /* CBOR：模型 + session 选项 */
pime_status pime_host_ml_run(uint32_t id, const uint8_t* in, uint32_t in_len,
                             uint8_t* out, uint32_t cap, uint32_t* out_len);
uint32_t    pime_host_ml_run_async(uint32_t id, const uint8_t* in, uint32_t in_len);
pime_status pime_host_ml_cancel(uint32_t job);
pime_status pime_host_ml_free(uint32_t id);
pime_status pime_host_log(uint32_t level, const uint8_t* tag, uint32_t tag_len,
                          const uint8_t* msg, uint32_t msg_len);

/* op 为单字节操作码，in/out 按数据 schema 编组 */
pime_status pime_host_workspace(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                                uint8_t* out, uint32_t cap, uint32_t* out_len);
pime_status pime_host_store(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                            uint8_t* out, uint32_t cap, uint32_t* out_len);
pime_status pime_host_asset(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                            uint8_t* out, uint32_t cap, uint32_t* out_len);
pime_status pime_host_config(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                             uint8_t* out, uint32_t cap, uint32_t* out_len);
uint64_t    pime_host_time(uint32_t kind);   /* 0=monotonic, 1=wall, 2=tz offset */
pime_status pime_host_random(uint8_t* out, uint32_t len);
uint32_t    pime_host_timer_start(uint32_t delay_ms, uint32_t repeat,
                                  const uint8_t* id, uint32_t len);
pime_status pime_host_timer_cancel(uint32_t id);
pime_status pime_host_caps(uint8_t* out, uint32_t cap, uint32_t* out_len);

/* 线程 */
pime_status pime_host_thread_create(const uint8_t* desc, uint32_t len,
                                    uint32_t* tid);
pime_status pime_host_thread_post(uint32_t tid, const uint8_t* msg, uint32_t len);
pime_status pime_host_thread_recv(uint32_t tid, uint8_t* out, uint32_t cap,
                                  uint32_t* out_len, uint32_t timeout_ms);
pime_status pime_host_thread_park(uint32_t tid, uint32_t timeout_ms);
pime_status pime_host_thread_unpark(uint32_t tid);
pime_status pime_host_thread_close(uint32_t tid);
pime_status pime_host_thread_join(uint32_t tid, uint32_t timeout_ms);
pime_status pime_host_thread_info(uint32_t tid, uint8_t* out, uint32_t cap,
                                  uint32_t* out_len);   /* CBOR */
pime_status pime_host_emit(uint64_t session_id, const uint8_t* out, uint32_t len);

/* 上下文 / 通道 / 同步 / 共享内存：op 为单字节操作码 */
pime_status pime_host_ctx(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                          uint8_t* out, uint32_t cap, uint32_t* out_len);
pime_status pime_host_channel(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                              uint8_t* out, uint32_t cap, uint32_t* out_len);
pime_status pime_host_sync(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                           uint8_t* out, uint32_t cap, uint32_t* out_len);
pime_status pime_host_shm(const uint8_t* op, const uint8_t* in, uint32_t in_len,
                          uint8_t* out, uint32_t cap, uint32_t* out_len);
```

### 5.2 能力请求与响应

所有 `op` / opaque `req` 都是 CBOR map；响应为 `{ "status": pime_status, "result": map, "error": { "code": int, "msg": str } }`。`out` 不足返回 `PIME_BUFFER_TOO_SMALL`，`out_len` 写所需字节数。

- `net.*`：请求 `{method, url, headers, body, timeout_ms}`；响应 `{status, headers, body}`；流事件 `{type:"net.data", id, data}`、`{type:"net.state", state}`。代理/CA/客户端证书只来自 core 配置，插件不能覆盖。
- `crypto.*`：`key_import` / `key_generate` 返回 uint64 句柄；`key_destroy` 销毁；私钥不可导出。AEAD 参数 `{alg, key, iv, aad, plaintext}`，输出 `{ciphertext, tag}`。
- `audio.*`：帧 `{rate, channels, format, data}`；`capture` / `playback` 返回流 id；第二路采集返回 `BUSY`；事件 `audio.focus {focus}`、`audio.route {route}`。
- `ml.*`：张量 `{dtype, shape:[...], layout:"nchw"|"nhwc"|"none", data}`；session 选项 `{threads, quant, input_names, output_names}`，`ml_load_ex` 接收 `{model_id, options}`，`ml_load` 等价于空 options；结果同张量数组。
- `pime_host_caps` 返回宿主能力位（见 core 4.1）；`caps.query` 返回已授予能力与 core ABI，两者不混用。
- `permission` 事件：`{capability, granted}`；被拒绝的能力调用返回 `PIME_DENIED`。
- 所有 id 由 core 分配，插件不得伪造；跨代 id 返回 `STALE`。

- 同步事件（`key` / `text` / `touch` / `candidate` / `surrounding` / `context` / `focus` / `secure` / `reset`）走 `process`；异步事件（`timer` / `net.*` / `net.state` / `audio.*` / `ml.*` / `permission`）走 `on_event`；两者都返回 output。
- `timer` 带注册 id，`ml.result` 带 job id 与张量，`net.*` 带连接 id；插件用 id 关联自己的状态。
- 同步链只接受 AOT；解释执行只允许 async。

## 6 Lua

- Lua 5.4；自定义分配器；指令钩子；内存上限。
- 移除 `io / os / package / debug`；只保留安全子集。
- 接口：

```lua
function pime.init(config) end
function pime.process(input) return output end
function pime.on_event(event) return output end
function pime.shutdown() end
```

- 网络：

```lua
-- 请求/响应
local id = pime.net.request({ method = "POST", url = "https://...",
  headers = {...}, body = "...", timeout_ms = 5000 })

-- 长连接：ws / wss / sse
local ws = pime.net.open("wss", "wss://...", { heartbeat_ms = 15000 })
pime.net.send(ws, data)
pime.net.close(ws)

-- 事件：net.open / net.data / net.close / net.error
```

- 数据与运行时：

```lua
-- 工作区（链内共享）
pime.workspace.set("syllables", data)   -- 写入 plugin/<self>/syllables
local v = pime.workspace.get("plugin/pinyin-parse/syllables")

-- 存储（插件命名空间）
pime.store.set(key, value)
local v = pime.store.get(key)
pime.store.delete(key)
pime.store.batch({ {op="set", key=k, value=v}, ... })   -- 原子
for k, v in pime.store.scan("prefix", 100) do ... end
local used, limit = pime.store.quota()
pime.store.flush()

-- 资产（只读，大文件分段）
local a = pime.asset.open("dict.bin")
local n = pime.asset.size(a)
local chunk = pime.asset.read(a, offset, len)
local view = pime.asset.map(a, offset, len)   -- native/Lua 零拷贝
pime.asset.close(a)

-- 配置
local v = pime.config.get("candidate.page_size")
local list = pime.config.list("candidate")
local id = pime.config.snapshot()

-- 时间 / 随机 / 定时器
pime.time.monotonic_ms()
pime.time.wall_ms()
pime.time.timezone_offset()
pime.random.bytes(16)
local t = pime.timer.after(200, "long-press")
pime.timer.every(60000, "flush")
pime.timer.cancel(t)

-- 能力
pime.caps.has("ml.infer")
pime.caps.list()
```

- 线程 / 上下文 / 同步：

```lua
-- 每个线程是独立 lua_State；entry(tid) 在线程内执行
local t = pime.thread.create({ name = "asr", entry = "worker",
  priority = "interactive", stack_kb = 256 })
pime.thread.post(t, msg)
local msg = pime.thread.recv(t, 1000)
pime.thread.park(t, 100)
pime.thread.unpark(t)
pime.thread.close(t)
pime.thread.join(t, 5000)
pime.emit(session_id, output)              -- 任意线程输出到 owner pump

-- 命名上下文：data / instance / resource
local ctx = pime.ctx.acquire("asr.decoder", "instance", 1,
  { create = true, pin = true })
local out = pime.ctx.call(ctx, "decode", input)
pime.ctx.release(ctx)
pime.ctx.pin(ctx, true)
local info = pime.ctx.info("asr.decoder", "instance")
pime.ctx.drop("asr.decoder", "instance")

-- 通道
local ch = pime.channel.create(0)          -- 0 = 无界
pime.channel.send(ch, msg, 1000)
local msg, err = pime.channel.recv(ch, 1000)
pime.channel.close(ch, "drain")            -- 或 "abort"

-- 互斥 / 条件变量
local m = pime.sync.mutex({ recursive = false })
m:lock(1000); m:unlock()
local cv = pime.sync.condvar()
cv:wait(m, 1000); cv:signal(); cv:broadcast()

-- 共享内存
local shm = pime.shm.create(65536)
shm:write(0, data)
local d = shm:read(0, 1024)
shm:resize(131072)                         -- 有 map 时返回 BUSY
shm:close()
```

- 音频：

```lua
local mic = pime.audio.capture({ rate = 16000, channels = 1, format = "s16le" })
pime.audio.play({ rate = 24000, channels = 1, format = "s16le" })
local pcm = pime.audio.pcm.convert(frame,
  { rate = 16000, channels = 1 }, { rate = 48000, channels = 2 })
local packet = pime.audio.opus.encode(pcm, { rate = 16000, bitrate = 24000 })
local pcm2 = pime.audio.opus.decode(packet, { rate = 16000 })
```

- 模型推理：

```lua
local s = pime.ml.load("handwriting", { threads = 4 })   -- 对应清单 models[].id
local y = pime.ml.run(s, { dtype = "f32", shape = {1, 64, 64}, data = buf })  -- 多输入传数组
local job = pime.ml.run_async(s, tensor)   -- 结果事件 ml.result / ml.error
pime.ml.cancel(job)
pime.ml.free(s)
```

- 日志：

```lua
pime.log("info", "pinyin", "loaded 120k entries")
pime.log.info("pinyin", "loaded 120k entries")
pime.log.warn("net", "reconnecting")
pime.log.error("model", "backend missing")
```

- 加解密（全量 OpenSSL）：

```lua
pime.crypto.digest("sha256", data)
pime.crypto.mac("hmac-sha256", key, data)
pime.crypto.cipher("aes-256-gcm", key, iv, aad, plaintext)
pime.crypto.sign(key, "ecdsa-p256-sha256", data)
pime.crypto.verify(pubkey, "ecdsa-p256-sha256", data, sig)
pime.crypto.kdf("hkdf", key, salt, info, 32)
pime.crypto.x509_verify(cert, ca)
pime.crypto.random(32)
local k = pime.crypto.key_import("pem", pem)   -- 返回句柄，私钥不可导出
pime.crypto.key_destroy(k)
```

- 表结构与第 3 节字段一一对应，由 core 编组。
- 适合控制、配置、低频变换；进入同步链需实测预算通过。
- 字节码可选并签名；源码加载做语法白名单检查。

## 7 清单

```yaml
id: pime-pinyin
version: 1.4.0
abi: { major: 1, minor: 0 }
min_core: "1.0.0"
state_schema: 3
store_schema: 2
runtime: wasm
role: parse
after: [preprocess]   # 允许排在本插件之前的 role；[] 表示必须最先
platforms: [windows-x64, macos-arm64, ios-arm64, ios-x64, android-arm64, android-x64, harmony-arm64, harmony-x64]
entry: pime_process
capabilities: [input.read, output.emit, context.read, config.read, asset.read,
               store.kv, workspace, time, timer, caps.query, log, metric]
requires: []   # 例：pime-base@^1.2, ml.infer@1
assets: [pinyin.bin, lm.bin]
limits:
  memory_mb: 32
  instructions: 10000000
  time_ms: 5
  state_kb: 64
  workspace_kb: 256
  store_mb: 16
  candidates_max: 200
  scratch_bytes: 16384
  max_frame_bytes: 16384
  net_concurrency: 2
  net_streams: 2
  net_bytes: 1048576
  net_bandwidth_kbps: 256
  audio_frames: 64
  log_bytes_per_sec: 65536
  log_record_bytes: 1024
budget: { sync_us: 50, async_ms: 20 }
signature:
  alg: ed25519
  key_id: pime-official-1
```

校验顺序：清单 → ABI major → 平台 → 依赖 → 签名 → 可加载。

### 7.1 包格式与签名

```text
pime-plugin-<id>-<version>/
  manifest.yaml
  plugin.wasm | plugin.lua
  assets/
  models/
  SIGNATURE
```

- 签名：Ed25519 detached，`SIGNATURE` 覆盖“规范化 manifest（不含 `signature` 段）+ 按路径排序的 `path\0size\0sha256` 列表（不含 `SIGNATURE` 自身）”；`manifest.signature` 只放算法与 key id。
- 信任根：`pime_core_config.trust_store` 目录提供 `roots.pem` 与 `revoked.txt`；`key_id` 必须命中；吊销列表命中拒绝加载。
- 包内不允许未签名文件；路径不得越出包根；符号链接拒绝。
- 安装：临时目录校验 → 原子 rename；保留上一版本；失败回滚。iOS/HarmonyOS 只允许随包签名，不下载可执行代码。
- `runtime` 只能是 `wasm` 或 `lua`；`role` 取第 8 节值；`min_core` 高于当前 core 拒绝加载。

### 7.2 版本机制

- SemVer 2.0 `MAJOR.MINOR.PATCH`；预发布仅显式 pin，build metadata 不参与解析。
- MAJOR：不兼容 state/store/config/能力；MINOR：兼容新增；PATCH：修复。
- `(id, version)` 唯一；同一平台/架构的同 id+version 必须同 `code_hash`（安装时 core 计算），否则拒绝安装。
- 多版本可并存；一个代内默认每个 id 只激活一个版本；需要并行时用 `alias`，alias 在代内唯一。
- 依赖写版本约束：`pime-base@^1.2`、`ml.infer@1`、精确版本、`>=`、`<`、`*`；依赖可为能力或插件；循环或缺失返回 `NOT_FOUND`；解析取满足条件的最高版本，不降级。
- 代记录精确 `(id, version, code_hash, alias)`；代不可变。升级/回滚都通过 `activate` 切换代；旧代保留到显式 `retire`。
- 卸载被任一代/会话/上下文租约引用时返回 `BUSY`；删除后不能参与新代。
- `min_core` 与 core SemVer 比较；`abi` 单独判定。
- `state_schema` / `store_schema` 随 `state` / `store` 保存；迁移在 `init` 完成，降级需显式 pin 且可读旧 schema。
- 上下文键使用 `alias` + `code_hash`；日志 `source` 用 `id@version`。alias 默认 id；code hash 变则 `DATA` / `INSTANCE` 不复用。
- 上下文与 `store` 命名空间使用 alias；升级（同 alias）共享数据并迁移；并行版本用不同 alias 隔离。

线程、上下文、通道、同步、共享内存按需在 `capabilities` 声明：`thread`、`emit`、`ctx`、`channel`、`sync`、`shm`；跨插件共享上下文另加 `ctx.share`。core 不设数量配额，资源不足在调用时返回明确错误。

设置声明（宿主渲染，core 只校验类型与默认值）：

```yaml
settings:
  - { key: fuzzy, type: bool, default: true }
  - { key: page_size, type: int, default: 5, min: 1, max: 9 }
  - { key: theme, type: enum, values: [light, dark], default: light }
```

手写 / 离线语音的模型段：

```yaml
requires: [ml.infer@1]
models:
  - id: handwriting
    path: model.ncnn
    format: ncnn
    engine: ncnn@2024.08
    hash: sha256:...
    input:  [{ dtype: f32, shape: [1, 64, 64] }]
    output: [{ dtype: f32, shape: [1, 3755] }]
```

## 8 角色与组装

Core 只认“有序插件链”。插件声明 `role`，生态约定：

`preprocess`、`parse`、`translate`、`rank`、`format`、`recognize`、`learn`、`predict`、`service`。

- 顺序由配置声明，不写死在 core。
- 每个 manifest 用 `after` 声明**允许排在其前方的 role**。链里实际排在它前面的插件，
  其 role 必须都在 `after` 中；`after: []` 表示必须最先。这样用户往现成方案包里
  加插件时，看清单就知道能插在哪些插件之后、哪些之前。
- 同一 role 可挂多个插件；同名覆盖必须显式声明。要排在同 role 的其他插件之后，
  `after` 里也要写这个 role。
- 拼音 = parse + translate + rank + format；parse 把音节写入 workspace，translate 读取，只在本次链调用有效。
- 标点、键位、双拼、五笔、混输都是同一条链上的不同组合。
- 切换输入法 = 换插件链配置，构建新代。

## 9 安全

- 能力最小化；未声明即拒绝。
- 授权：宿主通过 `pime_core_permission` 授予；未授予返回 `PIME_DENIED`；`context.read(app)`、`net.insecure`、`ctx.share`、`store.kv` 可单独拒绝。
- 包与信任：Ed25519 签名、信任根、吊销、原子安装；未签名或未信任拒绝加载。
- 版本完整性：同一平台/架构的同 `id+version` 必须同 `code_hash`；解析失败返回 `NOT_FOUND`；降级需显式 pin，不静默回退。
- 存储：`store.kv` 使用 core 的 `store_key` 做 AES-256-GCM；无密钥返回 `PIME_DENIED`，不回退明文。键空间按插件隔离并带 schema 版本；迁移由插件在 `init` 完成，core 不改写。
- native / 进程外：native 仅可信签名；下载 backend 进程外；IPC 只传 id/offset/len；崩溃重启不影响进程。
- 内存安全：不得保存裸指针；buffer 句柄校验代际与引用计数；越界由 WASM/Lua 运行时拦截；host API 入参做边界与整数溢出检查。
- 日志：不记录按键、文本、候选、密钥；单条截断、限速；控制字符过滤；安全模式不记录事件内容。
- 网络：TLS 验证强制；主机白名单；重定向/端口/方法受控；代理/CA/客户端证书只来自 core 配置，插件不能覆盖；响应大小与并发配额。
- 流式网络：连接数、消息大小、带宽、重连频率配额；SSE/WS 空闲超时。
- 音频：帧大小与缓冲上限；不得直连麦克风/扬声器，只能经 HAL。
- 上下文：app id 需授权；surrounding 文本按敏感数据处理，不落日志、不持久化。
- 工作区：只能写自身前缀；大小受限；调用结束清空，不得当持久化通道。
- 线程：插件线程只执行沙箱代码；不提供 detach；卸载/代退休必须 `close` 并 `join`；不能申请 `critical` / `audio` 优先级；创建失败明确报错，不静默降级。
- 上下文缓存：键含 `alias`，命名空间隔离；`ctx.share` 单独授权；`pin` 阻止淘汰，内存耗尽由插件负责清理；schema / code hash 变化返回 `STALE`，不复用旧数据。
- 同步：channel / mutex / shared_buffer 句柄校验代际；mutex 中毒不自动恢复；关闭后拒绝新操作；shared_buffer 释放按敏感级别清零；无隐式同步。
- 推理：模型与 backend 签名校验；模型只读；backend 无文件/网络权限；张量大小与推理时间配额。
- 密钥：插件只拿句柄；签名/解密在 core 内完成；不提供私钥导出。
- 安全模式：不投递按键文本、surrounding、app_id；禁用 `net.*`、`store.kv` 写入、`ctx` DATA 写入与日志内容；本地组合/候选/提交仍可用；拒绝返回 `PIME_DENIED`，不得缓存历史输入。
- 签名校验使用 core 的 OpenSSL；不通过拒绝加载。
- 失败（trap / 超时 / 配额 / 越界）→ 禁用实例 → 连续 N 次隔离整个插件。

## 10 性能

- WASM AOT 单次 ≤2µs；Lua 单次 ≤10µs。
- 同步链总预算 ≤1ms；网络与重加解密只能 async。
- 小哈希可同步，但计入预算。
- WS/SSE/音频按消息或帧异步处理；不阻塞同步链。
- 推理走 core 的共享线程池；每插件并发/时间配额；不进同步链。
- 插件线程创建后保留，`park` / `unpark` 用等待原语，不轮询；线程不与同步链争用 owner。
- 上下文命中只做查找与引用计数；`INSTANCE` 在 `acquire` 时完成预热；首次构建可放 async。
- channel 传 `BUFFER` / `SHM` 句柄，避免大块拷贝；无界 channel 的增长受进程内存约束。
- `shared_buffer` native 零拷贝；WASM/Lua 只有显式 `read` / `write` 才发生拷贝。
- `on_event`、定时器、异步推理不占同步链；同一 id 定时器只保留一个。
- 日志关闭时一次分支；开启时无 malloc，异步投递；不在同步链做 I/O。
- 避免每次调用分配；输出缓冲复用。
- 声明预算，CI 与线上指标校验；长尾插件由指标发现后显式改 async 或拆分，不在运行时静默降级。
- CBOR 编解码在 scratch/池化缓冲上完成，不按帧 malloc；`pump` 间隔 ≤10ms，`emit` 队列有界。

## 11 统一

- 同一份插件在所有平台行为一致。
- 同一份 `id+version` 在同一平台/架构必须对应同一 `code_hash`；跨平台允许平台包不同，但 manifest 必须显式声明。
- 同一份 schema、能力名、错误码、生命周期。
- 平台差异只在 HAL；插件不写 `#ifdef`。
- 相同输入 + 相同资产 → 相同输出。

## 12 示例

**拼音（WASM）**：`parse` 按键流 → 音节写入 workspace；`translate` 读取并查词典与 LM；`rank` 打分；`format` 生成组合文本与候选。

**标点（Lua）**：`preprocess` 把 ASCII 标点映射为中文标点，输出 `commit` 或 `composition`。

**云同步（Lua）**：`service` 角色；用 `net.http`/`net.sse` 拉取用户词，用 `crypto.sign` 校验签名。

**语音输入（WASM）**：`thread.create` 起采集/编码线程，`audio.capture` → `audio.pcm.convert` → `audio.opus.encode` → `net.wss`；ASR 文本经 `net.data` 事件或 channel 回传，`emit` 到 owner `pump`，在 `on_event` 中 `commit`。解码器用 `ctx.acquire("asr.decoder", "instance")` 保留热实例。

**上下文缓存（Lua）**：`ctx.acquire("pinyin.index", "data", schema=3, { pin = true })` 保留词典索引；插件暂时卸载时 `DATA` 不销毁，下次 `acquire` 命中，schema 不符返回 `STALE` 并重建。

**版本升级/回滚**：安装 `pime-pinyin@1.4.0` 与 `@1.5.0`；代 A 固定 1.4.0，代 B 固定 1.5.0；`activate(B)` 后旧代保留，失败时重新 `activate(A)`；两代都 `retire` 前不卸载任一版本。

**手写（WASM）**：`touch` 笔迹 → `ml.run_async` 识别 → `ml.result` 事件产候选；离线用本地模型，在线改走 `net.wss`。
