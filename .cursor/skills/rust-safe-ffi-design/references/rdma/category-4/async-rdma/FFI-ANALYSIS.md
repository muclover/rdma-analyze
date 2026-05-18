# async-rdma — FFI 单项目分析

**分析对象（源码路径）：** `rdma/category-4/async-rdma/`  
**计划序号：** P9（阶段 B4）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`async-rdma` 是面向 **异步 RDMA 应用** 的单 crate 高级封装：在 crates.io 依赖 **`rdma-sys` 0.3.0**（独立 `-sys`，本仓库不 vendored 绑定）之上，用 **tokio 1.29.1（精确版本 pin）** 驱动 CQ 完成事件与后台任务，向应用暴露 `RdmaBuilder` / `Rdma` 及 local/remote MR 管理 API。FFI 调用集中在内部模块（`context`、`completion_queue`、`queue_pair`、`mr_allocator` 等），通过 `rdma_sys::*` 访问 libibverbs / librdmacm；本 crate 的 `build.rs` **仅** 声明链接 `ibverbs` 与 `rdmacm`，不运行 bindgen。后台 **`Agent`** 处理 MR 元数据与 SEND/RECV 控制消息；**`CQEventListener`** 用 completion channel fd + `tokio::io::unix::AsyncFd`（或手动 channel 触发）将 `ibv_poll_cq` 与调用方 future 关联。许可证 **GPL-3.0-only**。

**库画像（摘要）**

| 字段 | 内容 |
|------|------|
| **C 库** | **libibverbs**（设备、PD、MR、CQ、QP、WR/WC）；**librdmacm**（`cm` feature 下 `rdma_getaddrinfo` / `rdma_create_ep` / `rdma_connect` 等） |
| **API 形态** | Opaque handle（`ibv_context`、`ibv_qp`、`ibv_cq` 等）；WR/WC 结构体；verbs 返回 **负 errno** 或 null + errno（`confirmed` 自模块注释与 `error_utilities`） |
| **资源对** | `ibv_open_device`↔`ibv_close_device`；`ibv_alloc_pd`↔`ibv_dealloc_pd`；`ibv_reg_mr`↔`ibv_dereg_mr`；`ibv_create_cq`↔`ibv_destroy_cq`；`ibv_create_qp`↔`ibv_destroy_qp`；CM：`rdma_create_ep` 等（`cm` feature） |
| **线程安全** | libibverbs 惯例为调用方序列化 QP/CQ；crate 对 `QueuePair`、`ProtectionDomain` 等 `unsafe impl Send/Sync`（`inferred`）；CQ 轮询在独立 tokio task |
| **分发方式** | **系统库** + **crates.io `rdma-sys`**；README 要求 `libibverbs-dev`、`librdmacm-dev`、`rdma-core` 等（`confirmed`） |
| **绑定难点** | 由 **`rdma-sys`** 承担 bindgen 与 blocklist 复杂类型；本 crate 侧重 WR 组装、QP 状态机、jemalloc extent hook 与 MR 元数据协议 |

---

## 1. 仓库结构

### 1.1 Crate 与布局

