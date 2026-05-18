# Rust FFI 最佳实践

> **状态**：阶段 D 正文（2026-05-18）。  
> **读者**：维护 **可发布 `-sys` / safe crate** 的团队，面向 Rust 生态下游。  
> **证据**：[general-c-ffi.md](../comparison/general-c-ffi.md)、11×`FFI-ANALYSIS.md`（见 `references/` 各样本目录）。  
> **决策树**：[architecture-decisions.md](./architecture-decisions.md)（含 **手写 vs bindgen** 专节 §3.2）。

---

## 1. 适用范围与非目标

### 1.1 适用

- 绑定 **C** 或 **`extern "C"`** 稳定 ABI 的库。  
- 维护 **`build.rs`**、**`-sys`**、可选 **safe** 层，并计划 **semver** 与 **CI**。  
- 需要与 **Cargo `links`**、**feature**、**docs.rs** 协同的库（原则层面；运维细则见 checklist）。

### 1.2 非目标（本指南不写专章）

- C++ 全量绑定、自动生成 C++ 类层次。  
- 性能调优、厂商驱动差异、协议/业务语义。  
- Windows/vcpkg 专章、`no_std` 专章、`cbindgen`（Rust→C）、crates.io 发布运维细则（裁定 D13）。  
- 上述若与样本证据相关，仅在原则或 checklist 中**一笔带过**。

### 1.3 与阶段 C 的关系

| 阶段 C | 阶段 D（本文） |
|--------|----------------|
| 11 个项目**做了什么** | 绑**新库**时**怎么选、怎么开工** |
| 描述性、带 `→ Pn §x` | 核心原则有默认推荐；细节保留选项 |

---

## 2. 核心原则

**默认推荐**适用于多数对外生态 crate；例外见 [architecture-decisions.md](./architecture-decisions.md)。

| 原则 | 说明 | 默认做法 | 样本 |
|------|------|----------|------|
| **独立 `-sys`** | 下游可只依赖 FFI；semver 分离 | 发布 `foo-sys` + `foo` | P2、P5；对比 P6 单 crate |
| **最小公开 `unsafe`** | `unsafe` 收口在 `-sys` 或薄包装 | safe 模块不扩散 `extern` | P4 §6.2 |
| **所有权在类型中** | 对齐 C create/destroy | `Drop` / `ForeignType` / `Arc` | P3 §5；P11 §5 |
| **错误在 safe 边界映射** | `-sys` 保持 C 语义 | `Result` / `ErrorStack` | P3 §6；P4 §6 |
| **链接策略可文档化** | README + `build.rs` 注释写探测顺序 | pkg-config → 系统 → vendored | P1 §2.3；P2 §2.3 |
| **ABI 有回归测试** | 防手写/bindgen 漂移 | `systest` / `ctest2` | P1 §8；P2 §8 |
| **许可证可追溯** | 上游与依赖传染 | README 声明；慎用 GPL 框架作 MIT 依赖 | P9 §0 |
| **FFI 边界不 unwind** | C 回调内 panic 须处理 | `catch_unwind` 或 abort 策略 | P2 `panic.rs` |

**规范建议（无样本）**：在 `extern "C"` 回调中避免 panic 跨越边界（Rustonomicon / FFI 惯例）；与 P2 实践一致。

---

## 3. 分层与 crate 策略

### 3.1 推荐默认（生态共享）

```text
yourlib-sys   ← FFI + build.rs + 绑定（手写或 bindgen）
yourlib       ← safe API、错误类型、文档
（可选）yourlib-macros / yourlib-errors
```

- **`-sys`**：`#![allow(non_camel_case_types, …)]`；类型与 C 对齐；**不** 假装 safe。  
- **safe**：对外稳定 API；`unsafe` 仅集中在调用 `-sys` 的薄层。  
- **版本**：`-sys` 与 safe 可 **不同 major**（P3：openssl-sys 0.9 vs openssl 0.10）。

### 3.2 何时不必拆 safe

| 条件 | 可参考 | 风险 |
|------|--------|------|
| 官方文档要求只用 `-sys` | P1 → flate2 | 下游误用裸指针 |
| 仅内部工具、不发布 | — | semver 无意义 |
| 单 crate 实验（examples 多） | P6 | 绑定与 safe 同版本，难复用 |

### 3.3 三层与 workspace

- **三层**（sys → safe → io）：C API 稳定、需要 `Read`/`Write` 或流式 Ergonomic API → **P4**。  
- **workspace**：API 面极大、多 SSL 后端、上层协议适配 → **P3、P7**；控制成员间 **循环依赖**。

