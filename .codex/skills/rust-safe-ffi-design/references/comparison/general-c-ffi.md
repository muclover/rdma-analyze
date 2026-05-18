# Rust FFI 样本横向对照（P1–P11）

> **文档性质**：阶段 C **权威**横向对照（全样本）。  
> **范围**：P1–P11，同一类 C / `extern "C"` FFI 样本。  
> **依据**：`docs/**/FFI-ANALYSIS.md`（静态阅读，未构建验证）。  
> **证据深度**：索引级（`→ Pn §x`）。  
> **非目标**：规范性「应做/不应做」、性能排名。

**交叉链接**

| 文档 | 内容 |
|------|------|
| [rdma-overview.md](../rdma/rdma-overview.md) | P5–P11 补充深读；**五 Category 对照** |
| 单项目 | `docs/**/FFI-ANALYSIS.md` |

---

## 0. 范围摘要

| 项 | 约定 |
|----|------|
| **项目** | P1 `libz-sys` · P2 `curl-rust` · P3 `rust-openssl` · P4 `zstd-rs` · P5–P11 RDMA（见 [rdma/README.md](../rdma/README.md)） |
| **权威总表** | 本文 §1（D01–D17） |
| **RDMA 细读** | [rdma-overview.md](../rdma/rdma-overview.md)（列定义与 §1 对齐） |

### 0.1 项目索引

| P | 路径 | 单项目分析 |
|---|------|------------|
| P1 | `libz-sys/` | [FFI-ANALYSIS.md](../libz-sys/FFI-ANALYSIS.md) |
| P2 | `curl-rust/` | [FFI-ANALYSIS.md](../curl-rust/FFI-ANALYSIS.md) |
| P3 | `rust-openssl/` | [FFI-ANALYSIS.md](../rust-openssl/FFI-ANALYSIS.md) |
| P4 | `zstd-rs/` | [FFI-ANALYSIS.md](../zstd-rs/FFI-ANALYSIS.md) |
| P5 | `rdma/category-1/rust-ibverbs/` | [FFI-ANALYSIS.md](../rdma/category-1/rust-ibverbs/FFI-ANALYSIS.md) |
| P6 | `rdma/category-2/rdma/` | [FFI-ANALYSIS.md](../rdma/category-2/rdma/FFI-ANALYSIS.md) |
| P7 | `rdma/category-3/rust-rdma-io/` | [FFI-ANALYSIS.md](../rdma/category-3/rust-rdma-io/FFI-ANALYSIS.md) |
| P8 | `rdma/category-4/rdma-sys/` | [FFI-ANALYSIS.md](../rdma/category-4/rdma-sys/FFI-ANALYSIS.md) |
| P9 | `rdma/category-4/async-rdma/` | [FFI-ANALYSIS.md](../rdma/category-4/async-rdma/FFI-ANALYSIS.md) |
| P10 | `rdma/category-5/rdma-mummy-sys/` | [FFI-ANALYSIS.md](../rdma/category-5/rdma-mummy-sys/FFI-ANALYSIS.md) |
| P11 | `rdma/category-5/sideway/` | [FFI-ANALYSIS.md](../rdma/category-5/sideway/FFI-ANALYSIS.md) |

---

## 1. 对照总表

行 = 项目；列 = D01–D17。单元格：**短结论** + `→ §章节`。

### 1.1 核心维度（D01–D09）

