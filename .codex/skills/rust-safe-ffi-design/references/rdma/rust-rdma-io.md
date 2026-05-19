# rust-rdma-io — FFI 单项目分析

**分析对象（源码路径）：** `rdma/category-3/rust-rdma-io/`  
**计划序号：** P7（阶段 B3）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`rust-rdma-io` 为 **libibverbs** 与 **librdmacm**（rdma-core）提供 Rust 绑定与安全封装，面向需要 RDMA 数据路径、异步 I/O 以及 **tonic gRPC** / **Quinn QUIC** 集成的应用开发者。架构为 **六层 workspace**：`rdma-io-sys`（bnd 生成 FFI + `wrapper.c` 导出约 96 个 inline 包装符号）→ `rdma-io`（`Arc` RAII、tokio/epoll 异步 CQ、`Transport` trait 及 SendRecv / CreditRing / ReadRing 三种传输）→ 上层适配 `rdma-io-tonic`（`RdmaConnector` / `RdmaIncoming`）与 `rdma-io-quinn`（`RdmaUdpSocket` 实现 `AsyncUdpSocket`）。绑定生成采用 **bnd + WinMD**，非 bindgen；生成代码检入仓库，编译期不依赖系统头文件解析。

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| `rdma-io-sys` | `rdma-io-sys/` | 底层 FFI：`ibverbs`、`rdmacm`、`wrapper` 模块 |
| `rdma-io` | `rdma-io/` | Safe API、异步、Transport trait、三种传输实现 |
| `rdma-io-tonic` | `rdma-io-tonic/` | tonic gRPC 传输适配（connector / incoming / 可选 TLS） |
| `rdma-io-quinn` | `rdma-io-quinn/` | Quinn `AsyncUdpSocket` 适配 |
| `bnd-rdma-gen` | `bnd-rdma-gen/` | 开发用绑定生成器（读 `rdma.toml`，写 `rdma-io-sys/src/rdma/`） |
| `rdma-io-tests` | `rdma-io-tests/` | 集成测试（sys、safe、async、transport、tonic、quinn、tonic-h3） |
| `rdma-io-bench` | `tests/rdma-io-bench/` | 性能基准（非 FFI 核心，§1 列出即可） |

根 `Cargo.toml` 为 workspace，`resolver = "2"`，`edition = "2024"`（`confirmed`）。

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `rdma-io-sys/src/rdma/` | bnd / windows-bindgen 生成模块（勿手改） |
| `rdma-io-sys/wrapper/` | `wrapper.h` / `wrapper.c` — inline verbs 的 C  shim |
| `rdma-io-sys/build.rs` | `cc` 编译 `wrapper.c` → 静态库 `rdma_wrapper` |
| `bnd-rdma-gen/rdma.toml` | 三分区 WinMD 提取配置（ibverbs / rdmacm / wrapper） |
| `rdma-io/src/` | device、pd、cq、qp、mr、cm、wr、wc、async_*、*_transport |
| `docs/design/` | SafeApi、BndBindings、transport、quinn-rdma 等设计文档 |
| `scripts/setup-siw.sh`、`setup-rxe.sh` | 软件 RDMA 设备（测试用） |
| `.github/workflows/ci.yml` | siw + rxe 双 job 构建与测试 |

### 1.3 Examples

仓库根 **无** `examples/` 目录。用法示例分布在：

- 根 `README.md`（async stream、tonic、quinn、tonic-h3）
- 各 crate 的 `lib.rs` / 模块 `//!` 文档示例
- `rdma-io-tests` 集成测试作为 FFI 用法的主要演示载体

**简单阅读结论：** 无独立 example crate；FFI 演示以 README doctest 与 `rdma-io-tests` 为主，不逐文件深读业务逻辑。

### 1.4 Tests

