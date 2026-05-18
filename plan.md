# Rust FFI 项目分析计划

> 工作区：`rust-ffi-project-analyze/`  
> 状态：阶段 A/B/C/D 已完成（含 `docs/guide/` 新项目指导）。  
> 约束：仅在当前目录内操作；阶段 A/B 单项目分析时不做跨项目技术对比；阶段 C 横向对照已纳入计划，**待你确认对比范围后再实施**。

---

## 〇、总体目标

本仓库全部工作的**最终目的**，不是停留在「读懂 11 个现有项目」，而是：

> **从这些项目的实践中提炼一份可执行的 Rust FFI 最佳实践，形成对新项目可用的指导**——使你在绑定新的 C 库时，能依据该指导完成**架构决策**（分层、绑定方式、链接策略、错误与并发模型等）并指导**具体开发**（crate 划分、`build.rs`、`unsafe` 边界、测试与 CI）。

### 目标产出（面向「新项目」）

| 能力 | 说明 |
|------|------|
| **架构决策** | 给定库画像（API 形态、线程安全、分发方式），能选择合理的 crate 分层与工具链（bindgen / vendored / `-sys` 等） |
| **开发指导** | 有检查清单、推荐默认值、反模式与证据引用，减少从零摸索 |
| **可演进** | 指导文档与单项目分析、横向对照分离；新增样本项目后可回溯更新实践条目 |

### 阶段与目标的对应关系

| 阶段 | 作用 | 与最终目标的关系 |
|------|------|------------------|
| **A / B** | 单项目画像 | 提供**证据**与案例素材 |
| **C** | 横向对照 | 归纳**差异与模式**，为实践提炼打底 |
| **D** | 最佳实践与指导 | **最终交付**：可指导新项目的实践文档与决策辅助 |

阶段 D 为计划内**必达终态**；建议在阶段 C 完成后启动（若 C 范围裁剪，须保证 D 所引用的证据覆盖你关心的场景）。

---

## 一、项目清单（分析对象）

共 **11 个独立 FFI 项目**（4 个通用 C 库绑定 + RDMA 下 5 个 category、7 个仓库目录）。

| 序号 | 路径 | 类型 | 备注（仅路径推断） |
|------|------|------|-------------------|
| P1 | `libz-sys/` | 经典 `-sys` | 含 `systest/`、`maint/` |
| P2 | `curl-rust/` | `-sys` + 安全层 | `curl-sys/` + 主 crate |
| P3 | `rust-openssl/` | 多 crate workspace | `openssl-sys`、`openssl`、macros、errors |
| P4 | `zstd-rs/` | 三层 | `zstd-sys` → `zstd-safe` → 顶层 API |
| P5 | `rdma/category-1/rust-ibverbs/` | RDMA 基础 | `ibverbs-sys` + `ibverbs` |
| P6 | `rdma/category-2/rdma/` | RDMA workspace | `crates/rdma` + 多个 examples |
| P7 | `rdma/category-3/rust-rdma-io/` | 大型 workspace | 多 crate（sys、io、tonic、quinn 等） |
| P8 | `rdma/category-4/rdma-sys/` | `-sys` | 分析时不横向引用 async-rdma |
| P9 | `rdma/category-4/async-rdma/` | 异步安全封装 | 独立单仓分析 |
| P10 | `rdma/category-5/rdma-mummy-sys/` | mock/mummy 绑定 | 含 `rdma-core-mummy` |
| P11 | `rdma/category-5/sideway/` | 另一 RDMA 路线 | 独立单仓 |

### 工作约束

- 只在 `rust-ffi-project-analyze/` 内操作与落盘。
- **阶段 A/B**：每个项目只读对应源码目录，单项目 `FFI-ANALYSIS.md` 中不写跨项目技术对比。
- **阶段 C**：在全部相关单项目分析完成后，基于 `docs/` 内已有画像做横向对照；**启动前须由你书面确认对比范围**（见第四节）。
- **阶段 D**：在 A/B 完成且 C 已产出（或你明确同意跳过/缩减 C）后，撰写 **Rust FFI 最佳实践与新项目指导**（见第四节阶段 D）；这是本计划的**最终交付**。
- **分析文档一律写在仓库根目录 `docs/` 下**，按项目分子目录；**不在**各 FFI 项目目录内新增分析文件。
- 实施顺序：阶段 A → 阶段 B →（你确认对比范围后）阶段 C → 阶段 D。
- **阶段 A/B 的分析范围**以 [project-scope.md](./project-scope.md) 为准（含深度、必读路径、非目标、完成定义）；启动单项目分析前应先确认该文档中的「待你裁定项」。

