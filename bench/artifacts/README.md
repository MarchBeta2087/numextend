# bench/artifacts

一次性性能实测产物的归档（**非 CI 基线**）。CI 趋势基线在 `bench/baseline/`，
suite 定义与运行方式见 `docs/benchmarking.md`。

| 文件 | 内容 | 关联 |
|---|---|---|
| `2026-10-20-full.zip` | 3 h 全量（nex + contrib，full + frozen）产物：主 HTML、8 个日志 HTML + index、JSON/Markdown、原始日志 | `docs/bench-full-2026-10-20.md`、issue #128 |

归档原则：只在需要长期留档时提交；优先附到 issue/Release。文件应尽量小
（本例 23 KB），不要放原始大体积输出或 CI 基线。