| P | D01 定位/分层 | D02 C 库/API 形态 | D03 绑定生成 | D04 inline/复杂类型 | D05 链接与分发 | D06 公开 API/safe | D07 资源/生命周期 | D08 错误模型 | D09 并发/async |
|---|---------------|-------------------|--------------|---------------------|----------------|-------------------|-------------------|--------------|----------------|
| **P1** | 纯 `-sys` 单层；workspace 含 `systest`/`maint` | zlib / zlib-ng；`z_stream` 过程式 | **手写** `lib.rs`；无 bindgen | 无典型 inline 难题 | `links=z`；系统/pkg/vcpkg/vendored/zlib-ng | 无 safe；全 FFI 表面 | 无 RAII；调用方 `*End`/ `gzclose` | `Z_*` 返回码；无 Rust `Result` | 无 async；无 `Send`/`Sync` 文档 |
| **P2** | `curl-sys` + `curl` 双层 | libcurl；`CURL`/`CURLM` + 回调 | **手写** `curl-sys/lib.rs` | 回调 trampoline；`httppost` opaque | `links=curl`；默认 **vendored** libcurl | `easy`/`multi` safe | `Easy2`/`Multi` `Drop`；全局 init 不 cleanup | `CURLcode` → `Error` | `multi` 同步 poll；无 `.await` |
| **P3** | workspace：`openssl-sys`+`openssl`+macros+errors | OpenSSL 指针栈；provider/ERR 队列 | **混合**：手写+`handwritten/`；可选 bindgen | 宏/inline → handwritten | `links=openssl`；vendored/多后端 | `foreign-types`；ssl/x509/evp 模块 | `ForeignType` `Drop`/`Clone` | `ErrorStack` | 无 crate 级 async |
| **P4** | **三层**：`zstd-sys`→`zstd-safe`→`zstd` | `ZSTD_CCtx`/流式 API | **预生成** `bindings_*.rs`；可选 bindgen | `experimental` 静态链接 API | `links=zstd`；默认 vendored | 顶层 `Read`/`Write` stream | `CCtx`/`DCtx` RAII | `SafeResult`/`ErrorCode` | `zstdmt` C 内线程；无 tokio |
| **P5** | `ibverbs-sys` + `ibverbs` | libibverbs；opaque + `ops` 表 | bindgen；手写 `ibv_wc` | 经 bindgen 函数 + ops | `links=ibverbs`；cmake vendored 或系统 | safe：context/qp/mr/cq | `Arc` RAII | `io::Error` | nix `poll` 阻塞 wait |
| **P6** | **单 crate** `bindings`+safe | ibverbs + rdmacm | bindgen + **`ibverbs.rs` 手写 inline** | Rust 复刻 `ops`/container_of | pkg-config；**无** `links` | 低层 + 多 example | `Arc<Owner>`+`WeakSet` | crate `error` | example 自研 Tokio |
| **P7** | 6 成员 workspace sys→io→tonic/quinn | ibverbs + rdmacm | **bnd** + **`wrapper.c` ~96 符号** | C wrapper 导出 inline | 系统库 + `cc` shim | `Transport` trait；cm/async_cm | `Arc` 父子树 | `thiserror` | tokio `AsyncFd` |
| **P8** | 纯 `-sys` | ibverbs + rdmacm | bindgen + `verbs.rs`/`types.rs` | 大块手写 `verbs.rs` | pkg-config（较低版本门槛） | 裸 FFI；无 safe | 裸指针；无 RAII | C errno；无统一 enum | 无 |
| **P9** | 单 crate 框架；**无本仓 bindgen** | 经 **`rdma-sys`** | 依赖 crates.io `-sys` | 在 `rdma-sys` | `build.rs` link + pkg-config | `Agent`/MR/CQ 高阶 API | `Arc`+Agent 协议 | `thiserror`+`io::Error` | **CQ task + tokio** |
| **P10** | 纯 `-sys` | ibverbs+rdmacm（**mummy**） | bindgen + 手写 `verbs.rs` | mummy 可链接符号 | **静态 mummy**；无 pkg-config | 裸 FFI | 裸指针 | C errno | 无 |
| **P11** | 单 crate safe；依赖 P10 | 经 `rdma-mummy-sys` | 无本仓 bindgen | 调用 mummy 导出 | 编译 mummy；**运行**要 rdma-core | `PostSendGuard` 类型状态 | `Arc`+guard 不变量 | `thiserror` 分模块 | 无 |

**证据（§1.1）**：P1 → §0–§7 · P2 → §0–§7 · P3 → §0–§7 · P4 → §0–§7 · P5 → §0–§7 · P6 → §0–§7 · P7 → §0–§7 · P8 → §0–§7 · P9 → §0–§7 · P10 → §0–§7 · P11 → §0–§7。

### 1.2 扩展维度（D10–D17）

