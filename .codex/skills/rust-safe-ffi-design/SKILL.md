---
name: rust-safe-ffi-design
description: Designs publishable, full-stack Rust FFI architecture for C libraries, including -sys bindings, build.rs/linking, safe API boundaries, tests/CI, async or upper-layer crate decisions, and release-oriented tradeoffs. Use this skill whenever the user is binding a C or extern "C" library, asks for Rust FFI architecture, safe wrapper design, -sys/safe crate splits, bindgen vs handwritten decisions, wrapper.c/mummy choices, or wants a rigorous multi-file FFI design review.
disable-model-invocation: false
---

# rust-safe-ffi-design

为 **C / `extern "C"`** 库设计可发布到 Rust 生态的 **完整 FFI 栈**：`-sys`、safe crate、`build.rs`/linking、测试与 CI、async/上层协议 crate 必要性，并产出固定的 **6 份架构交付物**。

本技能面向通用发布场景，默认追求严谨、可评审、可开工。它不是代码生成技能；除非用户明确要求，不编译、不实现、不重写 C API 手册。

## 使用边界

### 适用

- 新绑定一个 C 库，需要完整 Rust FFI 架构设计。
- 评估 **手写 extern / 预生成 bindgen / 构建时 bindgen / wrapper.c / mummy**。
- 决定是否拆分 **`foo-sys` + `foo`**，是否需要三层 workspace、async crate 或上层协议 crate。
- 审查已有 Rust FFI 草稿，要求输出设计级修订方案。
- 用户点名 `rust-safe-ffi-design`、`@rust-safe-ffi-design`，或提到「FFI 架构设计」「safe 封装设计」「Rust 绑定 C 库」。

### 不适用

- C++ 全量绑定。若只有稳定 `extern "C"` facade，可限定在该 facade 上设计；否则说明需另案处理。
- 单纯解释一个 FFI 语法点。
- 直接写完整实现代码、跑构建、发布 crate。

## 输出约定

| 项 | 约定 |
|----|------|
| **语言** | 中文正文 + 英文术语（`-sys`、`bindgen`、`RAII`、`Send`/`Sync`） |
| **位置** | 用户指定优先；否则 `docs/ffi-design/<lib>/`；仍无法定位时用 `<C 项目同级>/rust-ffi-design/` |
| **文件** | 固定 6 个，来自 [templates/](templates/)：`README.md` + 5 份设计正文 |
| **风格** | 最终设计文档：结论性、可评审、可开工；不暴露内部样本引用 |
| **分析方式** | 静态分析：头文件 + 仓库元数据；默认不 `cargo build` / 不编译 C 项目 |
| **严谨性** | 每个核心决策必须能追溯到 C API 画像、发布目标或维护约束 |

### 输出模式

默认输出是可评审的方案设计文档。若用户给出输出目录、要求生成文档、或没有限制写文件，则按「输出约定」在目标目录生成固定 6 份 Markdown 交付物。

若用户明确要求「不要创建文件」「只在回复中输出」「不要修改仓库」，则不要落盘；仍按同一设计结构组织回复内容，让读者能看到架构、绑定与构建、safe API、检查清单、async/上层协议分析等部分。

若用户要求审查已有草案，先输出设计级修订方案文档，指出草案风险和替代架构；只有在用户要求完整交付物或未限制写文件时，才落盘生成固定 6 份文档。

## Reference 使用策略

Reference 是内部决策材料，不是交付物内容。读取要按需、分层，避免把内部样本痕迹带入最终文档。

`core/` 是每次必读的干净执行手册，可转化为交付物判断。`cases/`、`rdma/` 和 `comparison.md` 是按画像选读的内部样本和横向材料，只能辅助判断，不能复制进交付物。

### 必读

每次执行先读：

1. [references/README.md](references/README.md) — reference 索引与条件路由。
2. [references/core/decision-questions.md](references/core/decision-questions.md) — Q1-Q10、静态扫描和交付物映射。
3. [references/core/decision-rules.md](references/core/decision-rules.md) — crate 分层、绑定、linking、safe、async、测试与 CI 的默认判断。
4. [references/core/delivery-checklist.md](references/core/delivery-checklist.md) — `CHECKLIST.md` 和最终自检的覆盖基线。

### 条件读取

按静态画像选择额外 reference，通常最多选 1-2 个最相似样本：

| 画像触发 | 可读 reference |
|----------|----------------|
| 纯 `-sys`、复杂 `build.rs` / system-vendored 切换 | `references/cases/libz-sys.md` |
| 双 crate、回调、panic/线程边界 | `references/cases/curl-rust.md` |
| workspace、错误栈、手写+bindgen 混合 | `references/cases/rust-openssl.md` |
| 三层、预生成 bindgen、大 API 面 | `references/cases/zstd-rs.md` |
| ibverbs / rdma-core / RDMA 相关 | `references/rdma/README.md` 与 `references/rdma/overview.md` |
| 需要横向模式核对 | `references/comparison.md` |