| 项 | 内容 |
|----|------|
| **类型** | 单 crate（**非** workspace） |
| **包名** | `async-rdma` v0.5.0，`edition = "2021"`（`confirmed`：`Cargo.toml`） |
| **FFI 来源** | 依赖 **`rdma-sys = "0.3.0"`**（同组织 datenlord 仓库；本分析目录内 **无** `rdma-sys` 源码） |
| **上层适配 crate** | **无** 独立 tonic/quinn 等成员；本 crate 自身即为高级 async 框架 |

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `src/lib.rs` | 公开 API：`RdmaBuilder`、`Rdma`、`ConnectionType`、连接/MR/读写/send API；`cm` 条件编译 |
| `src/agent.rs` | 后台 Agent：MR 元数据、SEND/RECV 控制面、与 `RemoteMrManager` 协作 |
| `src/cq_event_listener.rs` | CQ 异步轮询、`wr_id`→channel 映射 |
| `src/completion_queue.rs` | `ibv_create_cq` / `ibv_poll_cq` / `ibv_req_notify_cq` |
| `src/queue_pair.rs` | QP 创建、状态迁移、`ibv_post_send`/`recv`、read/write/atomic |
| `src/mr_allocator.rs` | Jemalloc extent hook + `ibv_reg_mr` 或 Raw 分配策略 |
| `src/memory_region/` | `local` / `remote` / `raw` MR 抽象与 token |
| `src/rmr_manager.rs` | 远端 MR 请求超时与 `LocalMr` 租约 |
| `src/context.rs`、`device.rs` | 设备枚举、`ibv_open_device` |
| `build.rs` | `cargo:rustc-link-lib=rdmacm`、`ibverbs` |
| `doc/cq_event_listener_arch.md` | CQ 监听器设计（Tokio `AsyncFd`） |
| `rdma-env-setup/`（git submodule） | CI 软 RoCE / siw 环境脚本（`confirmed`：`.gitmodules`） |
| `.github/workflows/ci.yml` | Rust 1.63、clippy/fmt、软 RDMA 集成测试 |

### 1.3 Examples（简单阅读）

| Example | 入口 | FFI 相关要点 |
|---------|------|----------------|
| `client` / `server` | `examples/client.rs`、`server.rs` | 默认 `RdmaBuilder` + TCP `connect`/`listen`（`RCSocket`）；演示 read/write/send、MR 交换 |
| `cm_client` / `cm_server` | `examples/cm_*.rs` | 需 **`cm` + `raw`**；`ConnectionType::RCCM`、`send_raw`/`receive_raw`（绕过 Agent） |
| `rpc` | `examples/rpc.rs` | 高阶 API + `async-bincode` 序列化请求；README 推荐入门 |
| `devices` | `examples/devices.rs` | 仅 `DeviceList::available()`，展示设备枚举 FFI 封装 |

**结论：** examples 均通过 **`Rdma` / `RdmaBuilder`** 使用库，不直接调用 `rdma_sys`；`devices` 为最薄设备层演示；`cm_*` 展示 CM + raw 路径。

### 1.4 Tests

| 文件 | 说明 |
|------|------|
| `tests/device.rs` | 设备列表 |
| `tests/loop.rs`、`imm.rs`、`atomic.rs` | 数据路径与 immediate/atomic |
| `tests/mr_slice.rs`、`rmr_access.rs`、`rmr_timeout.rs` | MR 切片与远端 MR 权限/超时 |
| `tests/cancel_safety.rs` | 取消安全（`cancel_safety_test` feature） |
| `tests/time.rs` | 计时相关（部分 `#[test]` 注释掉） |
| `tests/test_utilities.rs` | client/server 测试辅助 |

集成测试依赖 **真实 RDMA 环境**（CI 通过 submodule `rdma-env-setup` + `scripts/run.sh`，`confirmed`）。

---

## 2. 原生依赖

### 2.1 库画像

见 §0 表。verbs 数据路径与 rdma_cm 连接路径在本 crate 中通过 **`ConnectionType`** 分支选择（§4）。

### 2.2 `links` 与链接

| 层级 | 行为 |
|------|------|
| **`async-rdma`** | **无** `package.links`；`build.rs` 仅 `println!("cargo:rustc-link-lib=rdmacm")` 与 `ibverbs`（`confirmed`） |
| **`rdma-sys`（依赖）** | 其 `build.rs` 使用 **pkg-config** 探测 `libibverbs`、`librdmacm` 并 **bindgen** 生成 `OUT_DIR/bindings.rs`（本分析 **不** 展开 `rdma-sys` 与 async-rdma 的逐符号对比） |
| **运行时** | 需系统安装 rdma-core 与用户态 provider（README：rxe/siw 配置说明，`confirmed`） |

### 2.3 主要 Rust 依赖（与 FFI 相关）