| P | D10 上层适配 | D11 测试/无硬件 CI | D12 Mock/mummy | D13 许可证 | D14 §10 决策摘要 | D15 Feature 要点 | D16 上游 `-sys` | D17 文档/可发现性 |
|---|--------------|-------------------|----------------|------------|------------------|------------------|----------------|-------------------|
| **P1** | 无（README→flate2） | **`systest`** ctest2；多 target CI | 无 | MIT/Apache-2.0 | 双发布 `libz-ng-sys`；默认系统 libz | `zlib-ng`/`static`/stock | 自包含 | README 明确纯 `-sys` |
| **P2** | 无独立协议 crate | `systest`；7 个 examples | 无 | MIT/Apache-2.0 | 默认 vendored；回调 panic 捕获 | `ssl`/`http2`/`static-curl`/`rustls` | 传递 `libz-sys` 等 | examples 覆盖 HTTPS/multi |
| **P3** | 无 | `systest`；模块测试极多 | 无 | MIT/Apache-2.0 | 手写默认；`foreign-types`；多 SSL 后端 | `vendored`/`bindgen`/BoringSSL/AWS-LC | `bssl-sys`/`aws-lc-sys` | crate 文档为主 |
| **P4** | README→async-compression | fuzz；分层 examples | 无 | MIT/Apache-2.0 | 三层递进；预生成绑定 | `legacy`/`zdict_builder`/`bindgen` | 自包含 path 链 | `it_work` sys 示例 |
| **P5** | 无 | CI 装 dev 包；vendored 可选 | 无 | MIT/Apache-2.0 | 经典双 crate；vendored 服务 docs.rs | vendored rdma-core feature | 自包含 `ibverbs-sys` | README+双 crate 清晰 |
| **P6** | example `rdma-async` | CI 自建 rdma-core | 无 | MIT | 单 crate 降 workspace 成本 | — | 无 | examples 多、偏实验 |
| **P7** | **tonic** / **quinn** crate | siw+rxe CI jobs | 无 | MIT | wrapper 链；bnd；Transport 抽象 | workspace 多 feature | 自研 `bnd-rdma-sys` | 文档+生成绑定检入 |
| **P8** | 无 | rxe + `run.sh` | 无 | 见仓库 LICENSE | blocklist 复杂类型；纯 sys | — | 无 | README 偏维护者 |
| **P9** | 自身为 async 框架 | submodule 软 RDMA | 无 | **GPL-3.0-only** | 不重复绑定；Agent 控制面 | `cm` feature | **`rdma-sys` 0.3.0** | README 架构说明 |
| **P10** | 无 | **`cargo build` only** 友好 | **mummy 静态链** | 见仓库 LICENSE | mummy 解 inline/CI | cmake mummy | 自包含 | README 强调无 dev 包编译 |
| **P11** | 无 | trybuild；运行要真库 | 编译依赖 P10 | **MPL-2.0** | Extended Verbs+类型状态 | — | **`rdma-mummy-sys` 0.2.3** | API 文档+compiletest |

---

## 2. 分维度索引

仅列差异要点 + 章节索引；共性见 §3。

### D01 定位 / 分层

| 模式 | 项目 | 索引 |
|------|------|------|
| 纯 `-sys` | P1,P8,P10 | P1 §0 · P8 §1 · P10 §1 |
| `-sys` + safe 双 crate | P2,P5 | P2 §1 · P5 §1 |
| 三层递进 | P4 | P4 §1 |
| 多 crate workspace | P3,P7 | P3 §1 · P7 §1 |
| 单 crate 内嵌 bindings | P6 | P6 §1 |
| 外部 `-sys` + 框架 | P9,P11 | P9 §1 · P11 §1 |

### D03 绑定生成

| 方式 | 项目 | 索引 |
|------|------|------|
| 全手写 | P1,P2 | P1 §3 · P2 §3 |
| 预生成 + 可选 bindgen | P4 | P4 §3 |
| 手写 + handwritten + 可选 bindgen | P3 | P3 §3 |
| bindgen + 手写补 inline/类型 | P5,P6,P8,P10 | P5 §3 · P6 §3 · P8 §3 · P10 §3 |
| bnd + C wrapper | P7 | P7 §3 |
| 无本仓 bindgen | P9,P11 | P9 §3 · P11 §3 |

### D04 inline / 复杂类型

| 策略 | 项目 | 索引 |
|------|------|------|
| N/A 或轻 | P1–P4 | 各 §3 |
| ops 派发 / 手写 verbs | P5,P6,P8,P10 | P6 §3 · P8 §3 · P10 §3 |
| C wrapper 导出 | P7 | P7 §3 |
| mummy 符号 | P10,P11 | P10 §3 · P11 §3 |

### D05 链接与分发

