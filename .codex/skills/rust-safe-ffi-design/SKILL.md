---
name: rust-safe-ffi-design
description: Designs full-stack Rust FFI for C libraries (-sys binding, build.rs/linking, safe API, tests, async/upper-layer needs). Statically analyzes C project headers and repo layout, applies decision trees from project docs/guide, and outputs multi-file architecture deliverables. Use when binding a new C library, designing safe FFI, -sys/safe crate split, bindgen vs handwritten, or user mentions rust-safe-ffi-design.
disable-model-invocation: false
---

# rust-safe-ffi-design

为 **C / `extern "C"`** 库设计可发布到 Rust 生态的 **完整 FFI 栈**（`-sys`、safe、`build.rs`、测试、async/上层协议必要性），并产出**多文件架构交付物**。

## 何时使用

- 新绑 C 库，需要 **架构设计 + safe API 方案**（不仅是代码片段）
- 评估 **手写 vs bindgen / wrapper.c / mummy**
- 决定是否拆 **`-sys` + safe**、是否需要 **async** 或 **上层协议 crate**
- 用户 `@rust-safe-ffi-design` 或提到「FFI 架构设计」「safe 封装设计」

## 内部参考（执行时必读，勿写入交付物）

执行本技能时 **必须先读** [references/README.md](references/README.md) 及其中所列**技能内副本**（`references/guide/`、`references/comparison/` 等），用于决策与默认值；**勿**依赖仓库根 `docs/` 路径（除非副本缺失）。**交付物中不写** P1–P11 证据、`→ §x` 或 comparison 链接。

## 产出约定

| 项 | 约定 |
|----|------|
| **语言** | 中文正文 + 英文术语（`-sys`、`bindgen`、`RAII`） |
| **位置** | 默认 `<输出根>/rust-ffi-design/`；首步向用户确认路径 |
| **文件** | 见 [templates/](templates/)，共 6 个：`README.md` + 5 份设计正文 |
| **风格** | **最终设计文档**：结论性、可评审、可开工；**无**样本引用、无证据表 |
| **分析** | **静态**：头文件 + 仓库元数据；**不** `cargo build`（除非用户明确要求） |

## 工作流

复制并跟踪进度：

```text
- [ ] 0. 确认输入与输出路径
- [ ] 1. C 项目静态画像
- [ ] 2. 库画像问卷（Q1–Q10）
- [ ] 3. 架构决策（读 reference → 内部分支）
- [ ] 4. 撰写 6 份交付物
- [ ] 5. 交叉一致性检查
```

### 0. 确认输入与输出

向用户确认（若上下文已给出则跳过）：

| 项 | 说明 |
|----|------|
| **C 项目路径** | 头文件根、或 `wrapper.h` 路径 |
| **输出根目录** | 默认：C 项目同级或用户指定 `docs/ffi-design/<lib>/` |
| **发布意图** | crates.io 生态 / 仅内部 |
| **是否已有 Rust 草稿** | 有则对齐，无则从 0 设计 |

### 1. C 项目静态画像

在 C 项目目录内收集（**不读** `.c` 实现细节，除非为理解宏/inline）：

| 来源 | 提取 |
|------|------|
| `*.h` / `include/` | API 形态：opaque、回调、ops 表、union、bitfield、`static inline` |
| `README*` / `INSTALL*` | 链接方式、线程安全、版本 |
| `CMakeLists.txt` / `Makefile` / `*.pc.in` | 库名、pkg-config、依赖、最低版本 |
| 目录结构 | 是否多库、是否 vendored 友好 |

产出 **内部笔记**（可简短列表），供步骤 2–3 使用；内容并入 `ARCHITECTURE.md` 的「库画像」节，**不写**「证据来源」字样。

### 2. 库画像问卷

填写下表（Q1–Q9 对齐 [references/guide/architecture-decisions.md](references/guide/architecture-decisions.md) §1；**Q10 为本技能扩展**）：