| Crate | 用途 |
|-------|------|
| `rdma-sys` | 全部 `extern` 类型与函数 |
| `libc` | `fcntl` O_NONBLOCK 等 |
| `tikv-jemalloc-sys` | MR 池化与 `extent_hooks` |
| `tokio` | 异步运行时、task、mpsc、`AsyncFd` |
| `async-bincode` / `bincode` / `serde` | Agent 控制消息与 MR token 序列化 |
| `parking_lot`、`lockfree-cuckoohash` | 并发 map、锁 |
| `thiserror` | CQ 等局部错误枚举 |
| `errno` | 辅助（与 `io::Error` 并存） |

### 2.4 Feature 矩阵

| Feature | 作用 |
|---------|------|
| **`cm`** | 启用 `rdma_getaddrinfo`、`rdma_create_ep`、`rdma_connect` 等；`RdmaBuilder::cm_connect` / `cm_listen` |
| **`raw`** | `send_raw` / `receive_raw` 等，不经 Agent 编解码 |
| **`exp`** | 实验性 API，如 `receive_sge_fn`（在 post recv 与 poll 之间插入闭包） |
| **`cancel_safety_test`** | CQ 监听器取消测试用延迟（仅测试） |
| **默认** | 无 default feature；`cm_client` example 声明 `required-features = ["cm", "raw"]` |

---

## 3. 绑定生成

### 3.1 本 crate：**不生成** FFI

`async-rdma` **无** bindgen、无 `wrapper.h`、无检入 `bindings.rs`。所有 C 符号经依赖 **`rdma-sys`** 引入。

### 3.2 依赖 `rdma-sys` 的绑定方式（概要，不 deep dive）

| 项 | 内容 |
|----|------|
| **工具** | **bindgen** 0.59 + **pkg-config**（`rdma-sys/build.rs`，同 category-4 样本树中的 P8 仓库） |
| **头文件** | `rdma-sys/src/bindings.h` → `infiniband/verbs.h`、`rdma/rdma_cma.h` 等 |
| **策略** | `allowlist` `ibv_*` / `rdma_*`；**blocklist** `ibv_send_wr`、`ibv_wc`、`ibv_ah_attr` 等（由本 crate 用 `zeroed` + 字段填充）；大量 **bitfield_enum** |
| **产物** | 构建时写入 `OUT_DIR/bindings.rs`（非检入 async-rdma 树） |
| **再生** | 在 `rdma-sys` 仓库内 `cargo build` 触发（本分析未构建） |

### 3.3 C 头暴露的主要 API 形态（经 `rdma-sys` + 本 crate 用法）

| 类别 | 形态 | async-rdma 中的用法 |
|------|------|---------------------|
| 设备 | `ibv_get_device_list`、`ibv_open_device` | `device.rs`、`context.rs` |
| 资源树 | context → PD → MR/QP/CQ | `Context`、`ProtectionDomain`、`MrAllocator`、`QueuePair` |
| 数据路径 | `ibv_post_send`/`recv`、`ibv_poll_cq`、`ibv_req_notify_cq` | `queue_pair.rs`、`completion_queue.rs`、`cq_event_listener.rs` |
| QP 状态 | `ibv_modify_qp`、`INIT→RTR→RTS` | `queue_pair.rs` |
| 连接（可选） | `rdma_*` CM API | `lib.rs` 中 `#[cfg(feature = "cm")]` |
| 错误习惯 | 多数 verbs：**负 errno**；失败时 `io::Error::last_os_error()`（`error_utilities.rs`） | 统一 `io::Result` |

### 3.4 本 crate `build.rs` 角色

仅补充链接器库名，**不**探测头文件版本；与 `rdma-sys` 的 pkg-config 职责分离（`inferred`：避免重复探测，依赖传递链接仍依赖 `rdma-sys` 构建）。

---

## 4. 分层与公开 API

### 4.1 分层总览

```
应用 / examples
    ↓  RdmaBuilder → Rdma（read/write/send/MR API）
    ↓  Agent（控制面 SEND/RECV + bincode）  |  raw/cm 路径绕过 Agent
    ↓  QueuePair + CQEventListener + CompletionQueue
    ↓  Context / ProtectionDomain / MrAllocator / memory_region
    ↓  rdma-sys（bindgen FFI）
    ↓  libibverbs + librdmacm（系统）
```

