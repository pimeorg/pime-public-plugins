# CHANGELOG

## 1.1.0

- 新增：字槽分组（`touch.group`），多字连写/叠写各自推理并生成拼接候选。
- 新增：包围盒归一化到 0..255。
- 新增：纠正样本学习（`handwriting.correct` → `fix/<wrong>\x1f<right>`）。
- 新增：`groups` workspace 输出；state 增补 `groups`（schema 2）。

## 1.0.0

- 初版：采集笔迹点、调用 ml.infer、id → 字符候选。