| 类型 | 位置 | 说明 |
|------|------|------|
| 原始 FFI | `rdma-io-tests/tests/sys_tests.rs` | 直接 `rdma_io_sys::ibverbs`，控制路径（device/PD/CQ/MR） |
| Safe API | `safe_api_tests.rs`、`cm_tests.rs` | RAII、rdma_cm |
| Async | `async_cq_tests.rs`、`async_qp_tests.rs`、`async_stream_tests.rs` | epoll / CQ / stream |
| Transport | `transport_tests.rs`、`ring_transport_tests.rs`、`read_ring_transport_tests.rs` | 三种 Transport |
| tonic | `tonic_tests.rs`、`tonic_tls_tests.rs`、`tonic_h3_tests.rs` | gRPC / TLS / HTTP3 栈 |
| Quinn | `quinn_tests.rs` | `RdmaUdpSocket` |
| Bench | `tests/rdma-io-bench/` | 性能，非本分析重点 |
| E2E / infra | `tests/e2e/`、`tests/infra-local/` | Ansible/Terraform 远程测试（§8 一句带过） |

---

## 2. 原生依赖

### 2.1 库画像

| 字段 | 内容 |
|------|------|
| **C 库** | **libibverbs**（设备、PD、MR、CQ、QP、work request/completion）；**librdmacm**（地址解析、连接管理、与 TCP 类似的 listen/connect） |
| **API 形态** | Opaque handle（`ibv_context`、`ibv_pd`、`ibv_cq`、`ibv_qp`、`rdma_cm_id` 等）；大量 struct + enum；verbs 含 legacy `ibv_post_*` 与 extended `ibv_wr_*` / `ibv_cq_ex` 两套路径；**大量 `static inline`** 函数仅存在于头文件 |
| **资源对** | `ibv_open_device` ↔ `ibv_close_device`；`ibv_alloc_pd` ↔ `ibv_dealloc_pd`；`ibv_reg_mr` ↔ `ibv_dereg_mr`；`ibv_create_qp` ↔ `ibv_destroy_qp`；`ibv_create_cq` ↔ `ibv_destroy_cq`；rdma_cm：`rdma_create_id` ↔ `rdma_destroy_id` 等 |
| **线程安全** | 单 `ibv_context` / QP 通常由调用方序列化；CQ 与 completion channel 需按 verbs 规范 arm/poll/ack；rdma_cm 事件通道为 fd 驱动（`confirmed` 自头文件与 SafeApi 设计） |
| **分发方式** | **系统库**（`libibverbs-dev`、`librdmacm-dev`、`rdma-core`）；无 vendored rdma-core 源码 |
| **绑定难点** | ~96 个 `static inline` 需 C wrapper；WinMD 分区与 POSIX/Linux 类型交叉引用；硬件/软件 provider（siw、rxe、mlx5）行为差异 |

### 2.2 `links` 与链接

- **无** `package.links` 字段（`confirmed`：全 workspace `Cargo.toml` 无 `links =`）。
- `rdma-io-sys/build.rs`：`cc` 编译 `wrapper/wrapper.c`，产出静态库 **`rdma_wrapper`**。
- 生成绑定通过 `bnd-macros` 的 `windows_link::link!` 声明：`"ibverbs"`、`"rdmacm"`、`"rdma_wrapper"`（见 `rdma-io-sys/src/rdma/ibverbs/mod.rs`）。
- 运行时依赖系统已安装 `libibverbs.so`、`librdmacm.so` 及内核/用户态 RDMA 设备。

### 2.3 传递依赖（类型）

| Crate | 用途 |
|-------|------|
| `bnd-linux` | `__be16`/`__be32`/`__be64` 等 Linux 类型 |
| `bnd-macros` | `windows_link::link!` 宏（crate 内别名为 `windows_link`） |

`rdma-io` 另依赖 `libc`、`bitflags`、`thiserror`、`tracing`；可选 `tokio` / `futures-io`。

### 2.4 Feature 矩阵（要点）

| Crate | Feature | 作用 |
|-------|---------|------|
| `rdma-io-sys` | `ibverbs`、`rdmacm`、`wrapper`（均 default） | 模块开关 |
| `rdma-io` | `tokio`（default）、`async` | tokio `AsyncFd` CQ 通知 vs 仅 futures |
| `rdma-io-tonic` | `tls` | `tonic-tls` + OpenSSL 包装 |
| `rdma-io-quinn` | — | 依赖 workspace `quinn`（`runtime-tokio`） |

---

## 3. 绑定生成

### 3.1 方式：**bnd + WinMD**，非 bindgen

流水线（`docs/design/BndBindings.md`，`confirmed`）：

```
C 头文件 → libclang → bnd-winmd → bnd-rdma.winmd → windows-bindgen → rdma-io-sys/src/rdma/
```

