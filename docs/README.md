# FFI 项目分析文档索引

> 工作区：`rust-ffi-project-analyze/`  
> 总计划：[plan.md](../plan.md) · 分析范围：[project-scope.md](../project-scope.md)

## 阶段 A/B 单项目分析

| 序号 | 源码路径 | 分析文档 | 状态 |
|------|----------|----------|------|
| P1 | `libz-sys/` | [libz-sys/FFI-ANALYSIS.md](./libz-sys/FFI-ANALYSIS.md) | 完成 |
| P2 | `curl-rust/` | [curl-rust/FFI-ANALYSIS.md](./curl-rust/FFI-ANALYSIS.md) | 完成 |
| P3 | `rust-openssl/` | [rust-openssl/FFI-ANALYSIS.md](./rust-openssl/FFI-ANALYSIS.md) | 完成 |
| P4 | `zstd-rs/` | [zstd-rs/FFI-ANALYSIS.md](./zstd-rs/FFI-ANALYSIS.md) | 完成 |
| P5 | `rdma/category-1/rust-ibverbs/` | [rdma/category-1/rust-ibverbs/FFI-ANALYSIS.md](./rdma/category-1/rust-ibverbs/FFI-ANALYSIS.md) | 完成 |
| P6 | `rdma/category-2/rdma/` | [rdma/category-2/rdma/FFI-ANALYSIS.md](./rdma/category-2/rdma/FFI-ANALYSIS.md) | 完成 |
| P7 | `rdma/category-3/rust-rdma-io/` | [rdma/category-3/rust-rdma-io/FFI-ANALYSIS.md](./rdma/category-3/rust-rdma-io/FFI-ANALYSIS.md) | 完成 |
| P8 | `rdma/category-4/rdma-sys/` | [rdma/category-4/rdma-sys/FFI-ANALYSIS.md](./rdma/category-4/rdma-sys/FFI-ANALYSIS.md) | 完成 |
| P9 | `rdma/category-4/async-rdma/` | [rdma/category-4/async-rdma/FFI-ANALYSIS.md](./rdma/category-4/async-rdma/FFI-ANALYSIS.md) | 完成 |
| P10 | `rdma/category-5/rdma-mummy-sys/` | [rdma/category-5/rdma-mummy-sys/FFI-ANALYSIS.md](./rdma/category-5/rdma-mummy-sys/FFI-ANALYSIS.md) | 完成 |
| P11 | `rdma/category-5/sideway/` | [rdma/category-5/sideway/FFI-ANALYSIS.md](./rdma/category-5/sideway/FFI-ANALYSIS.md) | 完成 |

## 阶段 C / D

| 项 | 路径 | 状态 |
|----|------|------|
| 横向对照范围 | [comparison/SCOPE.md](./comparison/SCOPE.md) | 已裁定 |
| 全样本对照（P1–P11，权威总表） | [comparison/general-c-ffi.md](./comparison/general-c-ffi.md) | 完成 |
| RDMA 补充 + 五 Category 对照 | [rdma/rdma-overview.md](./rdma/rdma-overview.md) | 完成 |
| 最佳实践指导 | [guide/README.md](./guide/README.md) | 完成 |

### 阶段 D 阅读顺序

1. [guide/architecture-decisions.md](./guide/architecture-decisions.md) — 问卷 + 决策树（含手写/bindgen）+ 虚构库演练  
2. [guide/rust-ffi-best-practices.md](./guide/rust-ffi-best-practices.md) — 原则与反模式  
3. [guide/new-project-checklist.md](./guide/new-project-checklist.md) — 勾选清单  

### 阶段 C 阅读顺序

1. [comparison/SCOPE.md](./comparison/SCOPE.md) — 范围与维度  
2. [comparison/general-c-ffi.md](./comparison/general-c-ffi.md) — 11 项目总表、模式、例外/专题  
3. [rdma/rdma-overview.md](./rdma/rdma-overview.md) — P5–P11 补充与 Category 对照  
4. 按需单项目 `FFI-ANALYSIS.md`

### 阶段 D 入口

- [guide/README.md](./guide/README.md) — 指导总览（阶段 D 已完成）