| # | 问题 | 记录 |
|---|------|------|
| Q1 | opaque / ops / 值结构体？ | |
| Q2 | 大量 inline / 宏 / union？ | |
| Q3 | create/destroy / 引用计数？ | |
| Q4 | errno / 返回码 / 错误栈？ | |
| Q5 | 系统库 / vendored / 多后端？ | |
| Q6 | CI 无 dev 包 / 无硬件？ | |
| Q7 | 独立 `-sys` + 生态发布？ | |
| Q8 | 许可证与下游传染？ | |
| Q9 | safe 上是否挂协议/运行时？ | |
| **Q10** | **是否需要 async 或 reactor 集成？** | 无 / 库内 tokio / 外置 crate / 仅 example |

若 **libibverbs / rdma-core** 类：在内部走 RDMA 子分支（见 reference），交付物中仅写**结论**。

### 3. 架构决策（内部）

按 [references/README.md](references/README.md) 顺序阅读 `references/guide` 与 `references/comparison`，在内部完成：

1. **总览树** → crate 分层（纯 `-sys` / 双 crate / 三层 / workspace）
2. **§3.2 绑定** → 手写 / 预生成 bindgen / 构建时 bindgen / wrapper.c / mummy（**必须显式二选一或组合**）
3. **链接** → `links`、pkg-config、vendored 顺序
4. **错误 / 资源 / Send-Sync**
5. **测试与 CI**
6. **Async** → 是否需要独立 `ASYNC-ANALYZE` 结论（无则写 N/A 与理由）

**默认倾向**（生态可维护，可被画像推翻）：

- 对外：`foo-sys` + `foo`
- 无 inline 难题：预生成 bindgen + systest，或极小 API 手写 + systest
- async：绑定层不绑 tokio；需要时 safe 之上或独立 crate

### 4. 撰写交付物

基于 [templates/](templates/) 生成 6 个文件，**删除**模板中 `<!-- ... -->` 占位说明：

| 文件 | 内容 |
|------|------|
| `README.md` | 索引、阅读顺序、一句话决策摘要 |
| `ARCHITECTURE.md` | 库画像、crate 划分、workspace、许可证、决策摘要表 |
| `BINDING.md` | **手写 vs bindgen** 选定路径、blocklist/wrapper、`build.rs`、链接、再生流程 |
| `SAFE-API.md` | 模块树、核心类型、RAII、`Error`、`unsafe` 边界、`# Safety` 契约表草案 |
| `CHECKLIST.md` | 从调研到发布的勾选清单（可执行） |
| `ASYNC-ANALYZE.md` | 是否需要 async；若需要：集成层、线程模型、与 C 等待/回调关系；若不需要：N/A + 理由 |

### 5. 交叉一致性检查

交付前自检：

- [ ] `BINDING.md` 的链接策略与 `ARCHITECTURE.md` 一致
- [ ] `SAFE-API.md` 资源对与 Q3、C 头文件一致
- [ ] `ASYNC-ANALYZE.md` 与 Q9/Q10、是否含上层协议 crate 一致
- [ ] `CHECKLIST.md` 覆盖 `BINDING` + `SAFE-API` 中的关键决策
- [ ] 六份文件 **无** P1–P11、`FFI-ANALYSIS`、`general-c-ffi` 字样
- [ ] 中文 + 英文术语统一

## 禁止事项

- 交付物中 **不要** 写「样本 P4」「见 comparison §x」等内部参考痕迹
- **不要** 在无用户要求时 `cargo build` C/Rust 项目
- **不要** 通读 vendored `.c` 或重写 C API 手册
- **不要** 将 C++ 全量绑定当作本技能范围（若库为 C++，仅说明需另案处理并停止或缩范围）

## 附加资源

- 决策树与原则详情：[references/README.md](references/README.md)（含 `references/guide/`、`references/comparison/` 副本）
- 副本同步说明：[references/SOURCE.md](references/SOURCE.md)
- 输出骨架：[templates/](templates/)