### 禁止泄漏

交付物中不要出现：

- `P1`-`P11`、`FFI-ANALYSIS`、`general-c-ffi`、`comparison`、`→ §x`、`裁定`、`阶段 C/D` 等内部证据标记。
- 「样本 P4」「参考某项目」这类样本归因。
- 大段复制 reference 原文。

## 工作流

执行时复制并更新这份进度：

```text
- [ ] 0. 锁定输入、输出与发布意图
- [ ] 1. 静态扫描 C 项目并形成库画像
- [ ] 2. 填写 Q1-Q10 决策问卷
- [ ] 3. 按画像读取 reference 并完成架构决策
- [ ] 4. 生成 6 份设计交付物
- [ ] 5. 做交叉一致性与发布可用性检查
```

### 0. 锁定输入、输出与发布意图

先从用户消息、当前目录和仓库结构推断。只有缺少阻塞信息时，才一次性询问，不要把 Q1-Q10 当成用户访谈逐题发问。

| 项 | 获取方式 |
|----|----------|
| **C 项目路径** | 用户给出的路径、当前仓库、`include/`、`wrapper.h`、主头文件 |
| **输出根目录** | 用户指定优先；否则 `docs/ffi-design/<lib>/`；仍无法定位时用 C 项目同级 `rust-ffi-design/` |
| **发布意图** | 用户指定优先；未知时按 crates.io 生态发布设计，并在未决项说明 |
| **已有 Rust 草稿** | 若存在 `Cargo.toml`、`*-sys`、`build.rs`、`src/lib.rs`，对齐现状；否则从 0 设计 |

如果无法确认 C 项目路径或输出根目录，最多问一个合并问题。

### 1. 静态扫描 C 项目并形成库画像

在 C 项目目录内收集。默认不通读 `.c` 实现；只有为理解 `static inline`、宏、ABI 或资源释放语义时，才读取相关小片段。

| 来源 | 提取 |
|------|------|
| `*.h` / `include/` | opaque、值结构体、ops 表、回调、union、bitfield、`static inline`、宏 API |
| `README*` / `INSTALL*` / 文档片段 | 链接方式、线程安全、版本、平台、许可证 |
| `CMakeLists.txt` / `Makefile` / `meson.build` / `*.pc.in` | 库名、pkg-config、依赖、feature、多后端、最低版本 |
| 目录结构 | 单库/多库、vendored 可能性、生成头文件、平台分支 |
| 已有 Rust 文件 | crate 命名、`links`、feature、unsafe 边界、测试形态 |

形成内部笔记，至少覆盖：

- API 表面：主要模块、函数族、类型族。
- 资源模型：create/destroy、引用计数、借用、全局初始化。
- 错误模型：返回码、`errno`、错误栈、回调错误、可恢复性。
- 并发模型：线程安全声明、fd/event/callback、阻塞调用。
- 构建模型：系统库、vendored、pkg-config、平台差异。
- 许可与发布风险。

最终把这些结论并入 `ARCHITECTURE.md` 的「库画像」节，不写「证据来源」表。

### 2. 填写 Q1-Q10 决策问卷

优先从静态扫描中填写；未知项写 `Unknown` 并进入风险/未决项。只有会改变架构方向的未知项才询问用户。

| # | 问题 | 影响 |
|---|------|------|
| Q1 | opaque / ops / 值结构体？ | safe 类型、所有权、`repr(C)` 暴露策略 |
| Q2 | 大量 inline / 宏 / union / bitfield？ | bindgen、wrapper.c、手写替代 |
| Q3 | create/destroy / 引用计数 / borrow？ | RAII、`Drop`、clone/refcount、生命周期 |
| Q4 | errno / 返回码 / 错误栈 / out param？ | `Error`、`Result`、错误上下文 |
| Q5 | 系统库 / vendored / 多后端？ | `build.rs`、feature、`links`、CI |
| Q6 | CI 无 dev 包 / 无硬件 / 平台限制？ | systest、mock、feature gate、docs.rs |
| Q7 | 独立 `-sys` + 生态发布？ | crate 分层、semver、公开 ABI 面 |
| Q8 | 许可证与下游传染？ | vendored、static link、README 警示 |
| Q9 | safe 上是否挂协议/运行时？ | 三层 workspace、协议 crate、依赖隔离 |
| Q10 | 是否需要 async 或 reactor 集成？ | `ASYNC-ANALYZE.md`、tokio/async-std/外置 crate |

### 3. 架构决策

基于 Q1-Q10 与 reference 完成内部决策。每个结论要有简短理由，避免只写偏好。

必须明确：

