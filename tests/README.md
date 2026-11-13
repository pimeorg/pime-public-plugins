# tests/

| 路径 | 用途 |
|---|---|
| `host/sim_main.cpp` + `sim_stubs.cpp` | 把 WASM 插件源码按原生编译，链接宿主桩，驱动一次 `pime_process` / `pime_on_event` |
| `host/main.cpp` | 最小 core 宿主（可选）：加载包 → 建代 → 激活 → 会话 → 事件 → 打印 sink 输出 |
| `run_cases.py` | 读 `plugins/*/*/tests/cases/*.yaml`，生成 CBOR 帧跑模拟器并比对 |
| `conformance/run.py` | 汇总：`check_plugins`、所有链 `--check`、已构建包的落盘检查 |

用例格式见 `run_cases.py` 顶部说明；`expect` 采用子集语义（workspace 只校验列出
的键）。

运行：

```sh
cmake --build build/native -j
python tests/run_cases.py --sim-dir build/native/sim
python tests/conformance/run.py
```
