# pime-handwriting-ncnn

`recognize`（WASM）：离线手写识别。笔迹点由宿主采集，模型经 `ml.infer` 运行；
插件本身不带 Runtime，只带模型与标签表。

## 事件

| 事件 | 行为 |
|---|---|
| `touch {x, y}` | 追加一个点（0..255），最多 128 点 |
| `touch.end` | 编码张量 → `ml.run` → 标签映射 → 候选；清空笔迹 |

## Workspace

| 键 | 类型 | 说明 |
|---|---|---|
| `plugin/pime-handwriting-ncnn/candidates` | array<map> | `{text, score, source:"ml"}` |

## 模型

- `models: handwriting`，engine `ncnn`，输入 `u8 [1,128,2,1]`，输出 `u32` 标签 id。
- 权重不入库，由 `models.lock` 分发；`hash` 当前为 `sha256:unfetched`，发布前必须替换。
- backend 缺失 → `pime_process` 返回 `PIME_UNSUPPORTED`，不静默降级。

## 资产

`assets/labels.txt`：`<id>\t<char>`，UTF-8。

## 预算

`sync_us: 200`；张量编码为 256 字节，向量拷贝即可。

## 用例

`tests/cases/`：单点累积、`touch.end` 识别（模拟器返回固定标签）。