| 策略 | 项目 | 索引 |
|------|------|------|
| `links` 键 | P1,P2,P3,P4,P5 | 各 §2 |
| 默认 vendored 静态 | P2,P4 | P2 §2 · P4 §2 |
| pkg-config 系统库 | P6,P7,P8 | P6 §2 · P7 §2 · P8 §2 |
| mummy 静态、运行 dlopen | P10 | P10 §2 |
| 编译 mummy + 运行要系统库 | P11 | P11 §2 |

### D08 错误模型

| 模型 | 项目 | 索引 |
|------|------|------|
| C 返回码 / errno 直暴露 | P1,P8,P10 | P1 §6 · P8 §6 · P10 §6 |
| 映射到 crate `Error`/`io::Error` | P2,P5,P6,P7,P9,P11 | 各 §6 |
| `ErrorStack` | P3 | P3 §6 |
| `SafeResult`/`ErrorCode` | P4 | P4 §6 |

### D09 并发 / async

| 形态 | 项目 | 索引 |
|------|------|------|
| 无 | P1,P3,P4,P5,P8,P10,P11 | 各 §7 |
| multi 同步事件循环 | P2 | P2 §7 |
| tokio + CQ（库内建） | P7,P9 | P7 §7 · P9 §7 |
| example 层 Tokio | P6 | P6 §7 · §8 |

### D10–D17

- **D10**：仅 P7（tonic/quinn）有独立上层适配 crate → P7 §4.3–§4.4；P6 仅在 example → P6 §8。  
- **D11**：P1 `systest` → P1 §8；P10 build-only CI → P10 §8；P7 siw/rxe → P7 §8。  
- **D12**：P10 mummy → P10 §2、§10；P11 依赖 → P11 §2。  
- **D13**：P9 GPL-3.0 → P9 §0、§11；P11 MPL-2.0 → P11 §0。  
- **D14**：各项目 §10（至少 3 条推断）。  
- **D15–D17**：见 §1.2 列。

---

## 3. 模式归纳（架构决策向）

供 [architecture-decisions.md](../guide/architecture-decisions.md) 引用的**选项分类**（非规范性推荐）。

| 模式 ID | 决策问题（简） | 描述 | 代表项目 | 索引 |
|---------|----------------|------|----------|------|
| **M0** | 是否只要 FFI 表面？ | 纯 `-sys`，无 safe | P1,P8,P10 | P1 §10.1 · P8 §10 · P10 §10 |
| **M1** | 是否拆独立 `-sys` crate？ | `-sys` 可单独发布 | P1,P2,P5,P8 | P2 §10 · P5 §10 |
| **M2** | safe 层厚度？ | 薄 safe（类型别名+RAII）vs 框架 | P4 safe · P5 · P9 | P4 §4 · P5 §4 · P9 §4 |
| **M3** | 绑定如何维护？ | 手写 vs 预生成 vs 构建时 bindgen | P1,P2 / P4 / P3,P5 | §2 D03 |
| **M4** | 头文件 inline 怎么办？ | wrapper.c / 手写 verbs / mummy | P7 / P6,P8 / P10 | §2 D04 · §5 T1 |
| **M5** | 链接策略？ | 系统共享 vs vendored vs mummy 静态 | P1,P2 / P4 / P10 | §2 D05 |
| **M6** | 错误如何映射？ | 返回码直暴露 / `io::Error` / `ErrorStack` / thiserror | P1 / P2,P5 / P3 / P7,P9 | §2 D08 |
| **M7** | 是否需要 async 一等支持？ | 无 / multi 手动 / tokio 集成 | P1–P4 / P2 / P7,P9 | §2 D09 |
| **M8** | 是否叠加上层运行时？ | 协议 crate 零新增 `extern "C"` | P7 | P7 §4.3–§4.4 · §10 |
| **M9** | CI 无原生库怎么办？ | systest / mummy / 软 RDMA | P1 / P10 / P7,P9 | §2 D11–D12 |
| **M10** | 是否复用他人 `-sys`？ | 本仓不 bindgen，锁版本 | P9,P11 | P9 §3 · P11 §3 · §4 S5 |

**分层深度（由浅到深，跨域）**

```text
纯 -sys          P1, P8, P10
-sys + 薄/无 safe  P1(明确无), P8, P10
-sys + safe       P2, P5, P6, P11
三层 API           P4
多 crate workspace P3, P7
框架层             P9
```

---

## 4. 特殊情况

总表无法容纳的样本特性；详见单项目 §10。

### S1 — P1：同仓双 crate 发布（`libz-sys` / `libz-ng-sys`）

