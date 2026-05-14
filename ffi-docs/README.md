# FFI 设计文档（工作区样本）

本目录文档基于仓库内 [rust-ffi-project/](../rust-ffi-project/) 中的快照代码分析得出，用于对比业界常见 Rust FFI 分层与安全策略，并整理成可迁移到「任意 C 库」的方法论与检查表。**RDMA / libibverbs** 的专用方案总览见 [rdma-ffi-schemes.md](rdma-ffi-schemes.md)，与 [rdma/docs/ffi-schemes/](../rdma/docs/ffi-schemes/README.md) 专题互为补充。

**分析基线**：本工作区 vendored 快照（不保证与上游逐提交一致）。若与 crates.io 最新版有差异，请以你实际依赖版本为准。

## 阅读顺序

1. [compare-projects.md](compare-projects.md) — 四个样本 crate 的出发点、分层、构建、错误与特殊点横向对比（文末含 **RDMA 延伸** 索引）。
2. [methodology-any-c-project.md](methodology-any-c-project.md) — **C 库画像**、两层决策（绑定闭合 → 分层）、safe 准入、`Send`/`Sync` 五类语义、测试清单；热路径闭合策略与 RDMA 共用 §2.1。
3. [design-output-template.md](design-output-template.md) — 新项目 FFI **设计文档输出模板**（把分析落成可评审方案）。
4. [async-ecosystem.md](async-ecosystem.md) — crate 内能力、**§1.1 异步适配决策表**、Tokio 生态边界、**RDMA CQ 与 async**（§5）。
5. [rdma-ffi-schemes.md](rdma-ffi-schemes.md) — RDMA 五条 FFI 主线 + 与通用样本的类比；权威长文仍在 `rdma/docs/ffi-schemes/`。

## 术语表

| 术语 | 含义 |
|------|------|
| `-sys` crate | 以 `*-sys` 命名的包：通常 `links = "..."`，暴露 `unsafe` FFI，负责探测/编译/链接 C 库。 |
| `links` | `Cargo.toml` 中的 `links` 字段：同一依赖图里同名 `links` 只能有一份构建脚本「胜出」，用于避免多份静态库冲突。 |
| Opaque handle | 在 Rust 侧用 `enum Opaque {}` 或私有 ZST + `NonNull<T>` 等表示「不完整 C 类型」，避免外部构造。 |
| Panic 边界 | C 调用 Rust 回调时禁止 unwinding 越过 FFI；常用 `catch_unwind` 或等价策略。 |
| Safe 中间层 | 介于 `-sys` 与最终 API 之间：用类型与 `Drop` 封装不变量，仍可能含少量 `unsafe`，但集中、可审计。 |

## 样本项目一览

| 目录 | 角色 |
|------|------|
| `rust-ffi-project/libz-sys` | 典型 `-sys`：链接 zlib、可选自带 C 源码编译。 |
| `rust-ffi-project/zstd-rs` | 三层：`zstd` → `zstd-safe` → `zstd-sys`。 |
| `rust-ffi-project/rust-openssl` | 工作区：`openssl-sys` + `openssl` + 宏/错误辅助 crate。 |
| `rust-ffi-project/curl-rust` | `curl` + `curl-sys`：回调、Multi、全局初始化。 |
| `rdma/docs/ffi-schemes/` | libibverbs 五类 FFI 主线 + 并发/async 专题（权威长文）。 |
| `ffi-docs/rdma-ffi-schemes.md` | 上表在 `ffi-docs` 内的总入口与选型压缩版。 |
| `ffi-docs/design-output-template.md` | 新项目 FFI 设计文档输出骨架（与方法论配套）。 |