---

## 二、分析文档落盘规范（`docs/`）

所有分析产出集中在工作区根目录 **`docs/`**（与各 FFI 项目目录并列），**按项目分目录**存放；源码树只读、不改。

### 目录布局

```
docs/
├── README.md                          # 总索引：计划链接、进度、各项目文档路径
├── comparison/                        # 阶段 C 横向对照（范围确认后再写）
│   ├── SCOPE.md                       # 你已确认的对比范围与维度（由你审定后落盘）
│   └── <对照主题>.md                  # 按范围拆分，如 general-c-ffi.md、rdma.md
├── guide/                             # 阶段 D 最终目标：最佳实践与新项目指导
│   ├── README.md                      # 指导入口：阅读顺序、适用场景
│   ├── rust-ffi-best-practices.md     # 最佳实践正文（原则、模式、反模式）
│   ├── architecture-decisions.md    # 架构决策树与分支说明
│   └── new-project-checklist.md       # 新项目启动检查清单与模板要点
├── libz-sys/
│   └── FFI-ANALYSIS.md
├── curl-rust/
│   └── FFI-ANALYSIS.md
├── rust-openssl/
│   └── FFI-ANALYSIS.md
├── zstd-rs/
│   └── FFI-ANALYSIS.md
└── rdma/
    ├── category-1/
    │   └── rust-ibverbs/
    │       └── FFI-ANALYSIS.md
    ├── category-2/
    │   └── rdma/
    │       └── FFI-ANALYSIS.md
    ├── category-3/
    │   └── rust-rdma-io/
    │       └── FFI-ANALYSIS.md
    ├── category-4/
    │   ├── rdma-sys/
    │   │   └── FFI-ANALYSIS.md
    │   └── async-rdma/
    │       └── FFI-ANALYSIS.md
    └── category-5/
        ├── rdma-mummy-sys/
        │   └── FFI-ANALYSIS.md
        └── sideway/
            └── FFI-ANALYSIS.md
```

### 路径对照表

| 序号 | 源码路径（只读） | 分析文档路径（写入） |
|------|------------------|----------------------|
| P1 | `libz-sys/` | `docs/libz-sys/FFI-ANALYSIS.md` |
| P2 | `curl-rust/` | `docs/curl-rust/FFI-ANALYSIS.md` |
| P3 | `rust-openssl/` | `docs/rust-openssl/FFI-ANALYSIS.md` |
| P4 | `zstd-rs/` | `docs/zstd-rs/FFI-ANALYSIS.md` |
| P5 | `rdma/category-1/rust-ibverbs/` | `docs/rdma/category-1/rust-ibverbs/FFI-ANALYSIS.md` |
| P6 | `rdma/category-2/rdma/` | `docs/rdma/category-2/rdma/FFI-ANALYSIS.md` |
| P7 | `rdma/category-3/rust-rdma-io/` | `docs/rdma/category-3/rust-rdma-io/FFI-ANALYSIS.md` |
| P8 | `rdma/category-4/rdma-sys/` | `docs/rdma/category-4/rdma-sys/FFI-ANALYSIS.md` |
| P9 | `rdma/category-4/async-rdma/` | `docs/rdma/category-4/async-rdma/FFI-ANALYSIS.md` |
| P10 | `rdma/category-5/rdma-mummy-sys/` | `docs/rdma/category-5/rdma-mummy-sys/FFI-ANALYSIS.md` |
| P11 | `rdma/category-5/sideway/` | `docs/rdma/category-5/sideway/FFI-ANALYSIS.md` |

### 命名与引用约定

- 单项目主文档固定文件名：**`FFI-ANALYSIS.md`**。
- 文档开头「分析对象」须写明对应**源码相对路径**（如 `libz-sys/`），证据索引中的路径亦相对于工作区根目录。
- 可选：某 category 完成后在 `docs/rdma/category-N/README.md` 列出本 category 内各项目分析链接（仍不做跨 category 技术对比）。

---

## 三、单项目分析模板（统一产出）

