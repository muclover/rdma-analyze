# libobmm Rust 绑定 — 设计草案（未使用 rust-safe-ffi-design 技能）

> **说明**：本目录为对照实验的「无技能」路径产出：仅基于对 `obmm/` 的通读与通用 Rust FFI 经验，**未** 按项目 `docs/guide` 决策树与六件套模板执行。  
> **日期**：2026-05-18

## 1. 概述

OBMM 是华为的远端内存管理用户态库，提供 export/import 等 API。建议用 Rust 绑定 `libobmm.so`，对外提供一个 crate 方便调用。

## 2. 推荐技术栈

- 使用 **bindgen** 从头文件生成绑定。
- 使用 **build.rs** 链接 `-lobmm`。
- 错误可以用 `Result` 包装。

## 3. Crate 结构

可以创建一个 `obmm` crate，里面包含：

- `build.rs`：查找 libobmm
- `src/lib.rs`：bindgen 生成的代码 + 一些 safe 封装

如果 API 多，以后再拆 `-sys`。

## 4. 主要 API（需绑定）

来自 `libobmm.h`：

- `obmm_export` / `obmm_unexport`
- `obmm_import` / `obmm_unimport`
- `obmm_preimport` / `obmm_unpreimport`
- `obmm_export_useraddr`
- `obmm_set_ownership`
- `obmm_query_*`

结构体 `obmm_mem_desc`、`obmm_preimport_info` 用 bindgen 生成即可。

## 5. Safe 层建议

- 为 `mem_id` 建一个 newtype。
- export/import 返回 `Result`。
- 考虑用 `Drop` 自动 unexport。

## 6. 构建与部署

- 用户需安装 obmm（CMake 编译安装）。
- 在 Linux 上使用，需要内核模块支持。
- README 写明依赖。

## 7. 测试

- 写单元测试调用 export（需要环境）。
- 可以用 mock。

## 8. 后续

- 视情况加 async。
- 文档翻译 C 文档。

---

**本草案缺失项（相对技能路径，供对比）**：未单独处理 `ub/obmm.h` 依赖；未决策预生成 vs 构建时 bindgen；未设计 FAM `priv[]`；未写 `links`；未区分 Mulan/GPL；无 ownership/mmap 分工；无 CI 分层；无独立 ASYNC/绑定专篇。