- 不同 `links`（`z` vs `z-ng`）分离符号空间 → P1 §1.2、§10  
- 默认倾向**系统共享 libz** → P1 §2.3、§10  

### S2 — P3：多 crate + 多 SSL 后端

- `openssl-sys` 与 `openssl` 分离；BoringSSL/AWS-LC 分支 → P3 §1、§10.4  
- 默认**手写**绑定，bindgen 可选 → P3 §3、§10.2  

### S3 — P4：三层递进 + 预生成绑定矩阵

- `bindings_*.rs` 按 feature 组合检入 → P4 §3  
- 顶层 stream API，async 外置到 `async-compression` → P4 §4.4、§7  

### S4 — P7：最大 workspace + 上层适配

- **tonic** / **quinn** 仅依赖 `rdma-io` safe，无新增 C 绑定 → P7 §4.3–§4.4  
- **bnd** + `wrapper.c` 三分区 → P7 §3、§10  

### S5 — P9：外置 `rdma-sys`（非工作区 P8）

- 本仓 `build.rs` 只 link；绑定版本锁定 **0.3.0** → P9 §1、§3  
- **GPL-3.0-only** → P9 §0  

### S6 — P10：mummy 静态链

- 编译期不依赖 `libibverbs-dev`；运行 dlopen → P10 §2、§10  

### S7 — P11：依赖 P10 + Extended Verbs 类型状态

- `PostSendGuard`；trybuild 不变量 → P11 §4.4、§8、§10  

### S8 — P6：rdmacm 已生成但 safe 未接

- bindgen 含 CM；safe 偏 verbs → P6 §3、§4  

---

## 5. 需区分专题

### T1 — inline / 复杂 C 类型（RDMA 为主）

| 策略 | 项目 | 索引 |
|------|------|------|
| blocklist + 手写结构 | P5,P8,P10 | P5 §3 · P8 §3 |
| 大块 `ibverbs.rs` | P6,P8,P10 | P6 §3 |
| `wrapper.c` | P7 | P7 §3 |
| mummy | P10,P11 | P10 §2 |

通用 C 样本（P1–P4）以手写或预生成为主，inline 压力低于 RDMA → 各 §3。

### T2 — `-sys` 是否独立 crate

| 形态 | 项目 |
|------|------|
| 独立 `-sys` | P1,P2,P5,P8,P10 |
| path 依赖但未 workspace 多 member | P2 |
| 内嵌 `pub mod bindings` | P6 |
| 依赖 crates.io `-sys` | P9,P11 |

→ P2 §1 · P6 §1 · P9 §3 · P11 §2

### T3 — 链接：`links` / pkg-config / vendored / mummy

见 §1.1 D05 与 §2 D05；决策树素材 → 阶段 D `architecture-decisions.md`。

### T4 — async 集成

| 级别 | 项目 |
|------|------|
| 无 | P1,P3,P4,P5,P8,P10,P11 |
| 库外/runtime 手动 | P2 `multi` |
| crate 内 tokio | P7,P9 |
| 仅 example | P6 |

### T5 — 无硬件 / CI

| 策略 | 项目 | 索引 |
|------|------|------|
| ABI systest | P1,P2,P3 | 各 §8 |
| mummy build-only | P10 | P10 §8 |
| 软 RDMA / rxe / siw | P7,P8,P9 | P7 §8 · P8 §8 · P9 §8 |

### T6 — 许可证与下游架构

- **GPL-3.0**（P9）→ 专有产品链接需单独法律评估 → P9 §11  
- **MPL-2.0**（P11）→ 文件级 copyleft 范围 → P11 §0  

---

## 6. 未覆盖项

| 项 | 原因 |
|----|------|
| P5–P11 Category 级对照细表 | 见 [rdma-overview.md](../rdma/rdma-overview.md) §0.7 |
| P9 与**本工作区 P8** 符号级差异 | P9 用 crates.io `rdma-sys`，未 vendored 对比 |
| 性能、延迟、厂商 OFED | 非目标 |
| `cargo build` / ABI 实测 | 阶段 A/B 裁定不构建 |
| 规范性最佳实践条文 | 阶段 D `docs/guide/` |

---

## 修订记录

| 日期 | 变更 |
|------|------|
| 2026-05-18 | 初稿：P1–P11 总表 D01–D17、模式 M0–M10、§4/§5 清单 |