### 4.2 公开模块与类型（API 目录级）

| 模块/类型 | 职责 |
|-----------|------|
| **`RdmaBuilder`** | Builder 模式：设备名、CQ/QP/MR/Agent 参数；`connect`/`listen`/`ibv_connect`/`cm_*` |
| **`Rdma`** | 用户唯一主句柄：`alloc_local_mr`、`request_remote_mr`、`read`/`write`/`send`/`receive_*`、`send_remote_mr` 等 |
| **`ConnectionType`** | `RCSocket`（TCP 交换 QP 元数据）、`RCCM`（rdma_cm）、`RCIBV`（手动 `QueuePairEndpoint`） |
| **`device`** | `DeviceList` 公开枚举设备 |
| **`LocalMr*` / `RemoteMr*` trait** | 安全切片与访问权限抽象 |
| **`AccessFlag`、`MrToken`、`MRManageStrategy`** | 访问权限与远端 MR 元数据；Jemalloc vs Raw 分配 |
| **`PollingTriggerType`、`ManualTrigger`** | CQ 自动 `AsyncFd` vs 手动触发 |
| **`IbvEventType`** | 异步设备事件（`ibv_event_listener`） |

内部未公开但关键：`Agent`、`CQEventListener`、`QueuePair`、`MrAllocator`、`RemoteMrManager`。

### 4.3 连接建立（三种路径）

| 类型 | 机制 | Agent |
|------|------|-------|
| **`RCSocket`**（默认） | `tokio::net::TcpStream` 交换 `QueuePairEndpoint`（GID、QPN、PSN 等），再 `qp_handshake` | `connect`/`listen` 后 `init_agent` |
| **`RCCM`** | `rdma_getaddrinfo` + `rdma_create_ep` + `rdma_connect` / listen 变体（`cm` feature） | `set_raw(true)` 时常关闭 Agent |
| **`RCIBV`** | 调用方提供对端 QP 信息，`ibv_modify_qp` 直接建链 | 同 Socket 路径 |

### 4.4 上层适配 crate

**N/A** — 无独立 gRPC/QUIC 等适配成员；`async-rdma` 本身定位为 **高级 async 框架**，非 `-sys` 亦非薄 safe 层。

### 4.5 Agent 与 event_listener（框架核心）

| 组件 | 做了什么 | 与 FFI 的关系 |
|------|----------|----------------|
| **`Agent`** | tokio 后台任务：解析对端 SEND 的 MR token/控制消息；维护 `RemoteMrManager`；向 API 层 mpsc 投递 `LocalMr`/`RemoteMr`/数据/imm | 底层仍调用 `QueuePair::post_send`/`post_recv`（verbs） |
| **`CQEventListener`** | 独立 poller task：`ibv_req_notify_cq` + `AsyncFd` 或 channel；`wr_id` 映射到 `mpsc::Sender<WorkCompletion>` | 封装 `ibv_poll_cq` 与完成通道 fd |
| **`IbvEventListener`** | 监听 `ibv_context` 异步事件（设备级） | `ibv_get_async_event` 等 |

设计文档：`doc/cq_event_listener_arch.md` 说明选用 Tokio `AsyncFd` 而非自管 epoll（`confirmed`）。

---

## 5. 资源与生命周期

### 5.1 资源层次

```
DeviceList → Context (Arc)
              ├── CompletionQueue + EventChannel
              ├── ProtectionDomain (Arc)
              │     ├── MrAllocator → LocalMr (reg_mr / jemalloc extent)
              │     └── QueuePair (Arc)
              │           └── CQEventListener (poller JoinHandle)
              ├── Agent (可选, Arc) → RemoteMrManager
              └── IbvEventListener
```

### 5.2 Rust 封装要点