每个项目完成后，在 **`docs/<项目对应目录>/FFI-ANALYSIS.md`** 写入分析（见上一节路径对照表）。  
**范围与深度**见 [project-scope.md](./project-scope.md)（第一阶段：阶段 A/B）。

### 固定章节

| 章节 | 内容 |
|------|------|
| 0. 摘要 | 一句话：绑什么 C 库、面向谁、分层几层 |
| 1. 仓库结构 | workspace 成员、examples/tests、构建入口 |
| 2. 原生依赖 | 链接谁、`links` / `pkg-config` / vendored / 系统库 |
| 3. 绑定生成 | bindgen / 手写 / 混合；`build.rs` 要点 |
| 4. 分层与公开 API | `-sys` vs safe；主要类型与模块树 |
| 5. 资源与生命周期 | create/destroy、`Drop`、RAII、泄漏风险点 |
| 6. 错误与安全边界 | 错误类型、`unsafe` 集中在哪、调用契约 |
| 7. 并发与 async | `Send` / `Sync`、线程模型、是否 async（如有） |
| 8. 测试与示例 | 如何验证、是否需要硬件 / CI 特殊步骤；examples 简单浏览即可 |
| 9. 证据索引 | 关键路径/模块列表（简化，见 [project-scope.md](./project-scope.md)） |
| 10. 架构决策推断 | 结合 C 头文件与本项目架构，推断「为何这样设计」（`inferred` 标注） |
| 11. 待澄清 | 仅本项目内仍不确定的点 |

不适用处写 `N/A — <原因>`，不删节。章节范围与深度见 [project-scope.md](./project-scope.md)。

---

## 四、执行顺序与阶段安排

**原则：** 先掌握成熟 C FFI 模式，再进入 RDMA；RDMA 按 category 编号由浅入深；同 category 内先 `-sys` 再高层；最后沉淀为可指导新项目的实践文档。

```
阶段 A（通用 C FFI）           → 证据：单项目画像
  P1 → P2 → P3 → P4

阶段 B（RDMA）                 → 证据：单项目画像
  P5 → … → P11

阶段 C（横向对照）             → 模式归纳；gated：你确认对比范围后
  docs/README.md → comparison/SCOPE.md → comparison/<主题>.md

阶段 D（最佳实践与新项目指导）  → 【最终目标】可执行的指导体系
  docs/guide/README.md
    → rust-ffi-best-practices.md
    → architecture-decisions.md
    → new-project-checklist.md
```

### 阶段 A：通用 C 库绑定（4 轮）

| 轮次 | 项目 | 预估工作量 | 阅读重点 |
|------|------|------------|----------|
| A1 | P1 `libz-sys` | 0.5–1 天 | 最简 `-sys` 样板：link、bindgen、feature |
| A2 | P2 `curl-rust` | 1 天 | 网络库、多 feature、安全封装厚度 |
| A3 | P3 `rust-openssl` | 1–1.5 天 | 多 crate、宏、错误子 crate |
| A4 | P4 `zstd-rs` | 1 天 | vendored 源码、三层递进 |

**里程碑：** 四套「经典 FFI 分层」理解模板定型，后续 RDMA 复用同一 `FFI-ANALYSIS.md` 结构。

### 阶段 B：RDMA（7 轮，按 category）

| 轮次 | 项目 | 预估工作量 | 说明 |
|------|------|------------|------|
| B1 | P5 `category-1/rust-ibverbs` | 1 天 | ibverbs 入口：sys + safe 双 crate |
| B2 | P6 `category-2/rdma` | 1.5–2 天 | 单仓多 example，bindings 目录重 |
| B3 | P7 `category-3/rust-rdma-io` | 2–3 天 | 最大 workspace，按 crate 分子节 |
| B4 | P8 `category-4/rdma-sys` | 1 天 | 仅本目录 |
| B5 | P9 `category-4/async-rdma` | 1.5 天 | 仅本目录，关注 async 与 CQ 类 API |
| B6 | P10 `category-5/rdma-mummy-sys` | 1 天 | mummy/mock 与测试友好绑定 |
| B7 | P11 `category-5/sideway` | 1–1.5 天 | 另一实现路线，独立画像 |

**里程碑（可选）：** 每个 category 可增 `docs/rdma/category-N/README.md`，仅索引本 category 下各分析文档链接，不含跨 category 技术对比。

