# 仓颉码表

`cangjie.dict` 是 starter 规模（24 个基本字母 + 常用整码），格式为
`<code>	<word>	<freq>`，与 `data/wubi/wubi.dict` 一致。

- `pime-cangjie-parse` 负责累积码串，默认上限 5 码。
- `pime-cangjie-translate` 负责整码与补全，补全注释显示剩余编码。

生产码表按同样格式提供即可；更大的码表应走 `data/locks/` 分发。
