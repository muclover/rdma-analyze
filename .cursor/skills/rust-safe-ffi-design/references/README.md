# rust-safe-ffi-design — 内部参考索引

> **仅供 Agent 执行技能时阅读**；内容用于决策，**不得**原样复制「证据」「样本 Px」进用户交付物。  

## 阅读顺序（相对本 `references/` 目录）

| 顺序 | 路径 | 用途 |
|------|------|------|
| 1 | [guide/architecture-decisions.md](./guide/architecture-decisions.md) | 问卷 Q1–Q9、总览树、分主题树（**§3.2 手写/bindgen**、RDMA §2.1） |
| 2 | [guide/rust-ffi-best-practices.md](./guide/rust-ffi-best-practices.md) | 原则、§4 绑定、反模式 |
| 3 | [guide/new-project-checklist.md](./guide/new-project-checklist.md) | 勾选清单 → 产出 `CHECKLIST.md` |
| 4 | [comparison/general-c-ffi.md](./comparison/general-c-ffi.md) | 模式 M0–M10、例外 S1–S8、专题 T1–T6 |
| 5 | [comparison/SCOPE.md](./comparison/SCOPE.md) | 维度 D01–D17（内部对照，不写进交付物） |
| 6 | [rdma/rdma-overview.md](./rdma/rdma-overview.md) §0.7 | 仅当 C 库为 verbs/rdma-core 画像 |

## 单项目深读（仅当 C 库与某样本极像时）

| 样本 | 路径 |
|------|------|
| 纯 `-sys`、复杂 `build.rs` | [libz-sys/FFI-ANALYSIS.md](./libz-sys/FFI-ANALYSIS.md) |
| 双 crate、手写、回调 panic | [curl-rust/FFI-ANALYSIS.md](./curl-rust/FFI-ANALYSIS.md) |
| workspace、手写+bindgen、ERR 栈 | [rust-openssl/FFI-ANALYSIS.md](./rust-openssl/FFI-ANALYSIS.md) |
| 三层、预生成 bindgen | [zstd-rs/FFI-ANALYSIS.md](./zstd-rs/FFI-ANALYSIS.md) |
| RDMA 各路线 | [rdma/category-*/**/FFI-ANALYSIS.md](./rdma/) |

范围边界：[project-scope.md](./project-scope.md)。

## 目录布局

```text
references/
├── README.md                 # 本索引
├── SOURCE.md                 # 副本来源与同步说明
├── project-scope.md
├── guide/
├── comparison/
├── rdma/                     # rdma-overview + category-*/FFI-ANALYSIS
├── libz-sys/
├── curl-rust/
├── rust-openssl/
└── zstd-rs/
```

## 决策 → 交付物映射

| 决策主题 | 主要写入 |
|----------|----------|
| crate 分层、workspace | `ARCHITECTURE.md` |
| 手写/bindgen/wrapper/mummy、`build.rs`、链接 | `BINDING.md` |
| 类型、RAII、错误、`# Safety` | `SAFE-API.md` |
| 实施勾选 | `CHECKLIST.md` |
| async / 上层协议 | `ASYNC-ANALYZE.md` |
| 总览 | `README.md`（用户交付物，非本文件） |

## 绑定路径速查（内部）

| 条件 | 优先考虑 |
|------|----------|
| 无 inline，中等 API，要 docs.rs 友好 | 预生成 bindgen + systest |
| 无 inline，API 少且稳定 | 手写 + systest |
| 常追新头文件 | 构建时 bindgen（feature） |
| inline/宏多，非 RDMA | wrapper.c 或手写复刻 |
| ibverbs/rdma-core + CI 无 dev 包 | mummy 或 wrapper（见 rdma-overview §0.7） |
| 只做框架、绑定外包 | 依赖 crates.io `-sys`（查许可证） |

## Async 速查（内部）

| 画像 | 交付物 `ASYNC-ANALYZE.md` |
|------|---------------------------|
| 纯计算/同步 IO | **不需要**；说明用阻塞或线程池 |
| 已有 fd/多路 API（如 multi） | 暴露 C 多路 + 文档；或 safe 薄封 |
| 需 tokio 一等 | 库内 `AsyncFd`/任务 或 独立 async crate |
| gRPC/QUIC 等 | safe 之上独立 crate，**零新增** `extern "C"` |