| 资源 | C 释放 | Rust |
|------|--------|------|
| 设备列表 | `ibv_free_device_list` | `DeviceList` Drop |
| Context | `ibv_close_device` | `Context` Drop |
| PD | `ibv_dealloc_pd` | `ProtectionDomain` Drop |
| CQ | `ibv_destroy_cq` | `CompletionQueue` Drop |
| QP | `ibv_destroy_qp` | `QueuePair` Drop |
| MR | `ibv_dereg_mr` | `LocalMr` / jemalloc hook `extent_dalloc_hook` |
| CM ID | `rdma_destroy_id` 等 | `cm` 路径中连接失败时 `rdma_disconnect`（`lib.rs`） |

- **`Arc` 广泛持有**：`Rdma` 持有 `ctx`、`pd`、`qp`、`allocator`、`agent`（`confirmed`：`lib.rs` 字段）。
- **`CQEventListener::drop`**：abort poller task（`confirmed`）。
- **`Agent::drop`**：abort 多个 JoinHandle（`confirmed`）。
- **远端 MR 超时**：`RemoteMrManager` 定时回收 `MrToken` 对应 `LocalMr`（`rmr_manager.rs`）。
- **并发使用 MR**：`CQEventListener::register` 在 RDMA 操作期间 pin `LocalMrInner`（`LmrGuards`），防止 dereg（`confirmed` 注释）。

### 5.3 风险点

- MR 底层内存由 jemalloc arena 或 `alloc` 分配，须在 `ibv_dereg_mr` 前保持有效（`inferred`）。
- `lkey_unchecked` 等 **unsafe** 访问在热路径使用（`work_request.rs` 有 FIXME，`confirmed`）。
- QP/CQ 销毁顺序依赖 `Arc` 析构顺序；复杂并发下需依赖 guards 与单 poller（`inferred`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

| 层面 | 类型 | 说明 |
|------|------|------|
| **主 API** | `std::io::Result` / `io::Error` | 多数公开方法 |
| **辅助** | `thiserror`（如 `completion_queue` 内 WC 状态） | 局部转换后并入 `io::Error` |
| **日志** | `error_utilities::{log_ret_last_os_err, log_ret}` | tracing 记录 errno |

**无** 独立 crate 级 `Error` 枚举暴露给应用（`inferred`：简化 API，损失结构化错误）。

### 6.2 `unsafe` 集中处

| 模块 | 说明 |
|------|------|
| 全库 | 凡 `rdma_sys` / `libc` 调用均在 `unsafe` 块 |
| `mr_allocator.rs` | `extern "C"` jemalloc extent hooks；`ibv_reg_mr` 于 alloc hook |
| `work_request.rs` | `zeroed` 初始化 `ibv_send_wr` / `ibv_recv_wr` |
| `queue_pair.rs` | `ibv_modify_qp`、`ibv_post_*` |
| `memory_region/local.rs` | MR 指针与 lkey 访问 |
| `lib.rs` | CM 连接、`#[cfg(feature = "cm")]` 路径 |

crate 级 `#![deny(warnings)]` 且 **未** `forbid(unsafe_code)`（`lib.rs` 中 `unsafe_code` 注释掉，`confirmed`）。

### 6.3 安全契约（调用方）

- 遵循 RC QP 状态机；握手完成后再 post 数据 WR。
- `RemoteMr` / `MrToken` 在 `ddl` 之后可能失效（`MrToken` 含 deadline 字段）。
- **raw 模式**：调用方自行保证缓冲区与对端协议一致。
- **Cancel**：`cancel_safety_test` 表明项目关注 future 取消与 CQ 等待的交互（细节见测试，未静态验证）。

### 6.4 C 错误习惯

