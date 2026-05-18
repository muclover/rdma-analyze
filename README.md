# rdma-analyze（v2）

Rust **C / `extern "C"`** FFI 绑定案例研究与架构指导工作区。本分支（**`v2`**）在 v1（仅 `rdma/docs/ffi-schemes/` 方案对比）基础上，扩展为覆盖 **P1–P11** 共 11 个样本项目的完整分析仓库，并包含阶段 C/D 横向对照与新项目指导文档。

**远程仓库**：<https://github.com/muclover/rdma-analyze>  
**推荐分支**：`v2`（本工作区默认发布分支）  
**历史分支**：`main` 保留 v1 文档结构，便于对照迁移。

---

## 仓库做什么

| 目标 | 说明 |
|------|------|
| **案例证据** | 对 11 个真实 Rust FFI 项目做静态画像（`docs/**/FFI-ANALYSIS.md`） |
| **横向归纳** | 对比模式、例外与 RDMA 五 Category 演进（`docs/comparison/`、`docs/rdma/`） |
| **新项目指导** | 问卷、决策树、检查清单（`docs/guide/`） |
| **可复用技能** | Agent 用 `rust-safe-ffi-design` 产出架构交付物（`.cursor/skills/`） |

最终用途：在绑定**新的 C 库**时，能依据本仓库证据完成 **crate 分层、bindgen/手写、链接、错误与 async** 等架构决策。

---

## 快速导航

| 入口 | 路径 |
|------|------|
| 总计划与阶段说明 | [plan.md](./plan.md) |
| 分析范围约定 | [project-scope.md](./project-scope.md) |
| **文档总索引** | [docs/README.md](./docs/README.md) |
| 11 项目横向对照 | [docs/comparison/general-c-ffi.md](./docs/comparison/general-c-ffi.md) |
| 架构决策树 | [docs/guide/architecture-decisions.md](./docs/guide/architecture-decisions.md) |
| RDMA 专题 | [docs/rdma/rdma-overview.md](./docs/rdma/rdma-overview.md) |
| 设计实验与技能说明 | [design/README.md](./design/README.md) |

---

## 目录结构（v2）

```text
.
├── README.md                 # 本文件
├── plan.md                   # 阶段 A/B/C/D 计划
├── project-scope.md          # 单项目分析范围
├── docs/                     # 全部分析产出（只读样本的「结论」）
│   ├── comparison/           # 阶段 C
│   ├── guide/                # 阶段 D
│   ├── rdma/                 # RDMA 补充与 category 分析
│   └── {libz-sys,curl-rust,...}/FFI-ANALYSIS.md
├── libz-sys/ … zstd-rs/      # P1–P4 样本源码树（对照阅读）
├── rdma/category-*/          # P5–P11 RDMA 样本
├── obmm/                     # 额外 C/Rust 对照材料（可选阅读）
├── design/                   # 技能与交付物设计实验
└── .cursor/skills/           # rust-safe-ffi-design 等
```

**约定**：分析文档只写在 `docs/`；不修改各样本上游逻辑，样本树仅供静态阅读与引用。

---

## 样本项目一览（P1–P11）

| P | 路径 | 类型 |
|---|------|------|
| P1 | `libz-sys/` | 经典 `-sys` |
| P2 | `curl-rust/` | `-sys` + safe |
| P3 | `rust-openssl/` | workspace + 混合绑定 |
| P4 | `zstd-rs/` | 三层 `sys → safe → API` |
| P5–P11 | `rdma/category-*/` | RDMA / libibverbs 多条技术路线 |

单篇分析见 `docs/<对应路径>/FFI-ANALYSIS.md`。

---

## 从 v1（`main`）迁移到 v2

| v1 (`main`) | v2 (`v2`) |
|-------------|-----------|
| 以 `rdma/docs/ffi-schemes/` 为主 | 根目录 `docs/` 统一索引 |
| 样本需本地自行 `git clone` 到 `rdma/` | 样本树纳入本仓库（扁平快照，无嵌套 `.git`） |
| 侧重 RDMA 方案对比 | 覆盖通用 C FFI + RDMA + 新项目指导 |

若只需旧版 RDMA 方案对比入口，请 checkout `main` 并阅读其 README。

---

## 使用方式

1. **读结论**：从 [docs/README.md](./docs/README.md) 按阶段 C/D 阅读顺序开始。  
2. **查案例**：打开对应 `FFI-ANALYSIS.md`，必要时对照同路径下的样本源码。  
3. **做新绑定**：按 [docs/guide/architecture-decisions.md](./docs/guide/architecture-decisions.md) 填问卷并走决策树；或用 Cursor 技能 `rust-safe-ffi-design` 生成 `rust-ffi-design/` 交付物。

本仓库分析以**静态阅读**为主，不要求为阅读文档而 `cargo build` 全部样本。

---

## 许可与样本版权

- 本仓库**自著文档**（`docs/`、`plan.md`、`design/` 等）供研究与引用。  
- 各样本目录（`libz-sys/`、`curl-rust/` 等）遵循其**各自上游许可证**；商用或再分发请遵守对应项目许可。

---

## 维护

- 问题与改进：<https://github.com/muclover/rdma-analyze/issues>  
- 默认开发分支：`v2`
