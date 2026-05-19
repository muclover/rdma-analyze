# rdma（Nugine/rdma）— FFI 单项目分析

**分析对象（源码路径）：** `rdma/category-2/rdma/`  
**计划序号：** P6（阶段 B）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`rdma`（crates.io 名 `rdma`）绑定 **linux-rdma / rdma-core** 的 **libibverbs** 与 **librdmacm**，面向需要 **底层 RDMA verbs** 能力的 Rust 应用与上层库作者。架构为 **单 crate 内嵌绑定 + safe 封装**：`crates/rdma/src/bindings/` 由 bindgen 从 `generated.h` 生成，另用手写 `ibverbs.rs` 复刻 C 侧 `static inline` 与 provider op 分发；safe 层以 `Arc<Owner>` 管理 `ibv_*` 资源，配合 `WeakSet` 处理 CQ 与完成通道的弱引用环。Workspace 另含三个 **examples**（设备枚举、pingpong、基于 Tokio 的 async RPC 适配），演示 FFI 用法与异步集成思路，**非** 独立发布 crate。

**库画像（摘要）**

| 字段 | 内容 |
|------|------|
| **C 库** | libibverbs（设备/verbs）、librdmacm（连接管理；绑定已生成，safe 层未用） |
| **API 形态** | Opaque `ibv_context` / `ibv_qp` 等；大量经 `verbs_context` / `ibv_context.ops` 的函数指针；扩展 CQ/QP（`ibv_*_ex`）；errno 式失败 |
| **资源对** | `ibv_open_device`↔`ibv_close_device`；`ibv_alloc_pd`↔`ibv_dealloc_pd`；`ibv_create_cq_ex`↔`ibv_destroy_cq`；`ibv_reg_mr`↔`ibv_dereg_mr`；`ibv_create_qp_ex`↔`ibv_destroy_qp` 等 |
| **线程安全** | libibverbs 通常 **每 context 单线程使用** 或需外部序列化；本 crate 对 `Owner` 普遍 `Send + Sync`（`inferred`，由注释「owned type」声明） |
| **分发方式** | **系统库**（pkg-config，最低 libibverbs **1.14.41**、librdmacm **1.3.41**，与 README 一致，`confirmed`） |
| **绑定难点** | `static inline` / 宏；`ibv_reg_mr` / `ibv_query_port` 版本差异；CQ ex 与 legacy CQ 转换；bindgen 需 blocklist 后手写 |

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| `rdma` | `crates/rdma/` | 唯一库 crate：内嵌 `bindings` + safe API |
| `rdma-devices` | `examples/rdma-devices/` | 列出 RDMA 设备 |
| `rdma-pingpong` | `examples/rdma-pingpong/` | RC/UD pingpong（TCP 交换元数据） |
| `rdma-async` | `examples/rdma-async/` | Tokio + CQ 事件线程 + RPC 风格示例 |

根 `Cargo.toml`：`members = ["crates/*", "examples/*"]`，resolver = `"2"`（`confirmed`）。

**无独立 `rdma-sys` crate**；FFI 与 safe 层同处 `crates/rdma`（`inferred`：降低 workspace 复杂度，bindings 通过 `pub mod bindings` 对外可见）。

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `crates/rdma/build.rs` | pkg-config 探测、bindgen 生成 `OUT_DIR/generated.rs` |
| `crates/rdma/src/bindings/generated.h` | 包装 `#include <infiniband/verbs.h>`、`<rdma/rdma_cma.h>`、`<rdma/rdma_verbs.h>` |
| `crates/rdma/src/bindings/generated.rs` | docs.rs 时 include 快照；否则 include `OUT_DIR` |
| `crates/rdma/src/bindings/ibverbs.rs` | 手写 inline / op-table 包装（~470 行量级） |
| `crates/rdma/src/bindings/x86_64_unknown_linux_gnu.rs` | 提交到仓库的 bindgen 快照（docs.rs） |
| `crates/rdma/src/{ctx,pd,cq,qp,mr,...}.rs` | Safe 封装 |
| `crates/rdma/src/weakset.rs` | `WeakSet`：CompChannel 持有 CQ 的 `Weak` |
| `docs/ref/ref.svg` | 资源关系图（README 引用） |
| `.github/workflows/ci.yml` | 从源码构建 rdma-core 后 `fmt` / `clippy` / `build` |
| `justfile` | `dev`、`install-examples`、`bench-pingpong-*` |