- ibverbs 风格：**返回负 errno** 或 null pointer + `errno`（模块注释与 `log_ret_last_os_err`，`confirmed`）。
- 本 crate 统一映射为 `io::Error`，不区分 `Verbs` vs `Cm` 子类型（`inferred`）。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **`Send`/`Sync`** | `QueuePair`、`ProtectionDomain` 等显式 `unsafe impl Send/Sync`（`queue_pair.rs`、`protection_domain.rs`） |
| **运行时** | **tokio** `=1.29.1` features `full`（精确 pin，`confirmed`） |
| **CQ 模型** | 专用 **`CQEventListener`** poller；`AsyncFd` 读 completion channel；超时后仍 `poll_cq` 防空转（`cq_event_listener.rs` 注释） |
| **触发模式** | `PollingTriggerType::Automatic`（默认）或 `Manual` + `mpsc` |
| **Agent** | 多 `tokio::spawn`；`parking_lot::Mutex` + `tokio::sync::mpsc` 向 API 交付结果 |
| **数据 API** | `async fn`：`read`/`write`/`send`/… 注册 `wr_id` 后 `recv().await` |
| **设备事件** | `IbvEventListener` 独立监听（与 CQ 路径并行） |

**无** `async-std` / 可选 runtime 抽象（`inferred`：强绑定 tokio 1.29.1）。

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| **硬件/环境** | 需 RDMA 设备（CI：软 RoCE/siw，`rdma-env-setup` submodule + `scripts/run.sh`，`confirmed`） |
| **工具链** | CI 固定 **Rust 1.63.0**（`confirmed`：`.github/workflows/ci.yml`） |
| **单元/集成** | `tests/*.rs` 覆盖 MR、atomic、imm、loop、cancel；多通过 `RdmaBuilder` 建链 |
| **Clippy** | `--all-features --all-targets -D warnings` |
| **Fuzz** | 无 |
| **Examples** | 见 §1.3；`rpc` 为 README 推荐；`cm_*` 需 feature；**均间接演示 FFI**，不 teach 裸 `rdma_sys` |

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `async-rdma/Cargo.toml` | `rdma-sys` 依赖、tokio pin、features、GPL-3.0、examples 声明 |
| `async-rdma/build.rs` | 仅 link `rdmacm`、`ibverbs` |
| `async-rdma/README.md` | 组件说明、环境依赖、rxe 配置、rpc 示例 |
| `async-rdma/src/lib.rs` | `Rdma`/`RdmaBuilder`、`ConnectionType`、模块树、crate 文档示例 |
| `async-rdma/src/agent.rs` | Agent 结构、mpsc、后台任务 |
| `async-rdma/src/cq_event_listener.rs` | AsyncFd、wr_id 映射、poller |
| `async-rdma/doc/cq_event_listener_arch.md` | Tokio 选型与流程图 |
| `async-rdma/src/queue_pair.rs` | post_send/recv、modify_qp、Send/Sync |
| `async-rdma/src/mr_allocator.rs` | jemalloc extent hooks、`MRManageStrategy` |
| `async-rdma/src/completion_queue.rs` | create_cq、poll、notify |
| `async-rdma/src/error_utilities.rs` | errno → `io::Error` |
| `async-rdma/.github/workflows/ci.yml` | CI 环境、Rust 1.63、clippy |
| `rdma/category-4/rdma-sys/build.rs` | bindgen + pkg-config（依赖绑定概要，非对比） |
| `async-rdma/examples/cm_client.rs` | `RCCM` + `raw` 用法 |

---

## 10. 架构决策推断

### 10.1 依赖独立 `rdma-sys`，本 crate 只做高级封装

| 字段 | 内容 |
|------|------|
| **决策** | FFI 全部由 **`rdma-sys`** 提供；`async-rdma` 不内嵌 bindgen |
| **C 侧事实** | verbs + rdmacm 头文件庞大、类型多、部分结构需 blocklist |
| **Rust 侧事实** | `Cargo.toml` 仅依赖 `rdma-sys`；`build.rs` 两行 link |
| **推断动机** | 绑定维护与 async API 解耦；与 datenlord 生态其他项目复用同一 `-sys` |
| **证据** | `Cargo.toml`、`README` Related Projects |
| **置信度** | `confirmed`（README 直述 `rdma-sys`） |

### 10.2 `CQEventListener` + `wr_id` channel 实现 async verbs