- 配置：`bnd-rdma-gen/rdma.toml` 三个 **partition**：
  1. `rdma.ibverbs` — `infiniband/verbs.h` 等
  2. `rdma.rdmacm` — `rdma/rdma_cma.h`
  3. `rdma.wrapper` — `rdma-io-sys/wrapper/wrapper.h`（仅 `rdma_wrap_*` 符号）
- 再生：`cargo run -p bnd-rdma-gen`（`confirmed` README / `rdma-io-sys/README.md`）
- CI：`cmake -B build` 下载 `bnd-linux.winmd` 供类型导入（`.github/workflows/ci.yml`）

### 3.2 Inline 函数与 `wrapper.c`

bnd **跳过** `static inline`（与 bindgen 类似）。`wrapper.h` 声明 **96** 个 `rdma_wrap_*` 函数（README 写「~93」为约数）；`wrapper.c` 内联调用真实 `ibv_*` / `ibv_wr_*` / `ibv_start_poll` 等，导出为可链接符号，由 partition 3 绑定到 `rdma.wrapper` 模块。

`wrapper.h` 通过 `#include <infiniband/verbs.h>` 接触完整 verbs 表面（§4.4）。

### 3.3 C 头暴露的主要 API 形态

自 `infiniband/verbs.h`（经 `wrapper.h`）与 `rdma/rdma_cma.h`：

| 类别 | 形态 | Rust 侧含义 |
|------|------|-------------|
| 设备枚举 | `ibv_get_device_list` / `ibv_open_device` | 进程级设备列表与 context |
| 资源树 | PD → MR/QP/MW；Context → CQ/SRQ | 必须按序销毁，否则 UB |
| 数据路径（legacy） | `ibv_post_send` / `ibv_post_recv`、`ibv_poll_cq` | 经 `rdma_wrap_*` 链接 |
| 数据路径（extended） | `ibv_wr_*`、`ibv_start_poll`、`ibv_wc_read_*` | 同上，wrapper 覆盖 |
| 异步通知 | `ibv_req_notify_cq` + completion channel fd | `AsyncCq` + epoll（tokio `AsyncFd`） |
| 连接管理 | `rdma_create_ep`、`rdma_listen`、`rdma_connect`、事件 `rdma_get_cm_event` | `cm` / `async_cm` 模块 |
| 错误习惯 | ibverbs：负 errno 返回值；rdma_cm：`-1` + `errno`（`rdma-io/src/error.rs` 双路径转换） |

### 3.4 生成规模（文档记载）

| Partition | Structs | Enums | Functions |
|-----------|---------|-------|-----------|
| ibverbs | 275 | 92 | 164 |
| rdmacm | 19 | 4 | 40 |
| wrapper | 0 | 0 | 96 |

（来源 `docs/design/BndBindings.md`，`confirmed`）

---

## 4. 分层与公开 API

### 4.1 `rdma-io-sys`

- 薄 FFI：`pub mod rdma` → `ibverbs`、`rdmacm`、`wrapper`。
- 类型多为 generated struct/enum + `windows_link::link!` 声明。
- **无意图供应用直接使用**（README 明确，`confirmed`）。

### 4.2 `rdma-io`（核心 safe）

| 模块 | 职责 |
|------|------|
| `device` | 设备枚举、`Context`（`Arc` 持有） |
| `pd`、`mr`、`mw` | 保护域、内存注册、memory window |
| `cq`、`comp_channel`、`wc` | 完成队列、完成通道、work completion |
| `qp`、`wr` | 队列对、work request 构建 |
| `cm` | rdma_cm 同步 API |
| `error` | `Error` / `Result`，C 返回码映射 |
| `async_cq`、`async_qp` | CQ fd + drain-after-arm 异步轮询 |
| `async_cm` | 异步连接监听/建立 |
| `transport`、`transport_common` | `Transport` / `TransportBuilder` trait |
| `send_recv_transport` | Send/Recv 双边语义 |
| `credit_ring_transport` | RDMA Write + 立即数信用环 |
| `read_ring_transport` | Write 环 + Read 流控 |
| `async_stream` | `AsyncRead`/`AsyncWrite`（futures + 可选 tokio compat） |
| `tokio_notifier` | `CqNotifier` 的 tokio `AsyncFd` 实现 |

