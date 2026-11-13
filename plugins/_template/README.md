# pime-example

模板插件（目录以 `_` 开头，不参与发现、lint 与构建）。复制为
`plugins/<role>/<真实 id>/` 后：

1. 改 `manifest.yaml` 的 `id` / `role` / `capabilities` / `limits`；
2. 实现 `src/plugin.cpp` 的 `pime_process`；
3. 在 `tests/cases/` 写至少一个用例；
4. 运行 `python tools/check_plugins` 与 `python tests/run_cases.py`。
