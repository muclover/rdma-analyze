# rust-ibverbs — FFI 单项目分析

**分析对象（源码路径）：** `rdma/category-1/rust-ibverbs/`  
**计划序号：** P5（阶段 B1）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）；C 头参考上游 `linux-rdma/rdma-core` 的 `libibverbs/verbs.h`（样本树内 `vendor/rdma-core` 子模块未检出时以公开头文件为准）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`rust-ibverbs`（crates.io：`ibverbs` / `ibverbs-sys`）绑定 **libibverbs**（`rdma-core` 中的 userspace RDMA verbs 库），面向需要在 Rust 中做 **RDMA 控制面 + 数据面**（设备发现、PD/CQ/QP/MR、post send/recv、poll CQ）的开发者。架构为 **两层 workspace**：`ibverbs-sys`（bindgen 生成 + 少量手写）+ `ibverbs`（safe RAII、`Arc` 共享所有权、`std::io::Error`）。无独立 async crate；CQ 阻塞等待通过 **nix `poll`** 与 completion channel 完成。README 声明全 API **`Send` + `Sync`**（底层 libibverbs 线程安全）。**无 mock/mummy**；示例与 CI 依赖真实或 SoftRoCE 类 RDMA 环境。

### 库画像（§6 字段）

| 字段 | 内容 |
|------|------|
| **C 库名称与角色** | **libibverbs** — userspace RDMA verbs；控制路径经 `uverbs` 内核模块，数据路径常由厂商库直驱硬件 |
| **API 形态** | Opaque `ibv_*` 指针（`ibv_context`、`ibv_pd`、`ibv_cq`、`ibv_qp`、`ibv_mr` 等）；`ibv_context.ops` **函数表**分发 `poll_cq` / `post_send` / `post_recv`；QP **状态机**（`ibv_modify_qp`）；`ibv_wc` 描述完成事件 |
| **资源对** | `ibv_get_device_list` ↔ `ibv_free_device_list`；`ibv_open_device` ↔ `ibv_close_device`；`ibv_alloc_pd` ↔ `ibv_dealloc_pd`；`ibv_create_cq` ↔ `ibv_destroy_cq`；`ibv_create_qp` ↔ `ibv_destroy_qp`；`ibv_reg_mr` ↔ `ibv_dereg_mr` 等 |
| **线程安全** | README + crate 文档：底层 API 线程安全，Rust 类型普遍 `unsafe impl Send/Sync`（`confirmed`） |
| **分发方式** | 链接 `-libverbs`（`links = "ibverbs"`）；默认 **cmake 构建 vendored `rdma-core`** 产出头文件与 `build/lib`；可选 **`RDMA_CORE_INCLUDE_DIR` + `RDMA_CORE_LIB_DIR`** 使用系统安装 |
| **绑定难点** | 头文件体量大、演进快；`ibv_wc` 含 **union**（`imm_data` / `invalidated_rkey`）致 bindgen blocklist；大量 **bitfield 枚举**；provider 经 `ops` 间接调用 |

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| `ibverbs-sys` | `ibverbs-sys/` | `-sys`：bindgen → `OUT_DIR/bindings.rs`；手写 `ibv_wc`；`links = "ibverbs"` |
| `ibverbs` | `ibverbs/` | Safe API：设备/上下文/PD/CQ/QP/MR、post、poll/wait |

根 `Cargo.toml`：`members = ["ibverbs", "ibverbs-sys"]`（`confirmed`）。

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `ibverbs-sys/build.rs` | bindgen、cmake vendored rdma-core、链接与环境变量 |
| `ibverbs-sys/src/lib.rs` | `include!` 绑定 + `ibv_wc` 手写 + 布局单测 |
| `ibverbs-sys/vendor/rdma-core/` | git 子模块（`linux-rdma/rdma-core`），供头文件与本地链接 |
| `ibverbs/src/lib.rs` | 几乎全部 safe 封装（单文件 ~2100 行） |
| `ibverbs/examples/` | 示例 |
| `.github/workflows/` | `test.yml`、`check.yml`、`scheduled.yml` |
| `configure-ci.sh` | CI 安装 `libibverbs-dev`、clang、Cargo `target-dir` |
| `BUILD.bazel` / `MODULE.bazel` | Bazel 集成（根 BUILD 几乎为空占位） |