### 1.3 Examples（简单阅读）

| 示例 | 入口 | 是否演示 FFI |
|------|------|----------------|
| `rdma-devices` | `examples/rdma-devices/src/main.rs` | 间接 — 仅用 safe `DeviceList` |
| `rdma-pingpong` | `examples/rdma-pingpong/src/main.rs` | 是 — safe `QueuePair`/`MemoryRegion`/`post_send`/`poll`；元数据经 **TCP** 交换 |
| `rdma-async-rpc` | `examples/rdma-async/src/bin/rdma-async-rpc.rs` | 是 — 经 `rdma-async` 库封装 CQ 线程 + Tokio；底层 `unsafe` post/wc |

### 1.4 Tests

| 类型 | 位置 | 说明 |
|------|------|------|
| 单元测试 | `crates/rdma/src/**/*.rs` 内 `#[test]` | 如 `gid.rs`、`guid.rs`、`wr.rs`、`wc.rs`、`ah.rs`、`qp.rs` — 偏布局/转换，无硬件 |
| 集成 `tests/` | **无** | — |
| `systest` / `fuzz` | **无** | — |
| CI | `.github/workflows/ci.yml` | 构建上游 rdma-core；未在本分析中运行 |

---

## 2. 原生依赖

### 2.1 库画像