**所有权：** 子资源持 `Arc<Parent>`，Drop 时调用对应 `ibv_destroy_*` / `ibv_dealloc_*`（`docs/design/SafeApi.md`，`confirmed`）。

**Transport 抽象：** 消费者只见 `send_copy` / `poll_recv` / `poll_send_completion`，不直接碰 WC/QP（`transport.rs` 模块文档）。

### 4.3 `rdma-io-tonic`（上层适配 — gRPC）

#### 做了什么

- **`RdmaConnector<B>`**：实现 `tower_service::Service<Uri>`，供 `tonic::transport::Endpoint::connect_with_connector` 建立 RDMA 连接并返回 `TokioIo<TokioRdmaStream<...>>`。
- **`RdmaIncoming<B>`**：实现 `futures_util::Stream`，产出 `TokioRdmaStream`，供 `Server::serve_with_incoming`。
- **`TokioRdmaStream`**：`AsyncRdmaStream` + `tokio_util::compat::Compat`，实现 `tokio::io::AsyncRead/AsyncWrite` 与 tonic `Connected`（`RdmaConnectInfo` 扩展）。
- **可选 `tls` feature**：`tls::RdmaTransport` / 与 `tonic_tls::openssl` 组合，在 RDMA 字节流上叠 TLS（`rdma-io-tonic/src/tls.rs`）。

#### 为什么要做

tonic 默认假设 **TCP + hyper**；RDMA 提供的是已连接的 **字节流**（`AsyncRdmaStream`），无 Berkeley socket。独立 crate 将 RDMA 连接工厂注入 tonic 的 **connector / incoming** 扩展点，避免修改 tonic 本体，同时保持与 `TransportBuilder` 泛型一致（SendRecv / CreditRing 可切换）。

#### 怎么做的（FFI 边界）

- **不新增 C FFI**：仅依赖 `rdma-io` 的 `TransportBuilder::connect` / `accept` 与 `AsyncCmListener`（其下为 rdma_cm + verbs）。
- **URI → SocketAddr**：`connector::uri_to_socket_addr` 解析 host/port（`inferred`：gRPC authority 映射到 RDMA CM 地址）。
- **运行时桥接**：futures `AsyncRdmaStream` → `Compat` → tokio IO → `hyper_util::rt::TokioIo`，满足 tonic/hyper 的 IO trait。
- **`unsafe` 边界**：封装在 `rdma-io` 内；tonic 层无直接 `extern "C"` 调用。

### 4.4 `rdma-io-quinn`（上层适配 — QUIC）

#### 做了什么

- **`RdmaUdpSocket<B>`**：实现 Quinn 0.11 的 **`AsyncUdpSocket`** + **`UdpPoller`**。
- 按 peer `SocketAddr` 维护 `HashMap` of `Arc<Mutex<Transport>>`；`bind` 创建 `AsyncCmListener`；`poll_recv` 内驱动 accept 与多路 `poll_recv`；`try_send` 调用 `Transport::send_copy`。
- **`connect_to`**：客户端预建 RDMA 连接（RDMA 非无连接 UDP，必须先建链）。
- **`close` / `Drop`**：断开并释放 RDMA 资源，避免仅依赖 `Arc` 析构顺序。

#### 为什么要做

Quinn 将 **UDP datagram** 抽象为 `AsyncUdpSocket`。在 RDMA 上跑 QUIC/H3 需要把「假 UDP」映射到 **点对点 RDMA 传输**（通常 `SendRecvConfig::datagram()`），从而 **不 fork Quinn** 即可复用其 TLS、拥塞控制与 HTTP/3 栈（README tonic-h3 路径）。

#### 怎么做的（FFI 边界）

- 同样 **零直接 C FFI**；通过 `Transport` trait 消费 CQ/QP 完成事件。
- **语义适配**：`try_send` 在发送缓冲满时返回 `WouldBlock`，`RdmaUdpPoller::poll_writable` 等待 send CQ（避免忙等）；`poll_recv` 将 `RecvCompletion` 填入 Quinn 的 `IoSliceMut` / `RecvMeta`。
- **内部可变性**：Quinn API 要求 `&self`，故 `Mutex`/`RwLock` 包装 transport 与 accept future（`rdma-io-quinn/src/lib.rs` 注释，`confirmed`）。
- **与 tonic-h3**：测试 crate 组合 `RdmaUdpSocket` + `tonic-h3::quinn::*`（协议细节不展开）。