**无** 独立 `tests/`、`systest/`、`fuzz/` 目录；测试写在各 crate `src` 内 `#[cfg(test)]`。

### 1.3 Examples（简单阅读）

| 示例 | 文件 | 是否演示 FFI |
|------|------|----------------|
| `loopback` | `ibverbs/examples/loopback.rs` | **是** — 经 safe API：单设备 RC QP **自握手**（`endpoint` + `handshake(endpoint)`），`post_send`/`post_receive`、`CompletionQueue::poll`；未直接调用 `ibverbs_sys` |

### 1.4 Tests（§8 交叉引用）

| 类型 | 位置 | 说明 |
|------|------|------|
| 布局 | `ibverbs-sys/src/lib.rs` | `bindgen_test_layout_ibv_wc` |
| Serde / 类型 | `ibverbs/src/lib.rs` | `test_serde`、`test`、`gid_array_conversion` 等 |
| CI | `.github/workflows/test.yml` | `cargo test --all-features --all-targets`；需 `configure-ci.sh` + 子模块 |

---

## 2. 原生依赖

### 2.1 链接与 `links`

- `ibverbs-sys` 声明 **`links = "ibverbs"`**（`confirmed`，`ibverbs-sys/Cargo.toml`）。
- `build.rs` 固定：`println!("cargo:rustc-link-lib=ibverbs")`。

### 2.2 获取头文件与库路径

| 模式 | 触发条件 | 行为 |
|------|----------|------|
| **Vendored（默认）** | 未设置 `RDMA_CORE_INCLUDE_DIR` | `git submodule update`（若有 `.git`）；`cmake::Config` 配置 `vendor/rdma-core`（`NO_MAN_PAGES=1`，`CMAKE_INSTALL_PREFIX=/usr`，`no_build_target(true)`）；打印 `cargo:include` 与 `rustc-link-search` 指向 `vendor/rdma-core/build` |
| **系统 rdma-core** | 设置 `RDMA_CORE_INCLUDE_DIR` | **必须**同时设置 `RDMA_CORE_LIB_DIR`；用于 include 与 link search |
| **verbs 头路径** | `IBVERBS_HEADER_DIR`（可选） | 默认 `vendor/rdma-core/libibverbs`，bindgen 读 `{dir}/verbs.h` |

说明：`build.rs` 注释写明 vendored 构建主要为 **生成绑定**；同时仍向 Cargo 传递 include/link 路径，便于无系统库环境（如 docs.rs）编译（`confirmed` 注释，`inferred` 运行时是否始终链接 vendored `.so` 取决于部署）。

### 2.3 子模块与 CI 主机

- `.gitmodules`：`ibverbs-sys/vendor/rdma-core` → `https://github.com/linux-rdma/rdma-core.git`。
- `configure-ci.sh`：`apt` 安装 **`libibverbs1` / `libibverbs-dev`** 与 **clang**；`.cargo/config.toml` 将 `target-dir` 设为 `/tmp/cargo`（缩短路径，避免 docs.rs/本地路径过长问题，`confirmed` 自 `build.rs` 注释）。

### 2.4 Feature（`ibverbs`）

| Feature | 作用 |
|---------|------|
| `serde`（default） | `Guid`、`Gid`、`QueuePairEndpoint` 等序列化 |
| （无 async / static 等 FFI feature） | — |

`ibverbs` 依赖 `nix`（`fs` + **`poll`**），用于 CQ 事件等待。

---

## 3. 绑定生成

### 3.1 方式：**bindgen + 手写混合**

- **生成**：`ibverbs-sys/build.rs` 对 `verbs.h` 运行 bindgen，输出 `OUT_DIR/bindings.rs`，由 `lib.rs` `include!`（`confirmed`）。
- **手写**：`ibv_wc` 整结构 + 方法 + `Default` + 布局测试（因 `.blocklist_type("ibv_wc")`）。

### 3.2 bindgen 策略（摘要）