见 §0 表。README 要求本机安装 [rdma-core](https://github.com/linux-rdma/rdma-core)，且 `pkg-config` 版本不低于 libibverbs **1.14.41.0**、librdmacm **1.3.41.0**（`confirmed`）。

### 2.2 `links` 与传递依赖

- **无** `links = "ibverbs"` 于 `Cargo.toml`（`confirmed`）；由 `pkg-config` 在 `build.rs` 中注入链接标志（`inferred`：依赖构建脚本而非 Cargo native 依赖图）。
- 传递：`libc`、`bitflags`、`parking_lot`、`scopeguard`、`numeric_cast`、`fnv`、`hex-simd`、`nugine-rust-utils`；可选 `serde`、`bytemuck`（feature）。

### 2.3 链接策略（`crates/rdma/build.rs` 摘要）

```
docsrs / DOCS_RS → build.rs 提前 return（不 probe、不 bindgen）
否则:
  pkg_config libibverbs >= 1.14.41, statik(false)
  pkg_config librdmacm >= 1.3.41, statik(false)
  bindgen → OUT_DIR/generated.rs（header: src/bindings/generated.h）
```

- 失败时 `panic!("please install libibverbs-dev …")`（`confirmed`）。
- **无 vendored** rdma-core；CI 单独 checkout `linux-rdma/rdma-core` 并 `build.sh`，再设 `PKG_CONFIG_PATH`（`confirmed` 自 `ci.yml`）。

### 2.4 Feature 矩阵（`crates/rdma/Cargo.toml`）

| Feature | 作用 |
|---------|------|
| `serde` | 为部分类型派生 Serde（examples 用） |
| `bytemuck` | 可选 POD 辅助 |
| `docs.rs` | `package.metadata.docs.rs` + `rustdoc-args = ["--cfg", "docsrs"]` |

examples 无额外 FFI feature；`rdma-async` 依赖 `rdma` 的 `serde` feature。

---

## 3. 绑定生成

### 3.1 方式：**bindgen + 手写混合**

| 组件 | 说明 |
|------|------|
| **bindgen** | `build.rs`：`header("src/bindings/generated.h")`；allowlist `ibv.*`、`IBV.*`、`rdma.*`、`verbs.*`；blocklist `pthread.*`、`timespec`、`socklen_t`；**blocklist** `ibv_reg_mr`、`ibv_query_port`（改由 `ibverbs.rs` 提供） |
| **手写 `ibverbs.rs`** | 注释标明 libibverbs **1.14.41** 的 **static inline**；`container_of` / `verbs_get_ctx` / `verbs_get_ctx_op!`；复刻 `ibv_create_cq_ex`、`ibv_poll_cq`、`ibv_post_send`、`ibv_reg_mr`、WR builder 系列等 |
| **C 头** | `generated.h` 仅三行 include + `_RS_IBV_ACCESS_OPTIONAL_RANGE` 常量导出 |

`generated.rs` 加载逻辑（`confirmed`）：

- 非 docs.rs：`include!(concat!(env!("OUT_DIR"), "/generated.rs"));`
- docs.rs + linux gnu x86_64：`include!("./x86_64_unknown_linux_gnu.rs");`

### 3.2 C 头暴露的主要 API 形态（§4.4）

自 `infiniband/verbs.h`（经 `generated.h`）：

- **设备枚举**：`ibv_get_device_list` / `ibv_free_device_list`；`ibv_open_device` / `ibv_close_device`。
- **保护域与内存**：`ibv_alloc_pd`；`ibv_reg_mr`（inline，经 provider 或 compat 符号）；`ibv_dereg_mr`。
- **完成路径**：`ibv_create_comp_channel`；`ibv_create_cq_ex` + `ibv_cq_ex_to_cq`；`ibv_poll_cq` / `ibv_req_notify_cq`（经 `context->ops`）；`ibv_get_cq_event` / `ibv_ack_cq_events`。
- **队列**：`ibv_create_qp_ex`；`ibv_modify_qp`；`ibv_post_send` / `ibv_post_recv`（inline 转 ops）。
- **扩展写路径**：`ibv_wr_*` 系列（inline，在 `ibverbs.rs` 中实现，safe 层 `wr`/`qp` 以 legacy WR 为主）。

自 `rdma/rdma_cma.h`：

- **连接管理**：`rdma_*` 族（bindgen allowlist `rdma.+`）— **已生成绑定，safe 模块未调用**（`confirmed`：全库 `grep` 无 `C::rdma`）。

### 3.3 再生流程

1. 安装满足版本的 libibverbs / librdmacm dev 包。  
2. `cargo build` 触发 `build.rs` bindgen。  
3. 更新 docs.rs 快照时需将新 `OUT_DIR/generated.rs` 同步到 `x86_64_unknown_linux_gnu.rs`（流程未在仓库文档写明，`inferred`）。  
4. 修改 `ibverbs.rs` 当上游 inline 行为或版本宏变化时。

---

## 4. 分层与公开 API

### 4.1 `bindings`（crate 内 FFI 层）

- `pub mod bindings`：对外 **公开**（`lib.rs`），含 `generated` + `ibverbs`（`confirmed`）。
- 依赖 `libc`、`nugine-rust-utils::offset_of`；大量 `#[repr(C)]` 类型与 `extern "C"` 函数。
- **非** 独立 `-sys` crate；消费者可直接 `rdma::bindings::ibv_*`（`inferred`：偏库作者场景，一般用户用 safe 模块）。

### 4.2 Safe 层模块树

| 模块 | 职责 |
|------|------|
| `device` | `DeviceList`、`Device`、`Guid`、`Gid`/`GidEntry`、`PortAttr`、`DeviceAttr` |
| `ctx` | `Context` — `ibv_open_device` / `ibv_close_device` |
| `pd` | `ProtectionDomain` |
| `mr` | `MemoryRegion<T>` — `register` 为 **unsafe**（内存有效性契约） |
| `cq` / `cc` | `CompletionQueue`、`CompChannel`；CQ 在 `cq_context` 存自引用 |
| `qp` / `srq` | `QueuePair`、`SharedReceiveQueue`；`post_send`/`post_recv` **unsafe** |
| `wr` / `wc` | `SendRequest`、`RecvRequest`、`Sge`、`WorkCompletion` |
| `ah` | 地址句柄选项 |
| `mw` / `dm` | Memory window、device memory |
| `error` | `io::Error` + errno（`create_resource`、`from_errno`） |

**设计共性：** 公开类型多为 `#[derive(Clone)]` 包装 `Arc<Owner>`；`Owner` 在 `Drop` 中调用对应 `ibv_*` 销毁；子资源持有父资源克隆（如 `ProtectionDomain` 持 `Context`）以保证析构顺序（`inferred`）。

### 4.3 上层适配：`examples/rdma-async`

| 项 | 内容 |
|----|------|
| **做了什么** | 全局 `RdmaDriver`（Context/PD/CompChannel/CQ）；独立线程 `wait_cq_event` + `poll`；`Work<T>` Future 对接 `post_send`/`post_recv` 完成；`Buf` 注册 MR；`RdmaListener`/`RdmaConnection` 用 **Tokio TCP** 交换 QP 元数据后走 RDMA |
| **为什么** | README 写明仍在探索安全内存管理；示例展示如何把 **同步 verbs + 阻塞 CQ 事件** 接到 **async runtime**（`confirmed` README「Memory Management」） |
| **怎么做** | 依赖 path `rdma`（`serde`）；`work.rs` 用 `Waker`/`Mutex` 状态机；`driver.rs` `thread::spawn` 轮询 CQ；`net.rs` `TcpListener`/`TcpStream` + `bincode` 序列化 `Dest`；`rdma-async-rpc` bin 为 `#![forbid(unsafe_code)]` 仅调库 API |

**未** 使用 `librdmacm` 的 `rdma_connect` 等；与 pingpong 一样用 TCP 做带外协商（`confirmed`）。

---

## 5. 资源与生命周期

### 5.1 资源层次（与 `docs/ref/ref.svg` 一致，`confirmed` README）

```
DeviceList → Device → Context
Context → ProtectionDomain → MemoryRegion / QueuePair / CQ / CompChannel / …
CompChannel ↔ CompletionQueue（WeakSet 弱引用）
QueuePair → 持有 PD、Send/Recv CQ、可选 SRQ
```

### 5.2 典型资源对

| 资源 | 创建（safe） | 释放 | Rust 封装 |
|------|--------------|------|-----------|
| 设备列表 | `DeviceList::available` | `Drop` → `ibv_free_device_list` | `DeviceList` |
| 上下文 | `Context::open` | `Owner::drop` → `ibv_close_device` | `Arc<Owner>` |
| PD | `ProtectionDomain::alloc` | `ibv_dealloc_pd` | 持 `_ctx: Context` |
| MR | `MemoryRegion::register` (unsafe) | `ibv_dereg_mr` | 持 `_pd` |
| CQ | `CompletionQueue::create` | `ibv_ack_cq_events` + `ibv_destroy_cq` | `cq_context` 指向 `Owner` |
| CC | `CompChannel::create` | `ibv_destroy_comp_channel` | `WeakSet` 跟踪关联 CQ |
| QP | `QueuePair::create` | `ibv_destroy_qp` | 持 PD/CQ 克隆 |

### 5.3 特殊机制

- **CQ 自引用**：创建后将 `cq_context` 设为 `Owner` 指针，`from_cq_context` 用 `Weak::from_raw` / `upgrade` 还原 `CompletionQueue`（`cq.rs`，`inferred`：支撑 `ibv_get_cq_event` 回调路径）。
- **CompChannel::WeakSet**：CQ 创建时 `add_cq_ref`；CQ drop 时 `del_cq_ref` 并 `Weak::from_raw` 释放（`cc.rs`、`weakset.rs`）。
- **DeviceList**：`scopeguard::guard_on_unwind` 保证 panic 时仍 `ibv_free_device_list`（`device_list.rs`）。
- **README**：「All APIs related with raw memory are unsafe」「resources are managed by reference counting」（`confirmed`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

- 统一 **`std::io::Error`**：`create_resource` 在指针为 null 时读 `errno`（`__errno_location`）；多数 verbs 返回负 errno 或正错误码时 `from_errno`（`error.rs`）。
- 非 errno 路径：`custom_error` 字符串（如 `ibv_get_cq_event` 失败）。

### 6.2 `unsafe` 集中处

| 区域 | 契约 |
|------|------|
| `bindings` / `ibverbs.rs` | 全部 FFI；手写 inline 需保持与 C 语义一致 |
| `MemoryRegion::register` | 调用方保证 `[addr, addr+length)` 在 deregister 前有效且初始化 |
| `QueuePair::post_send` / `post_recv` | WR 链、SGE 指向内存在完成前有效（文档 TODO） |
| `CompletionQueue::from_cq_context` | 仅 CC 事件路径；须存在 `Weak` 升级 |
| `cq::poll` | 输出 `WorkCompletion` 切片，底层为 `ibv_wc` 布局 `transmute`（`wc.rs`） |

crate 级 `#![deny(clippy::all, …)]` 但对 `unwrap`/`expect`/`panic` 有 allow（`lib.rs`）。

### 6.3 C 错误习惯

- 资源创建：失败常返回 `NULL` + **errno**（`create_resource` 先 `set_errno(0)`）。
- 多数 verbs：`0` 成功，`<0` 为负 errno（如 `ibv_poll_cq`）。
- Provider 不支持：`EOPNOTSUPP` + `NULL`（手写 `ibv_create_cq_ex` 等）。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **`Send`/`Sync`** | 各 `Owner` 普遍 `unsafe impl Send + Sync`（注释「owned type」） |
| **线程模型** | 库本身 **无** 全局驱动线程；`rdma-async` example 自管 **单线程** CQ 轮询 + Tokio 任务 |
| **async** | 主 crate **无** `async fn`；`rdma-async` 用 `Future`/`Waker` 包装完成事件 |
| **CQ 通知** | `CompChannel::wait_cq_event` 阻塞；`CompletionQueue::poll` 同步拉取 WC |
| **硬件** | 真实 RDMA 设备或 Soft-RoCE（README `rdma link add rxe`）；CI 构建 rdma-core 但未在本分析验证硬件 |

**librdmacm**：已链接并 bindgen，但未提供 Rust 侧连接抽象；examples 用 TCP 代替 CM（`confirmed`）。

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| 模块内 `#[test]` | 纯 Rust 逻辑/布局，**不** 依赖 NIC |
| **无** `tests/` 集成测试 | 硬件路径依赖手动 `just dev` / examples |
| CI | ubuntu-latest + 源码 rdma-core；`cargo clippy --all-features`（未运行） |
| Examples | §1.3；**均通过 safe `rdma` API**，不直接演示 `bindings` |
| Bench | `justfile` 调用上游 `ibv_rc_pingpong` / `ibv_ud_pingpong` 对比 |

**硬件依赖：** 运行 examples 需 RDMA 设备与 rdma-core 安装（`confirmed` README）。

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `rdma/category-2/rdma/README.md` | 版本要求、 refcount、资源图、示例说明 |
| `rdma/category-2/rdma/Cargo.toml` | workspace 成员 |
| `rdma/category-2/rdma/crates/rdma/Cargo.toml` | 依赖、docs.rs、无 `links` |
| `rdma/category-2/rdma/crates/rdma/build.rs` | pkg-config、bindgen allowlist/blocklist、docsrs 跳过 |
| `rdma/category-2/rdma/crates/rdma/src/bindings/generated.h` | C include 链 |
| `rdma/category-2/rdma/crates/rdma/src/bindings/generated.rs` | OUT_DIR vs 快照 include |
| `rdma/category-2/rdma/crates/rdma/src/bindings/ibverbs.rs` | static inline / op 手写 |
| `rdma/category-2/rdma/crates/rdma/src/lib.rs` | 模块树、`pub mod bindings` |
| `rdma/category-2/rdma/crates/rdma/src/error.rs` | errno → `io::Error` |
| `rdma/category-2/rdma/crates/rdma/src/weakset.rs` | WeakSet 实现 |
| `rdma/category-2/rdma/crates/rdma/src/cc.rs` | CompChannel + WeakSet |
| `rdma/category-2/rdma/crates/rdma/src/cq.rs` | cq_context、poll、Drop ack |
| `rdma/category-2/rdma/crates/rdma/src/mr.rs` | unsafe register |
| `rdma/category-2/rdma/examples/rdma-async/src/driver.rs` | CQ 线程模型 |
| `rdma/category-2/rdma/examples/rdma-pingpong/src/main.rs` | TCP 元数据 + verbs pingpong |
| `rdma/category-2/rdma/.github/workflows/ci.yml` | rdma-core 构建与 CI 步骤 |

---

## 10. 架构决策推断

### 10.1 单 crate 内嵌 bindings（无独立 `-sys`）

| 字段 | 内容 |
|------|------|
| **决策** | FFI 与 safe API 同包发布；`pub mod bindings` |
| **C 侧事实** | verbs 头文件巨大但 API 稳定；与 rdmacm 同装 |
| **Rust 侧事实** | 仅 `crates/rdma`；build.rs 在库 crate 内 |
| **推断动机** | 作者即维护绑定与封装；避免双 crate 版本同步；低层库目标用户少，暴露 bindings 可接受 |
| **证据** | `lib.rs`、`Cargo.toml` workspace |
| **置信度** | `inferred` |

### 10.2 bindgen + 手写 `ibverbs.rs`（blocklist 关键 inline）

| 字段 | 内容 |
|------|------|
| **决策** | bindgen 生成类型与非 inline 符号；`ibv_reg_mr`/`ibv_query_port` 等由 Rust 复刻 |
| **C 侧事实** | 大量 `static inline` 与 `verbs_context` op 表；版本间 `ibv_query_port` ABI 差异 |
| **Rust 侧事实** | `build.rs` blocklist + `ibverbs.rs` `compat` 模块调用真实 `extern "C"` 符号 |
| **推断动机** | bindgen 无法稳定导出 inline；手写可集中处理 compat 与 `EOPNOTSUPP` 回退 |
| **证据** | `build.rs`、`ibverbs.rs` |
| **置信度** | `inferred` |

### 10.3 `Arc<Owner>` + CQ `cq_context` + `WeakSet`

| 字段 | 内容 |
|------|------|
| **决策** | 共享所有权；CQ 与 CC 双向关联用弱引用集合 |
| **C 侧事实** | `ibv_get_cq_event` 返回 `cq` 与 `cq_context`；销毁 CQ 前须 `ibv_ack_cq_events` |
| **Rust 侧事实** | `Arc` 克隆式 handle；`WeakSet` 防止 CC→CQ 强引用环 |
| **推断动机** | Rust 无 GC；需在 C 回调指针与 Rust 对象间建立可升级弱引用；满足 README refcount 图 |
| **证据** | `cq.rs`、`cc.rs`、`weakset.rs`、`docs/ref/ref.svg` |
| **置信度** | `inferred` |

### 10.4 生成 librdmacm 但 examples 用 TCP 带外协商

| 字段 | 内容 |
|------|------|
| **决策** | bindgen 包含 `rdma.*`；safe 层与 examples 均未封装 CM |
| **C 侧事实** | CM 提供 `rdma_connect`/`rdma_listen`；与 verbs 分层 |
| **Rust 侧事实** | pingpong/async 用 `TcpStream` 交换 lid/gid/qpn 等 |
| **推断动机** | 降低首版 scope；TCP 足够演示 RC/UD；CM 绑定留待后续或上层库 |
| **证据** | `generated.h`、`examples/*/net` 或 main 中 TCP |
| **置信度** | `inferred` |

### 10.5 `io::Error` + 显式 unsafe 内存/WR

| 字段 | 内容 |
|------|------|
| **决策** | 不用自定义 `RdmaError` enum；MR/post 保持 unsafe |
| **C 侧事实** | errno 为主；MR 注册与 WR 提交无 Rust 生命周期 |
| **Rust 侧事实** | `create_resource`；README 承认 raw memory API 均 unsafe |
| **推断动机** | 与 OS 错误 interoperable；RDMA 内存语义无法在不成本下完全 safe 化 |
| **证据** | `error.rs`、`mr.rs`、`README.md` |
| **置信度** | `confirmed`（README）+ `inferred`（错误类型选择） |

### 10.6 docs.rs 提交 bindgen 快照

| 字段 | 内容 |
|------|------|
| **决策** | `build.rs` 在 docs.rs 跳过；check in `x86_64_unknown_linux_gnu.rs` |
| **C 侧事实** | docs.rs 无 libibverbs |
| **Rust 侧事实** | `cfg(docsrs)` 分支 include 快照 |
| **推断动机** | 标准 docs.rs 模式；避免构建失败 |
| **证据** | `build.rs`、`generated.rs`、`Cargo.toml` metadata |
| **置信度** | `inferred` |

---

## 11. 待澄清

- `librdmacm` 绑定已生成但 **无 safe 封装** 是否为 intentional 路线图，或永久 out of scope。
- `pub mod bindings` 的稳定性承诺（semver）未在 README 说明。
- `x86_64_unknown_linux_gnu.rs` 与本地 bindgen 输出的 **同步流程**（维护者脚本）未文档化。
- `QueuePair::post_*` 的完整 safety 文档仍为 TODO（`qp.rs`）。
- `ibv_wr_*` 新 post API 在 safe 层 **未** 封装；是否计划迁移 off legacy `ibv_send_wr`。
- 多线程共享同一 `Context`/`QueuePair` 的官方约束未在 crate 文档中写明（依赖 libibverbs 厂商文档）。