### 阶段 C：收尾与横向对照（计划内；实施待你确认范围）

阶段 C **已写入本计划**，与阶段 A/B 并列；**不得**在单项目分析（A/B）过程中提前写对照结论。

#### 启动条件（全部满足后才动手）

| # | 条件 |
|---|------|
| 1 | 阶段 A、B 中**纳入对比范围**的所有项目，`docs/.../FFI-ANALYSIS.md` 均已完成 |
| 2 | 你以书面方式确认 **对比范围**（见下「待你确认的范围项」），并同意启动阶段 C |
| 3 | 已将确认结果落盘为 `docs/comparison/SCOPE.md`（可在你确认后由分析方起草，你审定） |

#### 待你确认的范围项（启动 C 前必填）

在对话或 `SCOPE.md` 中明确：

| 项 | 说明 | 示例选项 |
|----|------|----------|
| 项目子集 | 哪些 P1–P11 参与对照 | 仅 P1–P4 / 仅 RDMA P5–P11 / 全部 11 个 / 自定义列表 |
| 对照维度 | 表格中比较哪些列 | 绑定生成、分层、链接策略、错误模型、生命周期、async、测试策略等 |
| 拆分方式 | 一份总表 vs 多份主题文档 | `general-c-ffi.md` + `rdma.md` + `rdma-by-category.md` 等 |
| 深度 | 摘要级 vs 带证据引用 | 是否必须引用各项目 `FFI-ANALYSIS.md` 章节号 |

**在你确认上述范围之前：不撰写 `docs/comparison/` 下任何对照正文。**

#### 阶段 C 交付物

| 顺序 | 交付物 | 说明 |
|------|--------|------|
| C0 | `docs/README.md` | 总索引：链到各单项目分析 + 阶段 C 文档 + 完成状态 |
| C1 | `docs/comparison/SCOPE.md` | 对比范围、维度、项目列表、非目标（你审定版） |
| C2 | `docs/comparison/<主题>.md` | 按 `SCOPE.md` 拆分的横向对照正文；与单项目文档分离 |

#### 横向对照文档建议结构（`docs/comparison/<主题>.md`）

| 章节 | 内容 |
|------|------|
| 0. 范围摘要 | 引用 `SCOPE.md`；本文件覆盖的项目子集 |
| 1. 对照总表 | 行 = 项目，列 = 已确认维度；单元格为结论 + 指向单项目分析章节 |
| 2. 分维度叙述 | 按维度展开差异与共性（仅基于已有 `FFI-ANALYSIS.md`） |
| 3. 模式归纳 | 本范围内的 FFI 模式分类（如纯 sys / sys+safe / async 层） |
| 4. 未覆盖项 | 因范围裁剪而未对比的维度或项目 |

#### 预设对照主题（供你勾选范围时参考，非默认执行）

| 建议文件名 | 默认项目子集 | 用途 |
|------------|--------------|------|
| `general-c-ffi.md` | P1–P4 | 通用 C 库绑定路线对照 |
| `rdma-overview.md` | P5–P11 | RDMA 全路线总览 |
| `rdma-category-N.md` | 各 category 内项目 | 同 category 细粒度对照（按需） |

实际文件名与拆分以你确认的 `SCOPE.md` 为准。

### 阶段 D：Rust FFI 最佳实践与新项目指导（最终交付）

将阶段 A/B 的**案例证据**与阶段 C 的**模式对照**，提炼为**规范性、可执行**的指导，供绑定**新 C 库**时使用。

#### 启动条件

| # | 条件 |
|---|------|
| 1 | 阶段 A、B 全部完成（11 份 `FFI-ANALYSIS.md`） |
| 2 | 阶段 C 已按你确认的范围完成；**或**你书面同意在缩减 C 的前提下启动 D，并注明 D 中引用的证据范围 |
| 3 | （建议）你已通读 `docs/comparison/` 并对模式归纳无异议或已批注 |

#### 阶段 D 交付物（`docs/guide/`）