| 配置 | 值 |
|------|-----|
| Header | `{IBVERBS_HEADER_DIR or vendor}/verbs.h` |
| Allowlist | 函数 `ibv_.*`、`_ibv_.*`；类型 `ibv_.*`；变量 `IBV_LINK_LAYER_.*` |
| 枚举 | 多个 `ibv_*` 标志类为 `bitfield_enum`；`default_enum_style` Rust；`prepend_enum_name(false)` |
| 其它 | `derive_default/debug`；`size_t_is_usize(true)` |
| Blocklist | **`ibv_wc`** |

### 3.3 C 头暴露的主要 API 形态（`verbs.h`）

结合上游头文件与 allowlist 推断绑定覆盖面：

| 类别 | C 形态 | Rust 侧典型映射 |
|------|--------|-----------------|
| 设备枚举 | `ibv_get_device_list` / `ibv_free_device_list` | `devices()` → `DeviceList` |
| 上下文 | `ibv_open_device` / `ibv_close_device` | `Context` + `Arc<ContextInner>` |
| 保护域 | `ibv_alloc_pd` / `ibv_dealloc_pd` | `ProtectionDomain` |
| 完成队列 | `ibv_create_cq`、`ibv_comp_channel`、`ibv_get_cq_event` | `CompletionQueue`；poll 走 **`ctx.ops.poll_cq`** |
| QP | `ibv_create_qp`、`ibv_modify_qp`、`ibv_destroy_qp` | `QueuePairBuilder` → `PreparedQueuePair` → `handshake` → `QueuePair` |
| 内存 | `ibv_reg_mr`、`ibv_reg_dmabuf_mr`、`ibv_dereg_mr` | `MemoryRegion` |
| 数据路径 | `ops.post_send` / `ops.post_recv` | `QueuePair::post_*`（部分 `unsafe`） |
| 完成 | `struct ibv_wc`（含 union） | **手写** `ibv_wc` + safe 层 re-export |
| Provider 扩展 | `struct ibv_context_ops` 函数指针表 | safe 层显式经 `ops.poll_cq` 等调用 |

头文件中尚有 **SRQ、AH、MW、CQ extended、device ops** 等大量符号；safe 层仅封装子集（见 §4）。

### 3.4 再生流程

1. 更新 `vendor/rdma-core` 子模块版本。  
2. 修改 `build.rs` allowlist / blocklist（若 API 变更）。  
3. 若 `ibv_wc` 布局变化，同步手写 `ibverbs-sys/src/lib.rs` 与 `bindgen_test_layout_ibv_wc`。  
4. 发布前依赖 CI `cargo test`（本分析未执行构建）。

---

## 4. 分层与公开 API

### 4.1 `ibverbs-sys`

- **职责**：接近 1:1 的 `ibv_*` FFI；crate 级 `#![allow(...)]` 抑制 bindgen 噪声。
- **公开面**：生成的类型/函数 + 手写 `ibv_wc` 及其 inherent API（`wr_id`、`len`、`is_valid`、`error`、`imm_data` 等）。
- **无** safe 包装、无 RAII。

### 4.2 `ibverbs`（safe）

单文件模块树（逻辑分层）：

| 类型 / 模块 | 职责 |
|-------------|------|
| `devices` / `DeviceList` / `Device` | 枚举 HCA，`open()` → `Context` |
| `Context` | `create_cq`、`alloc_pd`、`gid_table`；打开时 **`query_port`** 且要求端口 **ACTIVE/ARMED**（硬编码 `PORT_NUM = 1`） |
| `CompletionQueue` | `poll`（非阻塞，经 `ops.poll_cq`）；`wait`（`req_notify_cq` + **nix poll** + `ibv_get_cq_event`） |
| `ProtectionDomain` | `create_qp`、`allocate` / `register` / `register_dmabuf` |
| `QueuePairBuilder` | RC/UC/UD 类型与大量 QP 参数；`build()` → `PreparedQueuePair` |
| `PreparedQueuePair` | `endpoint()`、`handshake(remote)` 三阶段 `ibv_modify_qp`（INIT→RTR→RTS） |
| `QueuePair` | `post_send`/`post_receive`（`unsafe`）、`post_write`/`post_read`（RDMA one-sided） |
| `MemoryRegion<T>` | RAII `ibv_dereg_mr`；`slice` → `LocalMemorySlice`（`repr(transparent)` `ibv_sge`） |
| `Guid` / `Gid` / `GidEntry` | 网络字节序 / union 的 Rust newtype |
| `QueuePairEndpoint` | 握手用 `(qp_num, lid, gid?)`；可选 **serde** |

