# pime-handwriting-ncnn

`recognize`（WASM）：离线手写识别，笔迹经 `ml.infer` 得到候选。

## 事件

| 事件 | 行为 |
|---|---|
| `touch {x, y, group?}` | 追加笔迹点；`group` 是字槽编号（默认 0） |
| `touch.end` | 按字槽分组、归一化、逐组推理，输出候选 |
| `handwriting.correct {wrong, right}` | 记录纠正样本 |

## 算法

1. **归一化**：每组点按包围盒缩放到 0..255；退化包围盒保持原坐标。
2. **多字连写/叠写分组**：`group` 不同的点分别推理，每组取 top-1 拼成一个
   「连写」候选排在首位；同时保留单字候选。
3. **纠正样本学习**：`handwriting.correct` 写 `fix/<wrong>\x1f<right>` 计数；
   下次模型返回 `wrong` 时，纠正结果以「纠正」注释排到首位。

## Workspace

| 方向 | 键 |
|---|---|
| 写 | `plugin/pime-handwriting-ncnn/candidates`（`source=ml`，注释可选）、`plugin/pime-handwriting-ncnn/groups`（本次字槽数） |

## 存储

| 键 | 值 |
|---|---|
| `fix/<wrong>\x1f<right>` | 纠正次数 |

## State

`{ pts, groups }`：未完成的笔迹点与字槽，`state_schema: 2`。

## 资产与模型

- `assets/labels.txt`：`<label id>\t<char>`。
- `models/handwriting.ncnn`：按 `models.lock` 拉取，不入库。

## 预算

`sync_us: 200`；最多 128 点、4 个字符槽、8 条候选。

## 用例

`tests/cases/`：采点、单字识别、多槽连写、包围盒归一化、纠正样本、touch 事件分组。