### 4.5 `bnd-rdma-gen`

- 开发工具：读取 `rdma.toml`，调用 `bnd_rdma_gen::generate`，输出到 `rdma-io-sys/src/rdma/`。
- §3 绑定再生流程的一部分；应用运行时不需要。

---

## 5. 资源与生命周期

### 5.1 资源层次（C → Rust）

```
DeviceList → Device → Context (Arc)
                         ├── ProtectionDomain (Arc)
                         │    ├── MemoryRegion / OwnedMemoryRegion
                         │    ├── QueuePair (+ Arc PD, send_cq, recv_cq)
                         │    └── MemoryWindow
                         ├── CompletionQueue (+ Arc Context)
                         └── CompletionChannel
CmId / AsyncCmListener → 关联 QP、PD、CQ（transport 层组装）
```

### 5.2 Rust 封装要点

| 资源 | C 释放 | Rust |
|------|--------|------|
| 设备列表 | `ibv_free_device_list` | `DeviceList` Drop（`device.rs`） |
| Context | `ibv_close_device` | `Context` Drop |
| PD | `ibv_dealloc_pd` | `ProtectionDomain` Drop |
| MR | `ibv_dereg_mr` | `MemoryRegion` / `OwnedMemoryRegion` Drop |
| CQ | `ibv_destroy_cq` | `CompletionQueue` Drop |
| QP | `ibv_destroy_qp` | `QueuePair` Drop |
| CM ID | `rdma_destroy_id` 等 | `cm` 模块对应类型 |

- **`Arc` 保序**：子持父引用，子先 Drop（`SafeApi.md` 明确，`confirmed`）。
- **`unsafe from_raw`**：`ProtectionDomain::from_raw` 等供高级/测试场景，调用方需保证所有权（`pd.rs`）。
- **Transport 断开**：`Transport::disconnect` / `poll_disconnect`；`RdmaUdpSocket::close` 主动清理连接表。

### 5.3 风险点（设计文档与 bugs 目录）

- CQ 通知丢失、credit ring 乱序覆盖、Drop 顺序等已知问题记录在 `docs/bugs/`（静态阅读未验证修复状态，`inferred` 仍与架构相关）。
- MR 注册内存须在 dereg 前保持有效 — safe 层用 `OwnedMemoryRegion` 拥有缓冲区（`mr.rs`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

`rdma-io::Error`（`thiserror`）：

| 变体 | 来源 |
|------|------|
| `Verbs(io::Error)` | ibverbs 负 errno 或 ptr null + errno |
| `NoDevices` / `DeviceNotFound` | 枚举路径 |
| `InvalidArg` | Rust 侧校验 |
| `WorkCompletion` | WC status / vendor_err |
| `WouldBlock` | 非阻塞路径 |

辅助函数：`from_ret`（ibverbs）、`from_ret_errno`（rdma_cm）、`from_ptr`（`error.rs`）。

### 6.2 `unsafe` 集中处

| 层级 | 说明 |
|------|------|
| `rdma-io-sys` | 全部 `extern` 调用 inherently unsafe |
| `rdma-io` | 所有 FFI 调用包在 `unsafe { ... }`；公开 API 尽量 safe |
| `rdma-io-tonic` / `rdma-io-quinn` | 无新增 `unsafe` 块（依赖下层） |

### 6.3 安全契约（调用方）

- 遵循 verbs 的 QP 状态机与 WR 发布规则；`wr` 模块构建请求。
- `MemoryRegion` 生命周期覆盖 RDMA 访问期。
- 异步路径须在 CQ 上正确 **arm → poll → ack**（`AsyncCq` 实现 drain-after-arm，`confirmed` 注释）。
- rdma_cm：事件需 `ack`；fd 边缘触发需 drain（`async_cq` / `TokioCqNotifier`）。

### 6.4 C 错误习惯

