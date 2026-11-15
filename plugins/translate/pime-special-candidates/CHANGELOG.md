# Changelog

## 1.0.0

- 首个版本，对齐 WeaveText `special.rs`：
  - `v` 模式：数字（原样、大写金额、中文数字、千分位）与算式（`+ - * / % ^`、
    括号、整数指数），结果为有限值时给出结果、`原式=结果` 与大写金额。
  - 日期键：`rq/riqi/jintian/mingtian/zuotian`、`sj/shijian/xianzai`、
    `xq/xingqi/zhouji`，四种日期格式、四种时间格式与星期。
  - 日期候选插在已有首位候选之后，保留普通候选。
  - 正文算式：`surrounding` 事件里 `表达式=` 直接给出结果。
- 无 libm：`^` 仅接受整数指数，结果用共享定点格式化。
- 与 `pime-pinyin-parse` 配合：`v` 之后保留数字与运算符，并输出单一纯文本段。
