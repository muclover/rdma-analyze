# RDMA 样本项目分析索引

> 总计划：[plan.md](../../plan.md) · 范围：[project-scope.md](../../project-scope.md)  
> 本目录索引各项目 `FFI-ANALYSIS.md` 与 RDMA 横向总览；单篇分析内不做跨项目对比。

## 横向对照（P5–P11）

| 文档 | 说明 |
|------|------|
| [rdma-overview.md](./rdma-overview.md) | P5–P11 补充对照；**五 Category 对照**（§0.7） |
| [comparison/general-c-ffi.md](../comparison/general-c-ffi.md) | **权威** P1–P11 总表与架构决策向模式归纳 |

阶段 C 范围见 [comparison/SCOPE.md](../comparison/SCOPE.md)（已裁定）。

## Category 1 — ibverbs 入门

| 序号 | 源码 | 分析文档 |
|------|------|----------|
| P5 | `rdma/category-1/rust-ibverbs/` | [rust-ibverbs/FFI-ANALYSIS.md](./category-1/rust-ibverbs/FFI-ANALYSIS.md) |

## Category 2 — 单仓低层 API + examples

| 序号 | 源码 | 分析文档 |
|------|------|----------|
| P6 | `rdma/category-2/rdma/` | [rdma/FFI-ANALYSIS.md](./category-2/rdma/FFI-ANALYSIS.md) |

## Category 3 — 大型 workspace + 上层传输适配

| 序号 | 源码 | 分析文档 |
|------|------|----------|
| P7 | `rdma/category-3/rust-rdma-io/` | [rust-rdma-io/FFI-ANALYSIS.md](./category-3/rust-rdma-io/FFI-ANALYSIS.md) |

## Category 4 — `-sys` 与 async 框架

| 序号 | 源码 | 分析文档 |
|------|------|----------|
| P8 | `rdma/category-4/rdma-sys/` | [rdma-sys/FFI-ANALYSIS.md](./category-4/rdma-sys/FFI-ANALYSIS.md) |
| P9 | `rdma/category-4/async-rdma/` | [async-rdma/FFI-ANALYSIS.md](./category-4/async-rdma/FFI-ANALYSIS.md) |

## Category 5 — mummy 绑定与另一 safe 路线

| 序号 | 源码 | 分析文档 |
|------|------|----------|
| P10 | `rdma/category-5/rdma-mummy-sys/` | [rdma-mummy-sys/FFI-ANALYSIS.md](./category-5/rdma-mummy-sys/FFI-ANALYSIS.md) |
| P11 | `rdma/category-5/sideway/` | [sideway/FFI-ANALYSIS.md](./category-5/sideway/FFI-ANALYSIS.md) |