- **ibverbs**：多数返回 `0` 成功，失败返回 **负 errno**（如 `-EINVAL`）。
- **rdmacm**：返回 `-1` 并设置 **errno**（与 ibverbs 不同，Rust 层分函数处理，`confirmed`）。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **`Send`/`Sync`** | 主要资源类型显式 `unsafe impl Send/Sync`（如 `Context`、`QueuePair`、`ProtectionDomain`）；依据为 libibverbs 进程级 handle 惯例（`inferred`） |
| **线程模型** | 无内置线程池；CQ 由调用方 task 轮询；CI 要求 `RUST_TEST_THREADS=1` 避免内核资源争用（`confirmed` README/CI） |
| **CQ 异步** | `CompletionChannel` fd + `CqNotifier`；tokio 下 `AsyncFd`（边缘触发，须 drain） |
| **CM 异步** | `AsyncCmListener` 与 transport `connect`/`accept` future |
| **Transport** | `poll_recv` / `poll_send_completion` 符合 `std::task::Poll`；`Transport: Send + Sync` |
| **tokio 集成** | `rdma-io` default feature `tokio`；`AsyncRdmaStream` 实现 futures IO，tonic 路径经 `Compat` |
| **futures-only** | `cargo build --no-default-features --features async`（README） |

**上层适配并发：**

- `RdmaConnector`：`Service` 实现，`Future` 为 `Send`。
- `RdmaIncoming`：`Stream` + 内部 `accept_fut`。
- `RdmaUdpSocket`：`Mutex`/`RwLock` 因 Quinn `&self` API；`recv_waker` 在 `connect_to` 后唤醒 `poll_recv`。

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| **硬件/软件依赖** | 需要 **siw** 或 **rxe** 软件 RDMA 设备；`sys_tests` 仅控制路径；数据路径依赖 rdma_cm + provider |
| **并发限制** | `RUST_TEST_THREADS=1`（README、CI env） |
| **sys 测试** | 直接 `unsafe` 调用 `ibv_*`，验证设备/PD/CQ/MR |
| **集成 breadth** | safe、cm、async、三种 ring/send transport、tonic、TLS、quinn、tonic-h3 |
| **CI** | `build-and-test`（siw，测试跑 5 遍抗 flaky）；`build-rxe`（可选编译 rxe 模块） |
| **E2E** | `tests/e2e/` Ansible playbooks — 远程/本地 VM RDMA 环境，非 crate 内单元测试 |
| **Fuzz** | 无 |
| **Examples** | 无独立 `examples/`；见 §1.3 |

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `rust-rdma-io/Cargo.toml` | workspace 成员与共享依赖 |
| `rust-rdma-io/README.md` | 功能清单、测试前提、crate 表 |
| `rdma-io-sys/README.md` | wrapper 动机、三分区、再生命令 |
| `rdma-io-sys/build.rs` | `cc` 编译 `rdma_wrapper` |
| `rdma-io-sys/wrapper/wrapper.h` | 96 个 `rdma_wrap_*`、verbs.h include |
| `rdma-io-sys/src/rdma/mod.rs` | ibverbs/rdmacm/wrapper 模块划分 |
| `rdma-io-sys/src/rdma/ibverbs/mod.rs` | `windows_link::link!("ibverbs" ...)` |
| `bnd-rdma-gen/rdma.toml` | bnd 三分区与 type_import |
| `docs/design/BndBindings.md` | bnd 流水线、规模表、vs bindgen |
| `docs/design/SafeApi.md` | Arc RAII 资源树 |
| `rdma-io/src/error.rs` | 双错误码约定 |
| `rdma-io/src/transport.rs` | Transport / TransportBuilder |
| `rdma-io/src/async_cq.rs` | drain-after-arm、CqNotifier |
| `rdma-io-tonic/src/connector.rs` | `RdmaConnector` + `Service<Uri>` |
| `rdma-io-tonic/src/incoming.rs` | `RdmaIncoming` Stream |
| `rdma-io-quinn/src/lib.rs` | `RdmaUdpSocket` / `AsyncUdpSocket` |
| `rdma-io-tests/Cargo.toml` | 测试依赖矩阵 |
| `.github/workflows/ci.yml` | siw/rxe、cmake winmd、单线程测试 |

---

## 10. 架构决策推断

### 10.1 bnd + 检入生成代码，而非 bindgen 或 build.rs 即时生成