| 文件 | 用途 | 面向新项目的用法 |
|------|------|------------------|
| `README.md` | 指导总入口、文档地图、推荐阅读顺序 | 新人 / 未来的你从这里开始 |
| `rust-ffi-best-practices.md` | 原则、推荐做法、反模式、与样本项目的证据链接 | 日常设计与 Code Review 参照 |
| `architecture-decisions.md` | **决策树**：库画像 → crate 分层、bindgen、链接、错误、并发/async | **架构决策**时按树走分支 |
| `new-project-checklist.md` | 从「接到新库」到「可发布 `-sys` / safe API」的检查清单 | **开发过程**逐步勾选 |

#### `rust-ffi-best-practices.md` 建议结构

| 章节 | 内容 |
|------|------|
| 1. 适用范围与非目标 | C / `extern "C"`；不覆盖 C++ 全量绑定等 |
| 2. 核心原则 | `unsafe` 边界、所有权、错误不吞、最小公开 API 等 |
| 3. 分层与 crate 策略 | `-sys` / safe / 可选 async；何时拆 workspace |
| 4. 绑定与构建 | bindgen、vendored、pkg-config、`links`、feature 探测 |
| 5. 资源、错误、并发 | RAII、`Drop` 顺序、错误类型、`Send`/`Sync`、async 适配要点 |
| 6. 测试与 CI | systest、mummy/mock、无硬件 CI |
| 7. 反模式 | 从 11 个项目中归纳的「应避免」条目 + 证据 |
| 8. 案例索引 | 每条实践指向 `docs/**/FFI-ANALYSIS.md` 或 `comparison/` 章节 |

#### `architecture-decisions.md` 建议结构

| 章节 | 内容 |
|------|------|
| 1. 库画像问卷 | 绑定前必答项（API 形态、生命周期、线程安全、分发方式…） |
| 2. 决策树 | Mermaid 或分级问答：每层对应推荐选项与例外 |
| 3. 决策记录模板 | ADR 式简短记录，便于新项目留痕 |
| 4. 与样本对照 | 若某 P1–P11 项目采用了树中某分支，注明引用 |

#### `new-project-checklist.md` 建议结构

| 阶段 | 检查项示例 |
|------|------------|
| 调研 | 头文件扫描、ABI 稳定性、上游文档、链接依赖 |
| 脚手架 | workspace 成员、`build.rs`、`-sys` 命名、license |
| 绑定 | wrapper.h、bindgen 配置、重生成流程 |
| 安全层 | 公开类型、错误、`unsafe` 集中模块、文档与 safety 注释 |
| 质量 | 单元测试、示例、CI、MSRV / feature 矩阵 |
| 发布 | docs.rs、交叉编译、下游 breaking 策略 |

#### 阶段 D 的质量标准（完成定义）

- [ ] 新项目读者**无需再读 11 个源码树**，仅凭 `docs/guide/` 即可做出首版架构方案（必要时再查证据链接）。
- [ ] 每条「应做 / 不应做」在文档中有**至少一处**样本项目证据或明确标注为「规范建议（无样本）」。
- [ ] `architecture-decisions.md` 覆盖阶段 C 对照过的主要维度，且无与 `comparison/` 明显矛盾的结论（若有分歧须在 D 中解释取舍）。

---

## 五、单项目内的阅读顺序（实施时用）

每个项目统一 **6 步**：

1. `README*` / 许可证 / CI 配置 → 定位与构建前提
2. 根 `Cargo.toml` + 各 member `Cargo.toml` → workspace 与 feature 矩阵
3. `build.rs`、`wrapper.h`、bindgen 脚本 → 绑定与链接策略
4. `-sys` 或 `bindings/` → 原始 FFI 表面
5. 安全层 `src/lib.rs` 及核心模块 → RAII、错误、`unsafe` 边界
6. `examples/`、`tests/`、`systest/` → 用法与契约

大 workspace（P3、P6、P7）增加一步：**按 crate 各写一小节**，再写总摘要。

---

## 六、时间与交付节奏（建议）

| 里程碑 | 内容 | 建议周期 |
|--------|------|----------|
| M0 | 本计划确认；初始化 `docs/` 目录骨架 | 当前 |
| M1 | 完成 P1–P4 + `docs/` 下 4 份分析文档 | 第 1 周 |
| M2 | 完成 P5–P7（category 1–3） | 第 2 周 |
| M3 | 完成 P8–P11（category 4–5） | 第 3 周 |
| M4 | 完善 `docs/README.md` | 第 3 周末 / 与 B 收尾并行 |
| M5 | 阶段 C：你确认范围 → `SCOPE.md` + `docs/comparison/*.md` | **范围确认后**再排期 |
| **M6** | **阶段 D：`docs/guide/` 四套指导文档（最终目标）** | **C 完成后**；约 1–2 周 |