| 字段 | 内容 |
|------|------|
| **决策** | 单 poller task + `HashMap<WorkRequestId, Responder>` + tokio `AsyncFd` |
| **C 侧事实** | `ibv_wc.wr_id` 与提交 WR 对应；completion channel 为 fd |
| **Rust 侧事实** | `register` 返回 `Receiver<WorkCompletion>`；`doc/cq_event_listener_arch.md` |
| **推断动机** | 避免每个 API busy-poll；与 tokio 生态一致 |
| **证据** | `cq_event_listener.rs`、`doc/cq_event_listener_arch.md` |
| **置信度** | `confirmed`（设计 doc） |

### 10.3 `Agent` 控制面 + bincode 实现远端 MR 协商

| 字段 | 内容 |
|------|------|
| **决策** | 默认启用 Agent，经 SEND/RECV 交换 `MrToken` 与元数据 |
| **C 侧事实** | verbs 不提供服务端「申请远端 MR」语义，需应用层协议 |
| **Rust 侧事实** | `request_remote_mr`、`send_remote_mr`、`RemoteMrManager` |
| **推断动机** | 将 RDMA 内存语义封装为类 RPC 的 async API，降低应用编写 CM/MR 协议的成本 |
| **证据** | `agent.rs`、`lib.rs` 模块文档 |
| **置信度** | `inferred` |

### 10.4 Jemalloc extent hook 合并分配与 `ibv_reg_mr`

| 字段 | 内容 |
|------|------|
| **决策** | 默认 `MRManageStrategy::Jemalloc`，alloc hook 内 `ibv_reg_mr` |
| **C 侧事实** | MR 注册需连续虚拟地址与 access flags |
| **Rust 侧事实** | `tikv-jemalloc-sys`、`RDMA_EXTENT_HOOKS`、`AccessPDArenaMap` |
| **推断动机** | 减少频繁 reg/dereg；按 PD/access 分 arena |
| **证据** | `mr_allocator.rs` |
| **置信度** | `inferred` |

### 10.5 默认 TCP Socket 交换 QP 元数据（`RCSocket`）

| 字段 | 内容 |
|------|------|
| **决策** | `connect`/`listen` 走 `TcpStream` + `QueuePairEndpoint`，非默认 CM |
| **C 侧事实** | RC QP 需交换 GID、QPN、PSN 等；CM 可替代但非唯一 |
| **Rust 侧事实** | `ConnectionType::RCSocket` 为 builder 默认；`cm` 为可选 feature |
| **推断动机** | 降低对 rdma_cm 的硬依赖；示例与测试更易在 loopback + rxe 上跑通 |
| **证据** | `lib.rs` `QPInitAttr` 默认、`examples/client.rs` |
| **置信度** | `inferred` |

### 10.6 `raw` / `cm` feature 提供无 Agent 与 CM 建链

| 字段 | 内容 |
|------|------|
| **决策** | `set_raw(true)` 跳过 Agent；`RCCM` + `cm_*` API |
| **Rust 侧事实** | `#[cfg(feature = "raw")]`、`#[cfg(feature = "cm")]` 分路径 |
| **推断动机** | 高级用户直接 post verbs；与 librdmacm 原生工作流对齐 |
| **证据** | `examples/cm_client.rs`、`Cargo.toml` features |
| **置信度** | `confirmed`（example 与 feature 名） |

---

## 11. 待澄清

- `rdma-sys` 0.3.0 与系统 rdma-core 版本不匹配时的行为（仅见 `rdma-sys` pkg-config 最低版本，未在本目录验证）。
- `tokio = "=1.29.1"` 精确 pin 是否与未来 Rust edition/依赖冲突（静态未测）。
- GPL-3.0-only 对下游专有项目链接的影响（法律层面超出技术分析）。
- `exp` feature API 稳定性与生产可用性（代码标注 experimental）。
- CI submodule `rdma-env-setup` 在离线克隆时是否必需（`.gitmodules` 存在，本分析未跑 CI）。
- `lib.rs` 体积极大（单文件数千行），模块边界长期维护成本（`inferred` 组织问题）。
