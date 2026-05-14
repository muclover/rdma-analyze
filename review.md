# FFI 文档架构复审

## 结论

验证通过。

本轮修改已经补齐上次评审指出的核心缺口：文档不再只是样本分析和经验清单，而是形成了“C 库画像 → 绑定闭合策略 → Rust 分层 → safe 边界 → 并发/异步 → 验证策略 → 设计输出模板”的完整设计路径。当前版本可以用于指导新 C 项目的 Rust 安全 FFI API 架构分析。

## 复审依据

### 1. 决策树已升级为架构决策流程

位置：`ffi-docs/methodology-any-c-project.md:7`

新版本先要求做 C 库技术画像，覆盖 API 形态、ABI、头文件难点、资源所有权、线程模型、链接分发和运行时集成。这解决了旧版从“是否发 crates.io”切入导致决策依据偏 Cargo 分发形态的问题。

位置：`ffi-docs/methodology-any-c-project.md:25`

两层决策结构清晰：第一层处理绑定生成与数据路径闭合，第二层处理 Rust 分层与生态适配。这个结构已经能指导新项目先判断 C 边界能否正确闭合，再决定 safe-low、人体工程学层和 async 适配层。

### 2. 绑定生成与闭合策略已进入主路径

位置：`ffi-docs/methodology-any-c-project.md:27`

文档把手写、bindgen、预生成、C wrapper、Rust 手写派发、safe 层派发、静态桩 + `dlopen`/`dlsym`、窄符号集等策略放入主决策表。尤其是 `static inline`、宏、函数指针表影响热路径时，文档明确要求选择闭合策略并验证链接语义。

位置：`ffi-docs/rdma-ffi-schemes.md:20`

RDMA 经验已经回灌到通用方法论，且明确这些策略不限于 RDMA。这解决了旧版 RDMA 专题与通用方法论脱节的问题。

### 3. safe API 边界判定已补足

位置：`ffi-docs/methodology-any-c-project.md:99`

新增的 safe API 准入标准能够指导何时提供 `pub fn`，何时保留 `unsafe`，何时使用 builder/token/guard。它覆盖了指针、长度、生命周期、C 保存指针、跨线程回调、Drop 顺序、`MaybeUninit`、aliasing 等关键安全边界。

这使文档从“如何封装得像 Rust”推进到了“如何证明 safe API 的前提可由 Rust 层维护”。

### 4. `Send` / `Sync` 与并发策略已可执行

位置：`ffi-docs/methodology-any-c-project.md:119`

新增五类 C 并发语义表：全局线程安全、对象级线程安全、仅单线程、每线程上下文、C 跨线程调 Rust 回调，并给出 Rust 类型策略。该表足以指导新项目对原始句柄、owned 包装、borrowed 视图、回调闭包分别做 `Send` / `Sync` 设计。

### 5. 异步适配已从经验说明变成选型表

位置：`ffi-docs/async-ecosystem.md:28`

新增异步适配决策表，按阻塞调用、readiness-driven、completion-driven、callback-driven 四类 C 侧进度模型给出 Rust 承载方式，并补充 cancellation、backpressure、句柄所有权。该部分可以指导新项目判断是否使用 `spawn_blocking`、专用线程、fd readiness + Waker、completion queue + channel 或 callback bridge。

### 6. 样本分析已能支撑横向选型

位置：`ffi-docs/compare-projects.md:14`

新增架构决策矩阵，按 C API 形态、主要 unsafe 来源、safe 层策略、构建复杂度、async/并发、适用条件和风险进行横向比较。相比旧版“何时像谁”的短总结，现在已经能作为新项目画像后的对照表使用。

### 7. 设计输出模板已补齐落地产物

位置：`ffi-docs/design-output-template.md:1`

新增模板覆盖目标 C 库画像、约束、crate 分层、`-sys` 生成/链接/闭合、safe API 类型模型、生命周期、错误模型、回调与 panic、`Send` / `Sync`、async、测试验证和 unsafe 边界清单。

这解决了旧版“读完后不知道新项目设计文档应长什么样”的问题。

### 8. 测试与验证策略已补足

位置：`ffi-docs/methodology-any-c-project.md:169`

检查表新增 ABI/layout、`ctest`/`systest`、多版本 C 库 CI、动态/静态/vendored/system 链接 smoke、交叉编译、Sanitizer、Miri、回调 panic、Drop/错误路径泄漏测试。这让“安全 FFI API”的设计主张具备可回归验证路径。

## 目标达成度

当前文档已经能够达成以下目标：

- 分析任意 C 库的技术画像与 FFI 风险。
- 选择 `-sys` 绑定生成和数据路径闭合策略。
- 决定是否拆 safe-low、人体工程学层、async adapter。
- 判断 safe API 与 `unsafe` API 的边界。
- 设计 `Send` / `Sync`、回调、panic、异步和 backpressure 策略。
- 输出一份可评审的 Rust FFI API 架构设计文档。
- 用测试矩阵验证 ABI、布局、链接、回调、Drop 和 safe 层逻辑。

## 轻微后续建议

当前已达到可用标准。后续若继续增强，可以补一个“示例填充版设计文档”，例如以 zlib 或一个小型回调 C 库为例，把 `design-output-template.md` 完整填一遍，帮助新读者理解模板粒度。这不是阻塞项。

## 最终结论

验证通过。当前 `ffi-docs` 可以作为“适配任意 C 语言项目的 Rust 安全 FFI API 技术/架构设计方法论”的初版基线。