**Re-export 的 FFI 类型**：`ibv_wc`、`ibv_wc_opcode`、`ibv_wc_status`、`ibv_qp_type`、`ibv_mtu`、`ibv_access_flags`、`ibv_gid_type` 等。

### 4.3 上层适配 crate

**N/A** — workspace 仅 sys + safe，无 `rdma-io-tonic` 类适配层。

### 4.4 Verbs 覆盖面（相对 libibverbs）

| 能力 | safe 层 |
|------|---------|
| RC QP + 内存注册 + SEND/RECV + RDMA read/write | **有** |
| CQ 轮询 + 事件等待 | **有** |
| RoCE GID / `gid_table` | **有**（builder `set_gid_index`） |
| DMA-BUF MR | **有**（`register_dmabuf`，数据占位 `()`） |
| UD QP / SRQ / AH / 原子操作 / MW | **无** 或仅 bindgen 存在、safe 未封装（TODO 注释提及 `ibv_post_srq_recv`、UD GRH 等） |
| Async / 与 Tokio 集成 | **无** |

---

## 5. 资源与生命周期

### 5.1 所有权图（`Arc` 链）

```
DeviceList (Drop → ibv_free_device_list)
  └─ Device (借用列表中的 *ibv_device)
       └─ Context { Arc<ContextInner> }  (Drop → ibv_close_device)
            ├─ CompletionQueue { Arc<CompletionQueueInner> }  (Drop → destroy_cq + destroy_comp_channel)
            └─ ProtectionDomain { Arc<ProtectionDomainInner> }  (Drop → ibv_dealloc_pd)
                 ├─ MemoryRegion { MemoryRegionInner }  (Drop → ibv_dereg_mr)
                 └─ QueuePair { *ibv_qp + Arc<ProtectionDomainInner> }  (Drop → ibv_destroy_qp)
```

`CompletionQueueInner` 持有 `_ctx: Arc<ContextInner>`，保证 CQ 不晚于 context 释放（`confirmed` 自字段与 `Drop` 顺序）。

### 5.2 创建 / 释放对照

| 资源 | 创建（safe 入口） | 释放 |
|------|-------------------|------|
| 设备列表 | `devices()` | `DeviceList::drop` → `ibv_free_device_list` |
| 上下文 | `Device::open` | `ContextInner::drop` → `ibv_close_device` |
| PD | `Context::alloc_pd` | `ProtectionDomainInner::drop` → `ibv_dealloc_pd` |
| CQ + comp channel | `Context::create_cq` | `CompletionQueueInner::drop` |
| QP | `QueuePairBuilder::build` | `QueuePair::drop` → `ibv_destroy_qp` |
| MR | `allocate` / `register` / `register_dmabuf` | `MemoryRegionInner::drop` → `ibv_dereg_mr` |

### 5.3 异步完成与缓冲区复用

- **WR 未完成前**：`post_send` / `post_receive` 文档要求 MR/缓冲区在对应 **WC 被 poll 到** 之前不得复用或 drop（`unsafe` 契约，`confirmed` 文档）。
- **CQ 溢出**：`poll` 文档警告 CQ 满触发 `IBV_EVENT_CQ_ERR` 后 CQ 不可用（与 RDMAmojo 一致）。

### 5.4 `Drop` 失败策略

多处 destroy 返回非 0 时 **`panic!`**（如 `ibv_destroy_cq`、`ibv_dereg_mr`），而非 `Result`（`confirmed`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

- Safe API 统一 **`std::io::Result`** / `io::Error`：
  - 指针 API 失败：常 `io::Error::last_os_error()` 或 `from_raw_os_error(errno)`（`ibv_modify_qp`、`ibv_query_port` 等返回负 errno）。
  - 逻辑错误：如端口非 ACTIVE → `io::Error::other(...)`；`wait` 超时 → `ErrorKind::TimedOut`。
- **无** 独立 `IbverbsError` 枚举；`ibv_wc` 完成状态用 `ibv_wc_status` + `ibv_wc::error()`（sys 层）。

