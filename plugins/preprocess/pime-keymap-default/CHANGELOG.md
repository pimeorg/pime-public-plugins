# Changelog

## 1.2.0

- `digit` 动作同时发布 `digit` 键值（0–9），format 可据此做数字选词。

## 1.0.0

- 首个版本：默认键位到链内动作的映射（backspace/delete/enter/escape/space/tab）。

## 1.1.0

- 新增候选导航动作：方向键、PageUp/PageDown、Home/End。
- 新增 `digit`、`shift`、`capslock`、`backtick` 动作。
- 新增设置 `candidate_keys`、`number_selects_candidate`。
- 修正 README 中 `handled` 的描述：键位映射不吞键，`handled` 恒为 false。
