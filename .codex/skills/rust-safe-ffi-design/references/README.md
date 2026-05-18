# rust-safe-ffi-design — Reference Router

本目录是 `rust-safe-ffi-design` 的内部参考包。它用于帮助 agent 做架构判断，不是用户交付物的一部分。

使用原则：

- 先读 guide，再按 C 库画像读取少量 case study。
- 不要默认通读所有 `FFI-ANALYSIS.md`。
- 不要把 `P1`-`P11`、样本编号、reference 路径或 comparison 章节写入用户交付物。
- 若 reference 与目标库冲突，以目标库的头文件、构建脚本、许可证和用户发布目标为准。

## 目录角色

| 目录 / 文件 | 角色 | 读取时机 |
|-------------|------|----------|
| `guide/` | 规范化决策树与实践原则 | 每次执行必读 |
| `comparison/` | 横向模式表，用于校验分层、绑定、linking、async 等选择 | 需要模式核对或决策不确定时读 |
| `libz-sys/`、`curl-rust/`、`rust-openssl/`、`zstd-rs/` | 通用 C FFI case studies | 仅当目标库与样本形态接近时选读 |
| `rdma/` | RDMA / verbs / rdma-core 专用 case studies | 仅当目标库属于 RDMA 类或有类似 inline/ops/硬件 CI 问题时读 |
| `SOURCE.md` | 副本来源与同步说明 | 维护 reference 包时读 |

## 必读路径

按顺序读取：

1. [guide/architecture-decisions.md](./guide/architecture-decisions.md)
2. [guide/rust-ffi-best-practices.md](./guide/rust-ffi-best-practices.md)
3. [guide/new-project-checklist.md](./guide/new-project-checklist.md)

这三份文件提供 Q1-Q10、crate 分层、手写/bindgen/wrapper/mummy、linking、错误/资源、测试与 CI 的默认判断。

## 条件路由

| 目标库画像 | 选读 reference |
|------------|----------------|
| 只需要裸 FFI，`build.rs`/系统库/vendored 切换复杂 | [libz-sys/FFI-ANALYSIS.md](./libz-sys/FFI-ANALYSIS.md) |
| 双 crate、回调、panic 不能跨 FFI、multi/event API | [curl-rust/FFI-ANALYSIS.md](./curl-rust/FFI-ANALYSIS.md) |
| 大 workspace、多后端、错误栈、手写 + bindgen 混合 | [rust-openssl/FFI-ANALYSIS.md](./rust-openssl/FFI-ANALYSIS.md) |
| 三层 `sys -> safe -> ergonomic API`、预生成 bindgen、流式 API | [zstd-rs/FFI-ANALYSIS.md](./zstd-rs/FFI-ANALYSIS.md) |
| verbs / rdma-core / ops 表 / inline 导出 / 无硬件 CI | [rdma/README.md](./rdma/README.md) |
| 无法在单一样本中定位，需要横向模式校验 | [comparison/general-c-ffi.md](./comparison/general-c-ffi.md) |

## 决策到交付物映射

| 决策主题 | 主要写入 |
|----------|----------|
| crate 分层、workspace、发布目标 | `ARCHITECTURE.md` |
| 手写/bindgen/wrapper/mummy、`build.rs`、linking | `BINDING.md` |
| 类型、RAII、错误、`# Safety`、`Send`/`Sync` | `SAFE-API.md` |
| 实施顺序、测试、CI、发布前检查 | `CHECKLIST.md` |
| async / reactor / 上层协议 crate | `ASYNC-ANALYZE.md` |
| 阅读顺序与摘要 | `README.md`（用户交付物，非本文件） |

## 快速规则

| 条件 | 优先考虑 |
|------|----------|
| 无 inline 难题，中大 API，要 docs.rs 友好 | 预生成 bindgen + ABI/systest |
| API 少且稳定 | 手写 extern + ABI/systest |
| 频繁追随系统头文件 | 构建时 bindgen，但用 feature 或文档限制成本 |
| inline/宏/bitfield/union 难题明显 | wrapper.c、blocklist + 手写替代，或专用导出层 |
| verbs/rdma-core 且 CI 无 dev 包或无硬件 | 先读 RDMA 路由，再比较 wrapper、mummy、软 RDMA |
| 只做上层框架 | 优先复用已有 `-sys`，检查许可证与 ABI 版本 |

## Async 快速规则

| 画像 | `ASYNC-ANALYZE.md` 写法 |
|------|-------------------------|
| 纯计算或同步 IO | N/A；说明阻塞 API、线程池或调用方负责 |
| C 库已有 fd/multi/poll API | 暴露多路能力或 safe 薄封，记录线程规则 |
| Rust crate 要成为 async 框架 | safe 之上集成 `AsyncFd`/任务，或拆独立 async crate |
| gRPC/QUIC/协议适配 | 放在 safe 之上，避免新增 `extern "C"` 绑定 |

## 维护建议

- `guide/` 应保持短、规范、可直接执行。
- `comparison/` 可以保留样本编号，但只作为内部横向表。
- case study 文件可以较长；通过本 router 条件读取，避免上下文膨胀。
- 若未来重构物理目录，推荐形态是 `cases/general/*` 与 `cases/rdma/*`，但需要同步更新所有相对链接和 `SKILL.md` 路径。