### 6.2 C 返回值习惯

| 模式 | 示例 |
|------|------|
| 指针成功 / `NULL` 失败 | `ibv_open_device`、`ibv_reg_mr` |
| `0` 成功 / 非 0 errno | `ibv_modify_qp`、`ibv_destroy_cq` |
| 负值表示错误条数 | `_ibv_query_gid_table`（safe 层取反格式化错误） |
| `ops.poll_cq` 返回完成条数或 `<0` 失败 | `CompletionQueue::poll` |

### 6.3 `unsafe` 边界

| 区域 | 说明 |
|------|------|
| 整个 `ibverbs-sys` | 原始 FFI |
| `QueuePair::post_send` / `post_receive` | 调用方保证 MR 与 WR 生命周期 |
| `DeviceList` | 内部 `&'static mut [*mut ibv_device]` — 依赖 libibverbs 列表指针在 `free` 前有效（与 C API 契约一致） |
| `CompletionQueue::wait` | `BorrowedFd::borrow_raw` + 对 `cc.fd` 的非阻塞假设 |
| `Guid`/`Gid` `AsRef` | `transmute` 到 `__be64` / `ibv_gid` |

### 6.4 `ibv_wc` 与 union

C 头中 `imm_data` / `invalidated_rkey` 为 **union**；bindgen blocklist 后由手写结构体将 `imm_data` 暴露为 `u32`，并通过 `wc_flags` 区分语义（`IBV_WC_WITH_IMM` 等），与 man page 行为一致（`confirmed` 自 `ibverbs-sys/src/lib.rs` 注释）。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **`Send` / `Sync`** | `DeviceList`、`Device`、`ContextInner`、`CompletionQueueInner`、`ProtectionDomainInner`、`MemoryRegionInner`、`QueuePair` 等均 **`unsafe impl Send/Sync`**；crate 文档引用 RDMAmojo 线程安全说明（`confirmed` README） |
| **共享所有权** | `Arc` 用于 context/CQ/PD 交叉引用 |
| **锁** | C 侧 `ibv_context` 含 `pthread_mutex_t`；Rust 不再包一层 Mutex |
| **async** | **无** `async fn`/executor 集成；`CompletionQueue::wait` 为同步阻塞 + `poll` 重试 |
| **CQ 通知** | 创建 CQ 时绑定 `ibv_comp_channel`；`fcntl` **O_NONBLOCK**；`wait` 用 nix `poll` 等待 fd |
| **数据路径并发** | 依赖 libibverbs 线程安全；多线程 poll/post 需应用自行遵守 QP/CQ 并发规则（库未额外约束） |

**硬件 / 内核依赖**：真实 RDMA 设备或 **SoftRoCE** 等；CI 安装 libibverbs 后跑集成测试（`inferred` 自 workflow + `configure-ci.sh`）。

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| **单元测试** | `ibv_wc` 布局；`Guid`/`Gid`/`LocalMemorySlice`；可选 `serde` roundtrip |
| **集成测试** | **无** 独立 `tests/*.rs`；`cargo test` 在 CI 跑 `--all-targets`，隐含需 RDMA 设备场景有限 |
| **示例** | 仅 **`loopback`**：单进程 RC 自连接，验证 send/recv WC；**演示 safe FFI 用法**，非 sys 直连 |
| **覆盖率** | `test.yml` 中 `cargo llvm-cov` job |
| **MSRV** | `check.yml`：`1.82.0`（与 `ibverbs-sys` `rust-version`、bindgen `unsafe extern` 一致） |
| **fuzz / systest / mock** | **无** |

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `rdma/category-1/rust-ibverbs/README.md` | 项目定位、libibverbs 角色、线程安全、rdma-core 依赖说明 |
| `rdma/category-1/rust-ibverbs/Cargo.toml` | workspace 两成员 |
| `rdma/category-1/rust-ibverbs/ibverbs-sys/Cargo.toml` | `links = "ibverbs"`、build-dependencies |
| `rdma/category-1/rust-ibverbs/ibverbs-sys/build.rs` | bindgen 配置、cmake vendored、环境变量、`blocklist_type("ibv_wc")` |
| `rdma/category-1/rust-ibverbs/ibverbs-sys/src/lib.rs` | 手写 `ibv_wc`、布局测试 |
| `rdma/category-1/rust-ibverbs/ibverbs/Cargo.toml` | 依赖 `ibverbs-sys`、`nix` poll、`serde` feature |
| `rdma/category-1/rust-ibverbs/ibverbs/src/lib.rs` | RAII/`Arc`、`poll`/`wait`、`handshake`、`post_*`、`Send/Sync` |
| `rdma/category-1/rust-ibverbs/ibverbs/examples/loopback.rs` | 唯一 example、RC 自握手与 poll 流程 |
| `rdma/category-1/rust-ibverbs/.gitmodules` | vendored `rdma-core` 子模块路径 |
| `rdma/category-1/rust-ibverbs/configure-ci.sh` | CI 安装 libibverbs、clang、target-dir |
| `rdma/category-1/rust-ibverbs/.github/workflows/test.yml` | 测试/覆盖率/子模块 checkout |
| `rdma/category-1/rust-ibverbs/.github/workflows/check.yml` | fmt/clippy/doc/hack/MSRV |
| `linux-rdma/rdma-core` `libibverbs/verbs.h` | `ibv_wc` union、`ibv_context_ops`、`资源 struct` 形态 |