| 字段 | 内容 |
|------|------|
| **决策** | WinMD 中间表示 + `windows-bindgen`；绑定源码提交到 `rdma-io-sys/src/rdma/` |
| **C 侧事实** | verbs 头巨大、类型交叉多；大量 inline 无法直接链接 |
| **Rust 侧事实** | `bnd-rdma-gen` + `rdma.toml` 三分区；依赖 `bnd-linux` 共享 POSIX 类型 |
| **推断动机** | docs.rs/CI 无需每台机器跑 libclang；命名空间模块化；与作者 bnd 生态一致 |
| **证据** | `docs/design/BndBindings.md`、`bnd-rdma-gen/rdma.toml` |
| **置信度** | `confirmed`（设计文档直述） |

### 10.2 `wrapper.c` 导出 inline verbs

| 字段 | 内容 |
|------|------|
| **决策** | 96 个 `rdma_wrap_*` C 函数 + `cc` 静态库 + 独立 WinMD partition |
| **C 侧事实** | `ibv_post_send`、`ibv_wr_*`、`ibv_poll_cq` 等为 `static inline` |
| **Rust 侧事实** | `build.rs` 仅编译 wrapper；生成代码 `link!("rdma_wrapper")` |
| **推断动机** | Rust FFI 只能链接真实符号；与 bindgen 项目常见 shim 模式相同 |
| **证据** | `rdma-io-sys/wrapper/`、`rdma-io-sys/README.md` |
| **置信度** | `confirmed` |

### 10.3 `Arc` 资源图 + 独立 `Transport` trait

| 字段 | 内容 |
|------|------|
| **决策** | 子资源 `Arc<Parent>`；数据路径抽象为 `Transport` / `TransportBuilder` |
| **C 侧事实** | 父先销毁子则 UB；verbs 与 CM 组合复杂 |
| **Rust 侧事实** | device/pd/qp 模块；三种 transport + `AsyncRdmaStream` 泛型 |
| **推断动机** | 避免 lifetime 组合爆炸；让 tonic/quinn 只依赖字节流语义而非 QP 细节 |
| **证据** | `docs/design/SafeApi.md`、`rdma-io/src/transport.rs` |
| **置信度** | `inferred`（SafeApi 确认 Arc；Transport 动机为推断） |

### 10.4 上层适配 crate（tonic / quinn）与核心 FFI 解耦

| 字段 | 内容 |
|------|------|
| **决策** | `rdma-io-tonic`、`rdma-io-quinn` 独立 workspace member，零 C FFI |
| **C 侧事实** | 无 gRPC/QUIC 概念；仅字节与连接 |
| **Rust 侧事实** | 注入 tonic `Service`/`Stream` 与 Quinn `AsyncUdpSocket` |
| **推断动机** | 核心库保持 RDMA 专注；协议栈版本与 feature（TLS、H3）独立演进 |
| **证据** | `rdma-io-tonic/src/lib.rs`、`rdma-io-quinn/src/lib.rs`、`README.md` |
| **置信度** | `inferred` |

### 10.5 tokio `AsyncFd` + epoll 驱动 CQ（非 busy poll）

| 字段 | 内容 |
|------|------|
| **决策** | `CqNotifier` trait；默认 `TokioCqNotifier`；drain-after-arm |
| **C 侧事实** | `ibv_req_notify_cq` + completion channel 可读事件 |
| **Rust 侧事实** | `async_cq.rs`、`tokio_notifier.rs` |
| **推断动机** | 与 tokio/tonic/quinn 生态集成；避免 spin 占满 CPU |
| **证据** | `rdma-io/src/async_cq.rs` |
| **置信度** | `confirmed`（注释说明 EPOLLET 与 drain） |

---

## 11. 待澄清

- `rdma-io-sys` 未使用 `links` key 时，多版本/重复链接 `ibverbs` 在复杂依赖树中的行为未静态验证。
- 部分 `rdma_wrap_*` 是否覆盖目标 rdma-core 版本的全部 inline API（升级 rdma-core 后的缺口）需对照上游头文件 diff。
- `docs/design/SafeApi.md` 仍将「async 集成」标为非目标，与当前 `async_*` / tonic 实现可能文档滞后（`inferred`）。
- Provider 专用 API（如 mlx5dv）是否计划进入 `-sys` 或 safe 层 — 设计文档称 non-goal。
- `credit_ring` / `read_ring` 与 siw/rxe 能力矩阵在测试文档中有记载，本分析未逐项核对跳过用例。
