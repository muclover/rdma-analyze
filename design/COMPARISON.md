# obmm FFI 设计对照实验：使用技能 vs 不使用技能

> **实验对象**：本仓库 `obmm/`（libobmm 用户态库）  
> **技能路径输出**：[`use-skills/rust-ffi-design/`](./use-skills/rust-ffi-design/)（6 文件）  
> **无技能路径输出**：[`without-skills/DESIGN.md`](./without-skills/DESIGN.md)（1 文件）  
> **日期**：2026-05-18

## 1. 实验设置

| 项 | 使用技能 | 不使用技能 |
|----|----------|------------|
| 流程 | `rust-safe-ffi-design`：静态画像 → Q1–Q10 → 读 `docs/guide` 决策 → 六件套模板 | 通读 `obmm` 头文件/README + 通用 FFI 经验 |
| 产出形态 | `README` + 5 份专题设计 | 单页 `DESIGN.md` |
| 证据引用 | 交付物内 **无** P1–P11（技能要求） | 无 |
| 构建验证 | 均未 `cargo build` | 同左 |

## 2. 结构完整性

| 维度 | 使用技能 | 不使用技能 |
|------|----------|------------|
| 文档数量 | 6 | 1 |
| 索引与决策摘要 | ✅ `README.md` 决策表 | ❌ |
| 架构 / crate 划分 | ✅ `ARCHITECTURE.md` workspace 图 | △ 一段「单 crate，以后可拆」 |
| 绑定专篇（手写 vs bindgen） | ✅ `BINDING.md` **显式选定预生成 bindgen** | △ 仅「用 bindgen」 |
| safe API 契约 | ✅ `SAFE-API.md` 模块树 + `# Safety` 表 | △ 列表级建议 |
| async / 上层协议 | ✅ `ASYNC-ANALYZE.md` 结论「不需要」+ 理由 | △ 「视情况加 async」 |
| 实施清单 | ✅ `CHECKLIST.md` ~25 项 | ❌ |

## 3. 技术决策深度（关键差异）

### 3.1 外部依赖 `ub/obmm.h`

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| 识别 | ✅ `wrapper.h` 聚合；build 检测 `OBMM_DIR` | ❌ 未提及 |
| 影响 | 决定 bindgen 输入与 CI 前置条件 | 易导致绑定失败或常量缺失 |

### 3.2 Flexible array member（`priv[]`）

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| 识别 | ✅ blocklist + 手写布局 + `MemDescBuilder` | ❌ 「bindgen 生成即可」 |
| 风险 | 可控（owned buffer） | **高**：直接 bindgen FAM 在 Rust 中难用且不安全 |

### 3.3 绑定路径：手写 vs bindgen

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| 决策 | **预生成 bindgen**（非默认构建时）；排除 wrapper.c / 纯手写主路径 | 笼统「bindgen」 |
| 理由 | FAM + `ub` 常量 + docs.rs/CI | 无对比表 |
| 再生流程 | `regenerate_bindings.sh` + CI diff | 无 |

### 3.4 分层与 `links`

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| Crate | 首版即 **`obmm-sys` + `obmm`** | 单 crate，**以后**再拆 |
| `links` | **`links = "obmm"`** | 仅 `-lobmm` 文字 |
| 生态 | 按可发布双 crate 设计 | 偏原型 |

### 3.5 错误与资源

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| `mem_id==0` | ✅ 映射 `ObmmError::InvalidMemId` | 未写 |
| RAII | ✅ `ExportGuard`/`ImportGuard` | △ 提到 `Drop` |
| `set_ownership` / fd | ✅ 独立模块，不混在 export | 未区分 |

### 3.6 许可证

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| 用户态 Mulan PSL v2 vs 内核 GPL | ✅ 明确 Rust 只链用户态 | ❌ |

### 3.7 CI / 硬件

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| 无 OBMM 环境 | systest + `#[ignore]` 集成测试 | 「需要环境」「mock」笼统 |
| 可编译性 | 区分 layout 测试与硬件测试 | 未分层 |

### 3.8 Async

| | 使用技能 | 不使用技能 |
|---|----------|------------|
| 结论 | **不需要**；mmap 与配置 API 分离 | 「视情况加 async」悬空 |
| 依据 | C API 全同步、无 CQ/fd 暴露 | 无 |

## 4. 可开工程度（主观评分）

| 标准 | 使用技能 | 不使用技能 |
|------|----------|------------|
| 新人能否按文档搭 crate 骨架 | 高 | 低 |
| 能否避免 FAM/ub 首周踩坑 | 高 | 低 |
| 评审可检查决策完整性 | 高（决策表 + 专篇） | 低 |
| 适合直接进 implement 的 API 草案 | 高（模块树 + 类型表） | 中低 |

## 5. 结论

1. **技能路径**将 obmm 的 FFI 设计落到 **可评审的六件套**，并在 **绑定策略（预生成 bindgen + FAM 处理）**、**`ub/obmm.h` 依赖**、**双 crate + links**、**async 否定结论** 等与实现强相关的点上给出明确方案。  
2. **无技能路径**能快速列出 API 名单和「用 bindgen」方向，但 **遗漏 UB 头文件、FAM、许可证与 CI 分层**，且 **未显式完成手写 vs bindgen 的决策**（用户 D13 要求项）。  
3. 对 **团队可维护、Rust 生态共享** 目标，技能产出更接近 [plan.md](../plan.md) 阶段 D「仅凭 guide 做首版架构」的完成定义；无技能产出适合 **头脑风暴**，不宜直接进入实现。

## 6. 建议

- 正式开发 libobmm Rust 绑定时，以 **`design/use-skills/rust-ffi-design/`** 为基线。  
- 可将本对照实验记入技能 README：复杂 C 库（外部头文件 + FAM + 内核耦合）差异最大。

## 7. 文件清单

```text
design/
├── COMPARISON.md                 # 本文件
├── use-skills/
│   └── rust-ffi-design/
│       ├── README.md
│       ├── ARCHITECTURE.md
│       ├── BINDING.md
│       ├── SAFE-API.md
│       ├── ASYNC-ANALYZE.md
│       └── CHECKLIST.md
└── without-skills/
    └── DESIGN.md
```