---

## 10. 架构决策推断

### 10.1 独立 `ibverbs-sys` + bindgen allowlist

| 字段 | 内容 |
|------|------|
| **决策** | Workspace 拆分 `-sys`（生成绑定）与 `ibverbs`（safe）；bindgen 用 **`ibv_.*` allowlist** 而非全量 `verbs.h` |
| **观察到的 C 侧事实** | `verbs.h` 数千行，含设备 ops、扩展 CQ/QP、内核兼容字段 |
| **观察到的 Rust 侧事实** | 仅 `ibverbs-sys` 包含 `build.rs`/`include!`；safe crate 依赖 path `ffi` |
| **推断动机** | 控制生成体积与编译时间；`-sys` 可单独发版（`0.3.x` vs safe `0.9.x`）；符合 Rust RDMA 生态常见分层 |
| **证据** | `Cargo.toml`、`ibverbs-sys/build.rs` |
| **置信度** | `inferred` |

### 10.2 默认 cmake vendored `rdma-core` + 可选系统路径

| 字段 | 内容 |
|------|------|
| **决策** | 无系统头文件时自动 submodule + cmake；支持 `RDMA_CORE_*` 覆盖 |
| **观察到的 C 侧事实** | 头文件与 `libibverbs` 随 rdma-core 发行版捆绑 |
| **观察到的 Rust 侧事实** | `CMAKE_INSTALL_PREFIX=/usr` 规避过长 rpath；CI 仍 `apt install libibverbs-dev` |
| **推断动机** | docs.rs/无 root 权限环境仍能 **生成绑定**；开发者机器可用系统库减构建量；与 README「cargo 自动 build vendor」一致 |
| **证据** | `ibverbs-sys/build.rs`、`README.md`、`configure-ci.sh` |
| **置信度** | `inferred`（「仅用于绑定」vs 实际链接行为以部署为准） |

### 10.3 手写 `ibv_wc`（blocklist bindgen）

| 字段 | 内容 |
|------|------|
| **决策** | `.blocklist_type("ibv_wc")` 后在 `ibverbs-sys` 手写结构体与安全访问器 |
| **观察到的 C 侧事实** | `struct ibv_wc` 含 **anonymous union**（immediate vs invalidated rkey） |
| **观察到的 Rust 侧事实** | 固定布局 `#[repr(C)]` + `offset` 单测；`imm_data()` 检查 `wc_flags` |
| **推断动机** | bindgen 对 union/bitfield 支持不稳定；手写可暴露 Rust 友好 API 并锁定 ABI 测试 |
| **证据** | `ibverbs-sys/build.rs`、`ibverbs-sys/src/lib.rs`；上游 `verbs.h` `struct ibv_wc` |
| **置信度** | `inferred` |

### 10.4 `Arc` RAII + 显式 `Send`/`Sync`

