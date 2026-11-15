# CHANGELOG

## 1.1.0

- 修复：`CMakeLists.txt` 补上 `ASSETS assets/continuation.tsv assets/collocations.tsv`。
  两个文件自本版起声明在 manifest 里，但构建清单漏了，打出的包没有 `assets/`，
  接续表与搭配模型因此从未生效。
- 增强：联想从单一短语表改为四档：用户二元组、接续表、字符搭配模型、末字续字。
- 新增：`assets/continuation.tsv` 接续表与 `assets/collocations.tsv` 搭配表（懒加载）。
- 新增：`show_candidates` 命令与「组合进行中不联想」守卫。
- 新增：`user_bigram` / `continuation` / `collocation` / `fallback` / `show_command` 设置。

## 1.0.0

- 初版：按光标前文本匹配内置短语表，产出下一词候选。