每完成一个项目：**确认后再开下一个**，避免并行深读导致上下文混杂。  
**仓库工作完成的标志**：M6 达成，且 `docs/guide/README.md` 声明可用于新项目架构与开发指导。

---

## 七、进度跟踪

| 序号 | 源码路径 | 分析文档（`docs/`） | 完成 |
|------|----------|---------------------|------|
| P1 | `libz-sys/` | `docs/libz-sys/FFI-ANALYSIS.md` | ☑ |
| P2 | `curl-rust/` | `docs/curl-rust/FFI-ANALYSIS.md` | ☑ |
| P3 | `rust-openssl/` | `docs/rust-openssl/FFI-ANALYSIS.md` | ☑ |
| P4 | `zstd-rs/` | `docs/zstd-rs/FFI-ANALYSIS.md` | ☑ |
| P5 | `rdma/category-1/rust-ibverbs/` | `docs/rdma/category-1/rust-ibverbs/FFI-ANALYSIS.md` | ☑ |
| P6 | `rdma/category-2/rdma/` | `docs/rdma/category-2/rdma/FFI-ANALYSIS.md` | ☑ |
| P7 | `rdma/category-3/rust-rdma-io/` | `docs/rdma/category-3/rust-rdma-io/FFI-ANALYSIS.md` | ☑ |
| P8 | `rdma/category-4/rdma-sys/` | `docs/rdma/category-4/rdma-sys/FFI-ANALYSIS.md` | ☑ |
| P9 | `rdma/category-4/async-rdma/` | `docs/rdma/category-4/async-rdma/FFI-ANALYSIS.md` | ☑ |
| P10 | `rdma/category-5/rdma-mummy-sys/` | `docs/rdma/category-5/rdma-mummy-sys/FFI-ANALYSIS.md` | ☑ |
| P11 | `rdma/category-5/sideway/` | `docs/rdma/category-5/sideway/FFI-ANALYSIS.md` | ☑ |

### 阶段 C 进度（对比范围确认前保持「未启动」）

| 项 | 路径 | 状态 |
|----|------|------|
| 对比范围确认 | `docs/comparison/SCOPE.md` | ☑ 已裁定 |
| 全样本对照（P1–P11） | `docs/comparison/general-c-ffi.md` | ☑ |
| RDMA 补充 + 五 Category 对照 | `docs/rdma/rdma-overview.md` | ☑ |
| 索引分析| `docs/README.md` | ☐（已链 RDMA 对照） |

### 阶段 D 进度（最终目标）

| 项 | 路径 | 状态 |
|----|------|------|
| 指导入口 | `docs/guide/README.md` | ☑ |
| 最佳实践 | `docs/guide/rust-ffi-best-practices.md` | ☑ |
| 架构决策 | `docs/guide/architecture-decisions.md` | ☑ |
| 新项目清单 | `docs/guide/new-project-checklist.md` | ☑ |
| **最终目标达成** | 上述四套可用于新项目决策与开发 | ☑ |

---

## 八、待确认项

| # | 事项 | 状态 |
|---|------|------|
| 1 | **第一阶段单项目分析范围**：[project-scope.md](./project-scope.md)（已裁定） | 已确认 |
| 2 | **是否从 P1 `libz-sys` 开始第一轮实施**（阶段 A）？ | 待确认 |
| 3 | **阶段 C 对比范围**：[comparison/SCOPE.md](./docs/comparison/SCOPE.md) | 已裁定 |
| 4 | 阶段 C 交付：`general-c-ffi.md`（P1–P11）+ 修订 `rdma-overview.md`（含五 Category 章） | 已完成 |
| 5 | 阶段 D 指导深度：决策树粒度、是否包含 RDMA 专章、是否与现有 `rust-ffi-design` skill 对齐 | 可在 C 完成后确认 |

单项目分析确认后，按 **A1 → libz-sys** 开始；产出写入 `docs/libz-sys/FFI-ANALYSIS.md`。  
阶段 C 在 #3 确认且相关单项目分析完成后执行；**阶段 D 在 C 完成后撰写，作为本计划最终交付。**