| 字段 | 内容 |
|------|------|
| **决策** | 内核对象指针包在 `Arc<...Inner>` 中，`Drop` 调 C destroy；对外 `unsafe impl Send/Sync` |
| **观察到的 C 侧事实** | `ibv_*` 为不透明指针；libibverbs 文档级线程安全 |
| **观察到的 Rust 侧事实** | 无 `Mutex` 包裹；`QueuePair` 持 `Arc<ProtectionDomainInner>` |
| **推断动机** | 多线程 poll/post 场景需要 `Send`；`Arc` 表达 CQ/PD/QP 共享生命周期优于单所有者 `Box` |
| **证据** | `ibverbs/src/lib.rs`、`README.md` Thread safety |
| **置信度** | `confirmed`（README 线程安全）+ `inferred`（为何选 Arc 而非其他共享模型） |

### 10.5 经 `ibv_context.ops` 调用 `poll_cq` / `post_send`

| 字段 | 内容 |
|------|------|
| **决策** | 非直接链接 `ibv_poll_cq` 符号，而从 `(*ctx).ops.poll_cq` 等函数指针派发 |
| **观察到的 C 侧事实** | `struct ibv_context { struct ibv_context_ops ops; ... }` |
| **观察到的 Rust 侧事实** | `CompletionQueue::poll`、`QueuePair::post_send` 取 `ctx.ops.*.unwrap()` |
| **推断动机** | 与上游 **provider 插件模型** 一致；各厂商可替换 fast path |
| **证据** | `ibverbs/src/lib.rs`；`verbs.h` `ibv_context_ops` |
| **置信度** | `inferred` |

### 10.6 `nix::poll` 实现 `CompletionQueue::wait`（非 async 运行时）

| 字段 | 内容 |
|------|------|
| **决策** | CQ 阻塞等待用 comp channel fd + `ibv_req_notify_cq` / `ibv_get_cq_event`，不用 busy-loop 封装为 async |
| **观察到的 C 侧事实** | completion channel 为可 `poll` 的 fd；`ibv_get_cq_event` 可能阻塞 |
| **观察到的 Rust 侧事实** | 创建 CQ 后 `fcntl` **非阻塞**；`wait` 循环中 `poll` + `WouldBlock` 重试 |
| **推断动机** | 在保持 **同步 API** 的同时支持低流量阻塞；避免引入 Tokio 依赖 |
| **证据** | `ibverbs/src/lib.rs` `create_cq`、`CompletionQueue::wait` |
| **置信度** | `inferred` |

### 10.7 `Guid` / `Gid` newtype（字节序与 union）

| 字段 | 内容 |
|------|------|
| **决策** | 用 `[u8; N]` + `repr(transparent)` 包装，而非直接暴露 `u64` 或 bindgen `ibv_gid` union |
| **观察到的 C 侧事实** | GUID 为 `__be64`；`ibv_gid` 为 16 字节 union |
| **观察到的 Rust 侧事实** | `from_be_bytes` / `subnet_prefix()`；`GidEntry` 包装 `_ibv_query_gid_table` |
| **推断动机** | 避免主机序误用；serde/调试更稳定 |
| **证据** | `ibverbs/src/lib.rs` `Guid`、`Gid` |
| **置信度** | `inferred` |

---

## 11. 待澄清

- 样本检出时 **`ibverbs-sys/vendor/rdma-core` 子模块树为空**，未本地对照 vendored `verbs.h` 与生成绑定差异；发布绑定以实际 submodule 版本为准。
- 默认构建产物链接的是 **vendored `build/lib`** 还是系统 `/usr/lib/libibverbs.so`，取决于环境变量与部署方式；`build.rs`「仅为绑定」注释与 `rustc-link-lib=ibverbs` 并存，运行时解析需实测。
- bindgen allowlist 实际导出的 **函数/类型完整列表** 未在本分析中枚举（需构建后查看 `bindings.rs`）。
- **SRQ、UD、原子操作、Address Handle** 等是否有路线图 — 当前仅 TODO/文档提及。
- `register_dmabuf` 返回 `MemoryRegion<()>` 的所有权语义与 TODO「opaque unowned」设计未定型。
- **Bazel**（`MODULE.bazel`）与 Cargo 路径谁为主力构建路径 — 仓库内 Cargo 更完整。
- **无 mock**：无法在无非 RDMA 环境做 CI 单元级数据路径测试的策略是否接受。