1. **crate 分层**：纯 `-sys`、双 crate、三层 workspace、或复用已有 `-sys`。
2. **绑定路径**：手写 extern、预生成 bindgen、构建时 bindgen、wrapper.c、mummy，或组合；同时说明排除路径。
3. **构建链接**：`links`、pkg-config、env override、vendored feature、docs.rs/CI 降级策略。
4. **safe 边界**：RAII、错误、`unsafe` 封装范围、`Send`/`Sync` 策略、回调 panic 策略。
5. **测试与 ABI 守卫**：systest/ctest2、绑定再生检查、smoke tests、无原生库路径。
6. **async/上层协议**：不需要时写 N/A + 理由；需要时明确是在 safe crate、独立 async crate，还是 example 层。

默认倾向可以被画像推翻：

- 对外发布优先 `foo-sys` + `foo`，复杂上层协议再加第三层 crate。
- 无 inline 难题且 API 中大型：预生成 bindgen + ABI/systest。
- API 极小且稳定：手写 extern + ABI/systest。
- inline/宏/bitfield 难题明显：wrapper.c 或手写替代，不把复杂 C 表达式泄漏到 safe 层。
- async 不放在 `-sys`；tokio 等运行时依赖优先放在 safe 之上或独立 crate。

### 4. 生成 6 份交付物

基于 [templates/](templates/) 生成固定 6 个文件，删除模板中的 `<!-- ... -->` 占位说明，确保每份都可独立评审。

| 文件 | 必须包含 |
|------|----------|
| `README.md` | 索引、阅读顺序、一句话决策摘要、当前状态 |
| `ARCHITECTURE.md` | 库画像、crate/workspace 划分、发布目标、许可证、决策摘要表、风险 |
| `BINDING.md` | 绑定路径选择、排除路径、allowlist/blocklist/wrapper、`build.rs`、链接、再生流程 |
| `SAFE-API.md` | 模块树、核心类型、RAII、`Error`、`unsafe` 边界、`# Safety` 契约表、`Send`/`Sync` |
| `CHECKLIST.md` | 从调研到发布的可执行清单，覆盖绑定、safe、测试、CI、文档、发布 |
| `ASYNC-ANALYZE.md` | async 是否需要；若需要，写集成层、线程模型、C 等待/回调关系；若不需要，写 N/A + 理由 |

交付物可以记录 `Unknown` 和未决项，但不能用它们替代核心架构决策；若信息不足，给出保守默认和需要用户确认的影响。

### 5. 交叉一致性与发布可用性检查

交付前逐项检查：

- [ ] 6 个文件全部存在，且文件名与模板约定一致。
- [ ] `ARCHITECTURE.md` 的 crate 分层与 `BINDING.md`、`SAFE-API.md` 一致。
- [ ] `BINDING.md` 明确选择绑定路径，并明确排除至少一个未选路径。
- [ ] `BINDING.md` 的链接策略与 `ARCHITECTURE.md` 一致。
- [ ] `SAFE-API.md` 资源对与 Q3、C 头文件一致。
- [ ] `SAFE-API.md` 对每个公开 `unsafe` 入口都有 `# Safety` 契约草案或封装计划。
- [ ] `ASYNC-ANALYZE.md` 与 Q9/Q10、crate 分层一致。
- [ ] `CHECKLIST.md` 覆盖 `BINDING.md` 与 `SAFE-API.md` 的关键决策。
- [ ] 所有文件无内部 reference 痕迹：`P1`-`P11`、`FFI-ANALYSIS`、`general-c-ffi`、`comparison`、`→ §`、`裁定`、`阶段 C/D`。
- [ ] 中文术语和英文术语统一，结论不是“可考虑”式泛泛建议。

可用以下命令辅助检查输出目录：

```bash
rg "P[0-9]+|FFI-ANALYSIS|general-c-ffi|comparison|→ §|裁定|阶段 [A-Z]" <输出目录>
```

## 质量标准

一次合格的输出应满足：

- **可开工**：开发者能根据 6 份文件创建 workspace、写 `build.rs`、生成/维护 binding、实现第一批 safe 类型。
- **可评审**：每个重大取舍都有简短理由和风险，不需要回到内部 reference 才能理解。
- **可发布**：考虑 crates.io 命名、`links` 冲突、feature、docs.rs、CI、许可证与 semver。
- **可维护**：包含绑定再生、ABI 检查、上游版本升级流程。
- **边界清楚**：明确哪些属于 `-sys`，哪些属于 safe，哪些属于 async/协议层，哪些是非目标。

## 禁止事项

- 不要在交付物中写内部样本、comparison 编号、reference 路径或证据表。
- 不要在无用户要求时运行 `cargo build`、编译 C 项目、安装系统依赖或访问网络。
- 不要通读 vendored `.c` 或重写 C API 手册；只读取与 ABI、宏/inline、资源释放语义相关的必要片段。
- 不要把 C++ 全量绑定纳入本技能范围。
- 不要为了完整性新增第 7 个交付文件；固定产出 6 个文件，额外信息放入对应章节。

## 附加资源

- Reference 索引与条件路由：[references/README.md](references/README.md)
- 每次必读规则：[references/core/](references/core/)
- 内部参考：[references/](references/)
- 输出骨架：[templates/](templates/)
