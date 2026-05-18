# libobmm Rust FFI 架构设计 — 文档索引

> **对象 C 库**：libobmm（OBMM 用户态库，`libobmm.so`）  
> **C 项目路径**：`obmm/`（本仓库内）  
> **设计范围**：`-sys` 绑定 · safe API · `build.rs`/链接 · 测试/CI · async/上层协议  
> **状态**：草案（技能 `rust-safe-ffi-design` 产出）  
> **日期**：2026-05-18

## 一句话摘要

采用 **`obmm-sys` + `obmm` 双 crate**；**预生成 bindgen**（`wrapper.h` 聚合 `libobmm.h` + `ub/obmm.h`）+ **手写 Rust 包装** 处理 flexible array `priv`；**`links = "obmm"`** + pkg-config/环境变量探测；safe 层以 **`MemId` RAII** 与 **`MemDesc` 构建器** 封装 export/import 生命周期；**crate 内不提供 async**；集成测试依赖内核模块与 OBMM 硬件环境。

## 阅读顺序

| 顺序 | 文档 | 内容 |
|------|------|------|
| 1 | [ARCHITECTURE.md](./ARCHITECTURE.md) | 库画像、crate 划分、总决策 |
| 2 | [BINDING.md](./BINDING.md) | 绑定方式、`build.rs`、链接 |
| 3 | [SAFE-API.md](./SAFE-API.md) | safe 层 API 与契约 |
| 4 | [ASYNC-ANALYZE.md](./ASYNC-ANALYZE.md) | async 与上层协议必要性 |
| 5 | [CHECKLIST.md](./CHECKLIST.md) | 实施勾选清单 |

## 决策摘要表

| 维度 | 决定 |
|------|------|
| Crate 分层 | `obmm-sys` + `obmm` |
| 绑定方式 | **预生成 bindgen** + FAM/常量手写补充；**不**整库纯手写 |
| 链接策略 | `links=obmm`；pkg-config `obmm` → `OBMM_DIR` → 可选 vendored 头文件路径 |
| 错误模型 | `-sys` 保留 C 返回；safe 用 `Result<_, ObmmError>`（`mem_id==0` + `errno`） |
| Async | **不需要**（库内） |
| 上层协议 crate | **不需要** |
| 许可证 | 用户态 **Mulan PSL v2**（与内核 GPL-2.0 分离） |