### 3.4 上层适配 crate

- 在 **safe 层之上** 接 gRPC/QUIC 等时，**不新增** `extern "C"`（P7 tonic/quinn）。  
- FFI 止于传输字节；协议类型留在 Rust。

---

## 4. 绑定与构建：手写 vs bindgen（必显式）

> 完整决策树：[architecture-decisions.md §3.2](./architecture-decisions.md#32-绑定手写--bindgen--预生成--wrapper)。  
> 模式 ID：**M3**、[general-c-ffi §2 D03](../comparison/general-c-ffi.md#d03-绑定生成)。

### 4.1 四种主路径对照

| 路径 | 何时优先 | 团队维护要点 | 样本 |
|------|----------|--------------|------|
| **手写 `extern` + 类型** | 符号少、ABI 多年不变 | **必须** systest；升级上游人工 diff | P1、P2 |
| **预生成 bindgen（检入 `.rs`）** | 中大规模、要 docs.rs 简单构建 | 文档化 `regenerate.sh`；feature 组合多文件 | P4 |
| **构建时 bindgen** | 常追新头文件、维护者熟悉 libclang | CI 装 clang；可选 feature 关闭 | P3、P5 |
| **非 bindgen 生成（bnd 等）** | 特殊工具链（如 WinMD） | 与 bindgen 同样需再生流程 | P7 |

### 4.2 默认推荐（无 inline 难题时）

1. **首选：预生成 bindgen + systest** — 构建不依赖 libclang，适合生态消费者与 CI（P4）。  
2. **次选：手写 + systest** — 当公开 FFI 符号 **少且稳定**（P1、P2）。  
3. **慎选：构建时 bindgen 为默认** — 保留手写 fallback 或 `bindgen` feature（P3）。  
4. **inline/宏多**：先走 [§4.4](#44-inline宏与-rdma-类库)，**不要** 用裸 bindgen 硬绑整头文件。

### 4.3 手写实践要点（选手写时）

- 对照 **单一真相头文件**（或 vendored 快照），避免混用系统头与 vendored 定义（P2 vendored 复制头到 `OUT_DIR`）。  
- **Opaque enum** 处理不稳定布局类型（P2 `curl_httppost`）。  
- **条件 `cfg`** 对齐 OpenSSL 式多版本（P3）或精简 feature。  
- **禁止**：数千行手写且无 ABI 测试 → 漂移风险（P1 用 ctest2 **对冲**）。

### 4.4 bindgen 实践要点（选 bindgen 时）

- **blocklist** 复杂 union / 位域结构，在 Rust **手写** 替代（P5 `ibv_wc`、P8 blocklist）。  
- **预生成** 时：CI 加「绑定文件是否与头文件一致」的检查（可选 job）。  
- **构建时**：`bindgen` feature 默认关闭或文档说明 CI 要求（P3 optional bindgen）。  
- **allowlist/blocklist** 控制公开 API 面，避免绑定整个翻译单元。

### 4.5 inline/宏与 RDMA 类库

| 策略 | 说明 | 样本 |
|------|------|------|
| **C wrapper 导出** | `wrapper.c` + `cc` | P7 |
| **Rust 手写 ops/inline** | 大块 `verbs.rs` | P6、P8、P10 |
| **mummy** | 编译期静态符号 | P10 |
| **依赖他人 `-sys`** | 本仓不 bindgen | P9、P11 |

→ [rdma-overview §0.7](../rdma/rdma-overview.md#07-五-category-对照阶段-c)、[general-c-ffi §5 T1](../comparison/general-c-ffi.md#t1--inline--复杂-c-类型rdma-为主)。

### 4.6 `build.rs` 与链接（与绑定并列）

- 写清 **探测顺序**（P1 树状注释）。  
- 需要时用 **`links`** 参与原生库去重（P1–P5）。  
- **vendored** 时考虑 **符号隐藏**（P4 `-fvisibility=hidden`）。  
- **勿** 在同一进程混用两套 zlib/ssl 实现（P1 §10.3、P4 §5）。

---

## 5. 资源、错误、并发

### 5.1 资源与生命周期

| C 模型 | Rust 默认 | 样本 |
|--------|-----------|------|
| 明确 create/destroy | `struct` + `Drop` | P4 `CCtx`、P5 `Arc` |
| 引用计数 | `Clone` + `up_ref` 或 `Arc` | P3 `foreign-types` |
| 纯 `-sys`、无 RAII | 文档 **# Safety** 强调配对 | P1、P8 |
| 全局 init | `Once`；慎 `cleanup` | P2 不调用 `curl_global_cleanup` |
| 父子资源 | `Drop` 顺序、`DetachGuard` | P2 multi、P7 父子树 |

### 5.2 错误模型

- **按 C 库习惯选型**，勿一律 `io::Error`（P3 ERR 栈 vs P4 `ZSTD_isError`）。  
- **`-sys`**：保留返回码/errno；**safe**：统一 `Result`。  
- 框架型库可用 **thiserror** 分模块（P7、P9、P11）。

### 5.3 并发与 `Send`/`Sync`

- **无依据不实现** `Send`/`Sync`（P11 等有明确设计再实现）。  
- **单 handle 非线程安全** 须在文档说明（P1 `z_stream`、libcurl easy）。  
- **async**：默认不在 `-sys` 绑 tokio；库内 async 仅当产品定位为框架（P9）或 Transport 层（P7）。

---

## 6. 测试与 CI

| 实践 | 说明 | 样本 |
|------|------|------|
| **systest/ctest2** | 布局、常量、函数签名 | P1–P3 |
| **往返测试** | 编解码、init/destroy | P4 单元测试 |
| **fuzz** | 压缩等纯函数路径 | P4 fuzz |
| **无硬件可 `cargo build`** | mummy 或仅编译 | P10 |
| **软设备集成** | 标 `ignore`、文档前置条件 | P7、P9 |
| **trybuild** | 类型状态/API 不变量 | P11 |
| **examples** | 至少一条 **safe** 路径 | P2 §1.3 |

**团队默认**：对外 `-sys` 合并前 **systest 绿**；集成测试不阻塞无设备 CI。

---

## 7. 反模式

仅列 **11 个样本中有证据** 的条目（裁定 D9=A）。

| 反模式 | 风险 | 样本证据 |
|--------|------|----------|
| 大手写绑定 **无** ABI 测试 | 上游小改即静默破坏 | P1 用 systest **对照** |
| 进程内 **两套** zlib/ssl 符号 | 链接/运行异常 | P1 §10.3；P4 §5 |
| C 回调 **panic 跨 FFI** | UB | P2 `panic.rs` |
| 将 **GPL-3.0** 框架作 MIT 依赖 | 许可传染 | P9 §0 |
| **无 `Drop`** 的 `-sys` 当 safe 用 | 泄漏/双重释放 | P1 §5.2 |
| bindgen **不 blocklist** 复杂 union | 不可用或错误布局 | P5、P8 §3 |
| 加入 `Multi` 后 **错误 cleanup 顺序** | use-after-free | P2 `DetachGuard` |
| 依赖 **crates.io `-sys`** 却假设与工作区另一 `-sys` 同 ABI | 符号/版本不一致 | P9 vs P8 [S5](../comparison/general-c-ffi.md#s5--p9外置-rdma-sys非工作区-p8) |
| **mummy 编译通过** 即假设生产无需 rdma-core | 运行失败 | P11 §2 |
| 单 crate **暴露** `pub mod bindings` 且无 semver 策略 | 下游锁死内嵌 FFI | P6 §1 |
| 全局 `curl_global_cleanup` 与多线程 | 文档禁止；crate 避免调用 | P2 §5 |

---

## 8. 案例索引

| 主题 | 文档 |
|------|------|
| 全样本总表 D01–D17 | [general-c-ffi.md §1](../comparison/general-c-ffi.md#1-对照总表) |
| 模式 M0–M10 | [general-c-ffi.md §3](../comparison/general-c-ffi.md#3-模式归纳架构决策向) |
| 例外 S1–S8、专题 T1–T6 | [general-c-ffi.md §4–§5](../comparison/general-c-ffi.md#4-特殊情况) |
| RDMA Category | [rdma-overview.md §0.7](../rdma/rdma-overview.md#07-五-category-对照阶段-c) |
| 手写 vs bindgen 决策 | [architecture-decisions.md §3.2](./architecture-decisions.md#32-绑定手写--bindgen--预生成--wrapper) |
| 虚构库演练 | [architecture-decisions.md 附录 B](./architecture-decisions.md#附录-b--虚构库演练裁定-d12) |
| 单项目深读 | `docs/**/FFI-ANALYSIS.md` |

---

## 修订记录

| 日期 | 变更 |
|------|------|
| 2026-05-18 | 阶段 D 正文：原则、分层、**§4 手写/bindgen**、资源/错误/并发、测试、反模式、索引 |
