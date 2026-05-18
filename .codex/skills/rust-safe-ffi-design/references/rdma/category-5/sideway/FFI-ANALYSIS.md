# sideway — FFI 单项目分析

**分析对象（源码路径）：** `rdma/category-5/sideway/`  
**计划序号：** P11（阶段 B7）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）；C API 形态参考 rdma-core `libibverbs` / `librdmacm` 公开 man page 与源码注释  
**许可证：** MPL-2.0（`LICENSE`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`sideway`（crates.io：`sideway`）是在 **Rust 侧直接封装 RDMA 编程 API** 的库，底层 FFI 全部来自依赖 **`rdma-mummy-sys`**（编译期不在本 crate 内跑 bindgen、也不链接系统头文件生成绑定）。项目重点把 rdma-core 较新的 **`ibv_wr_*` / `ibv_start_poll`** 等扩展 verbs 引入 Rust，并用 **`PostSendGuard` 类型状态** 约束「每 QP 同时仅一个 post-send 会话、每 guard 同时仅一个 WR / 一个 handle」；传统 **`ibv_post_send` / `ibv_poll_cq`** 仍提供 **Basic** 路径但 README 声明**无性能保证**。公开模块为 **`ibverbs`**（设备/上下文/PD/CQ/QP/MR）与 **`rdmacm`**（连接管理）；资源以 **`Arc`** 共享所有权。`libibmad` / `libibumad` 等不在范围内。测试含 **trybuild compile-fail** 校验 guard 不变量，示例覆盖 `rc_pingpong`、`ibv_devinfo`、`cmtime`、`show_gids` 等。

### 库画像（§6 字段）

| 字段 | 内容 |
|------|------|
| **C 库名称与角色** | **libibverbs**（verbs 资源与数据路径）+ **librdmacm**（带内连接管理）；运行时须安装真实 rdma-core 用户态库，否则非 inline 调用可能返回 `EOPNOTSUPP`（`confirmed` README） |
| **API 形态** | 不透明 `ibv_*` / `rdma_*` 指针；扩展对象 `ibv_qp_ex` / `ibv_cq_ex` + **`ibv_wr_start` → `ibv_wr_*` → `ibv_wr_complete`**；CQ 扩展轮询 **`ibv_start_poll` / `ibv_next_poll` / `ibv_end_poll`**；QP 状态机经 `ibv_modify_qp` |
| **资源对** | `ibv_get_device_list` ↔ `ibv_free_device_list`；`ibv_open_device` ↔ `ibv_close_device`；`ibv_alloc_pd` ↔ `ibv_dealloc_pd`；`ibv_create_cq(_ex)` ↔ `ibv_destroy_cq`；`ibv_create_qp(_ex)` ↔ `ibv_destroy_qp`；`ibv_reg_mr` ↔ `ibv_dereg_mr`；RDMA CM：`rdma_create_event_channel` / `rdma_create_id` 等（`rdmacm` 模块） |
| **线程安全** | 主要 RDMA 句柄类型均 **`unsafe impl Send/Sync`**（`device_context`、`protection_domain`、`memory_region`、QP/CQ、RDMA CM `EventChannel`/`Identifier`）；未再包 `Mutex`，依赖 libibverbs / librdmacm 线程安全契约（`inferred`） |
| **分发方式** | **编译期**：经 crates.io **`rdma-mummy-sys`**（mummy 桩库，README 称 `rdma-core-mummy`）；**运行期**：系统 `libibverbs` / `librdmacm`（及厂商 OFED 可选） |
| **绑定难点** | 扩展 WR/CQ API 与 legacy API 双轨；QP/CQ **Basic vs Extended** 分裂；`PostSendGuard` 需在类型层模拟 C 侧「单 WR 构建会话」；inline 数据在 Basic 路径需额外拷贝以保证安全 |

---

## 1. 仓库结构

### 1.1 Crate 布局

| 项 | 说明 |
|------|------|
| **形态** | **单 crate** `sideway`（非 workspace）；无独立 `-sys` 成员 |
| **入口** | `src/lib.rs` 导出 `ibverbs`、`rdmacm` |
| **版本** | `0.4.1`（`Cargo.toml`） |

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `src/ibverbs/` | libibverbs 安全封装：设备、上下文、PD、CQ、QP、MR、地址/GID |
| `src/rdmacm/communication_manager.rs` | librdmacm 事件通道、Identifier、连接参数等 |
| `examples/` | 可运行示例（见 §8） |
| `tests/` | 集成测试 + `post_send_guard/` compile-fail 用例 |
| `Justfile` | `just test` / 覆盖率 / 示例跑法 |
| `.github/workflows/test.yml` | Linux CI：clippy、`cargo test`、codecov |
| `.cirrus.yml` | Rocky Linux + 自建 rdma-core + RXE 软设备覆盖 |
| `codecov.yml` | 覆盖率上传配置 |

**无** 本仓库内 `build.rs`、vendor 子模块或 bindgen 输出目录。

### 1.3 `ibverbs` 子模块

| 模块 | 职责 |
|------|------|
| `device` | `DeviceList` / `Device` 枚举与 `open` |
| `device_context` | `DeviceContext`：查询设备/端口/GID、分配 PD、创建 CQ builder |
| `protection_domain` | `ProtectionDomain`（`Arc`）、`reg_mr`、`create_qp_builder` |
| `memory_region` | `MemoryRegion`（`Arc`） |
| `completion` | Basic/Extended CQ、Poller、`GenericCompletionQueue` |
| `queue_pair` | QP builder、状态机表、`PostSendGuard`、Basic/Extended QP |
| `address` | `Gid`、`GidEntry`、AH 属性等 |
| 根 `mod` | `AccessFlags`（`bitmask-enum` 包装 `ibv_access_flags`） |

### 1.4 `rdmacm` 子模块

| 模块 | 职责 |
|------|------|
| `communication_manager` | `EventChannel`、`Identifier`、`Event`、连接/监听/接受；**不**封装 `rdma_create_ep` / `rdma_create_qp`（文档说明避免限制 QP 属性且有 race 风险） |

### 1.5 Examples（简单阅读）

| 示例 | 文件 | 是否演示 FFI 用法 |
|------|------|-------------------|
| `show_gids` | `examples/show_gids.rs` | **是** — 遍历设备 GID 表，表格输出（类 `show_gids` 脚本） |
| `ibv_devinfo` | `examples/ibv_devinfo.rs` | **是** — 设备/端口属性树形打印 |
| `rc_pingpong` | `examples/rc_pingpong.rs` | **是** — RC ping-pong；OOB TCP 交换 QP 信息 + `PostSendGuard` / `GenericCompletionQueue` |
| `rc_pingpong_split` | `examples/rc_pingpong_split.rs` | **是** — 拆分版 ping-pong（CI 覆盖率用） |
| `cmtime` | `examples/cmtime.rs` | **是** — RDMA CM 建连耗时基准；可选自管 QP（`self_modify`） |

### 1.6 Tests（§8 交叉引用）

| 类型 | 位置 |
|------|------|
| 集成 | `tests/test_qp.rs`、`test_cq.rs`、`test_mr_and_cq.rs`、`test_post_send.rs` |
| Compile-fail | `tests/compiletest.rs` + `tests/post_send_guard/*.rs` |
| 单元 | `queue_pair.rs` 内 `#[cfg(test)]` QP 状态机与 post 错误路径 |

---

## 2. 原生依赖

### 2.1 编译期 vs 运行期

| 阶段 | 依赖 | 说明 |
|------|------|------|
| **编译 / 链接类型生成** | **`rdma-mummy-sys` 0.2.3** | 本 crate **不**含 `links =`、**不**含 `build.rs`；FFI 类型与函数由该依赖提供（mummy 桩，`confirmed` README + `Cargo.toml`） |
| **运行** | 系统 **libibverbs**、**librdmacm** | README：未安装时非 inline C 函数可能 **`EOPNOTSUPP`**；Ubuntu 示例包 `libibverbs1 librdmacm1 ibverbs-providers` |
| **构建 `rdma-mummy-sys` 工具链** | CMake、Clang | README 安装说明（传递依赖，非 sideway 直接声明） |

### 2.2 本 crate 直接 Rust 依赖（与 FFI 相关）

| 依赖 | 作用 |
|------|------|
| `rdma-mummy-sys` | 全部 `ibv_*` / RDMA CM FFI |
| `libc` | errno 常量（如 `EINVAL`、`ENOMEM`） |
| `thiserror` | 领域错误枚举 |
| `bitmask-enum` | `AccessFlags`、WC/QP 标志类 |
| `serde` | `Guid`、`Gid` 等序列化（设备上下文查询结果） |
| `os_socketaddr` | RDMA CM 地址处理 |

**无** `[features]` 段：未通过 feature 切换 bindgen / 系统库。

### 2.3 CI / 本地环境

| 来源 | 行为 |
|------|------|
| `test.yml` | `apt install libibverbs1 librdmacm1`；可选卸载 `mlx5_ib` 模块；`cargo clippy` + `cargo test` |
| `.cirrus.yml` | 自建 rdma-core、`LD_LIBRARY_PATH` 指向 build/lib；RXE；`just test-*-with-cov` |
| `Justfile` | 检测 `cargo-nextest`；覆盖率跑 `show_gids`、`ibv_devinfo`、条件跑 ping-pong / cmtime |

---

## 3. 绑定生成

### 3.1 本仓库：**无 bindgen / 无手写 `-sys`**

- `sideway` **不包含** `build.rs` 或 `wrapper.h`。
- 所有 `extern` 绑定由依赖 **`rdma-mummy-sys`** 提供；编译链接 mummy 库，**不在本 crate 编译时**对系统 rdma-core 跑 bindgen（`confirmed` 用户给定事实 + 仓库结构）。

### 3.2 上游绑定形态（经 `rdma_mummy_sys::` 使用推断）

| 类别 | 本 crate 中的使用方式 |
|------|------------------------|
| Verbs 扩展 post | `ibv_wr_start`、`ibv_wr_send`、`ibv_wr_rdma_*`、`ibv_wr_set_sge(_list)`、`ibv_wr_set_inline_data(_list)`、`ibv_wr_complete`、`ibv_wr_abort` |
| Verbs legacy post | `ibv_post_send`、`ibv_post_recv` |
| CQ 扩展 poll | `ibv_start_poll`、`ibv_next_poll`、`ibv_end_poll` |
| CQ legacy poll | `ibv_poll_cq`（Basic CQ `start_poll` 内部分配 `ibv_wc` 数组） |
| 对象创建 | `ibv_create_qp` / `ibv_create_qp_ex`、`ibv_create_cq` / `ibv_create_cq_ex` |
| 查询 / 修改 | `ibv_query_*`、`ibv_modify_qp`；WC 读字段 `ibv_wc_read_*`（Extended poller） |

### 3.3 再生流程（对本项目）

**N/A —** 绑定维护在 **`rdma-mummy-sys`** 仓库；升级 sideway 的 `rdma-mummy-sys` 版本即可带入新符号。本分析未打开 mummy 生成脚本。

---

## 4. 分层与公开 API

### 4.1 分层总览

```
应用 / examples / tests
        ↓
   sideway (safe only)
   ├─ ibverbs
   └─ rdmacm
        ↓
   rdma-mummy-sys (FFI + mummy 链接)
        ↓
   运行期 libibverbs / librdmacm
```

**无** 本仓库内 `-sys` crate；**无** 独立 async 层。

### 4.2 `ibverbs` 核心类型与模式

| 类型 / 概念 | 职责 |
|-------------|------|
| `DeviceList` / `Device` | 枚举 HCA，`Device::open` → `Arc<DeviceContext>` |
| `DeviceContext` | 查询属性/GID 表、`alloc_pd`、`create_cq_builder`、`create_comp_channel` |
| `ProtectionDomain` | `Arc`；`reg_mr`（`unsafe`）、`create_qp_builder` |
| `MemoryRegion` | `Arc`；`lkey`/`rkey`；`Drop` → `ibv_dereg_mr` |
| `BasicCompletionQueue` / `ExtendedCompletionQueue` | `build` / `build_ex`；`GenericCompletionQueue` 枚举统一 |
| `BasicQueuePair` / `ExtendedQueuePair` | `build` / `build_ex`；`GenericQueuePair` 枚举统一 |
| `QueuePair` trait | `modify`、`start_post_send`、`start_post_recv`、`qp_number` |
| **`PostSendGuard` trait** | `construct_wr` → `WorkRequestHandle` → `LocalBufferHandle`；`post()` 提交 |
| `PostRecvGuard` | recv WR 构建 + `ibv_post_recv` |
| `QueuePairAttribute` + 静态状态表 | RC/UC/UD/RAW 的 `ibv_modify_qp` 合法迁移与 mask 校验 |

### 4.3 Basic vs Extended 双轨（数据路径）

| 能力 | Basic 路径 | Extended 路径 |
|------|------------|-----------------|
| QP 创建 | `ibv_create_qp` | `ibv_create_qp_ex` + `ibv_qp_ex` |
| Send post | 攒 `ibv_send_wr` 链表 → **`ibv_post_send`** | **`ibv_wr_start`** → `ibv_wr_*` → **`ibv_wr_complete`**；失败/未 post 时 **`ibv_wr_abort`**（`Drop`） |
| CQ 创建 | `ibv_create_cq` | `ibv_create_cq_ex` |
| CQ poll | **`ibv_poll_cq`** 批量拉 `ibv_wc` | **`ibv_start_poll`** + `ibv_next_poll` + per-WC `ibv_wc_read_*` |
| 性能预期 | README：**无性能保证** | 设计目标：**更优**（`confirmed` 模块文档） |

### 4.4 `PostSendGuard` 类型状态（机制摘要）

1. `QueuePair::start_post_send(&mut self)` 取得 **`&mut` guard**，同一时刻仅能存在一个（compile-fail：`one_qp_has_only_one_guard`）。
2. `guard.construct_wr(...)` 返回 **`WorkRequestHandle`**，同时只能持有一个（`one_guard_has_only_one_wr`）。
3. `setup_send` / `setup_write` 等返回 **`LocalBufferHandle`**，与另一条 WR 的 handle 互斥（`one_guard_has_only_one_handle`）。
4. Extended：`start_post_send` 内调 **`ibv_wr_start`**；`post()` 调 **`ibv_wr_complete`** 并 `mem::forget(guard)` 避免 `Drop` 误 abort。

文档注释与 trybuild 用例一致（`confirmed`）。

### 4.5 `rdmacm` 公开面

| 类型 | 职责 |
|------|------|
| `EventChannel` | 创建 CM 事件通道、`create_id`、`get_cm_event` |
| `Identifier` | `bind_addr`、`listen`、`connect`、`accept`、`get_device_context`、`get_qp_attr` |
| `Event` / `EventType` | 连接请求、建立、断开等 |
| `ConnectionParameter` | 握手参数（含 QP number） |

用户自行用 `ibverbs` 创建/修改 QP，CM 仅负责带内信令（`confirmed` 模块级文档）。

### 4.6 上层适配 crate

**N/A** — 无独立 IO/async/tonic 适配层；示例内直接用 safe API。

### 4.7 Verbs / CM 覆盖面（相对 rdma-core 子集）

| 能力 | 封装情况 |
|------|----------|
| 设备发现、上下文、PD、MR、CQ、QP（含 ex） | **有** |
| 扩展 WR + 扩展 CQ poll | **有**（重点） |
| Legacy post/poll | **有**（Basic） |
| QP 状态机（RC/UC/UD/RAW 表） | **有** |
| RDMA CM 事件驱动建连 | **有**（不包 `rdma_create_qp`） |
| SRQ、MW、大量 MAD/UMAD | **无**（README 明确超出范围） |
| Async executor 集成 | **无** |

---

## 5. 资源与生命周期

### 5.1 所有权图（`Arc` 链）

```
DeviceList (Drop → ibv_free_device_list)
  └─ Device<'list> (借用列表内 ibv_device)
       └─ DeviceContext (Arc, Drop → ibv_close_device)
            ├─ CompletionChannel (Arc, Drop → destroy comp channel)
            ├─ Basic/ExtendedCompletionQueue (_dev_ctx, 可选 _comp_channel)
            └─ ProtectionDomain (Arc, Drop → ibv_dealloc_pd)
                 ├─ MemoryRegion (Arc, Drop → ibv_dereg_mr)
                 └─ Basic/ExtendedQueuePair (_pd, _send_cq, _recv_cq; Drop → ibv_destroy_qp)
```

QP/CQ 持有 **`Arc<DeviceContext>`** 或 **`Arc<ProtectionDomain>`** 与对端 CQ 的 **`GenericCompletionQueue`**，防止底层对象先释放（`confirmed` 字段命名 `_dev_ctx`、`_pd`）。

### 5.2 创建 / 释放对照

| 资源 | 创建入口 | 释放 |
|------|----------|------|
| 设备列表 | `DeviceList::new` | `DeviceList::drop` |
| 上下文 | `Device::open` | `DeviceContext::drop` |
| PD | `DeviceContext::alloc_pd` | `ProtectionDomain::drop` |
| MR | `ProtectionDomain::reg_mr` | `MemoryRegion::drop` |
| CQ | `CompletionQueueBuilder::build` / `build_ex` | 各 CQ `Drop` → `ibv_destroy_cq` |
| QP | `QueuePairBuilder::build` / `build_ex` | 各 QP `Drop` → `ibv_destroy_qp` |
| CM 通道 / ID | `EventChannel::new` / `create_id` | 对应 `Drop`（`communication_manager`） |

### 5.3 异步完成与缓冲区

- **`reg_mr` / `setup_sge`**：`unsafe` 契约要求缓冲区在 WR 完成前有效（`confirmed` 文档）。
- **Basic `setup_inline_data`**：库内 **拷贝** 到 `Vec`，使用户可在 `post()` 前修改原 buffer（注释说明可能比 C 多一次 memcpy，`confirmed`）。
- **Extended inline**：直接 `ibv_wr_set_inline_data`，缓冲区须在 `post()` 完成前保持有效（`inferred` 与 C API 一致）。
- **ExtendedPostSendGuard::drop**：未 `post` 则 **`ibv_wr_abort`**。

### 5.4 `Drop` 失败策略

- 多数 destroy（QP/CQ）在返回非 0 时 **`assert_eq!(ret, 0)`**（panic），而非 `Result`（`confirmed` `queue_pair` / `completion`）。
- MR/PD dereg/dealloc **未检查**返回值（`confirmed` `memory_region` / `protection_domain`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

- 各领域操作使用 **`thiserror`** 包装的错误类型（如 `CreateQueuePairError`、`PostSendError`、`ModifyQueuePairError`、`PollCompletionQueueError`）。
- 底层 verbs 失败多为 **`io::Error`**（`last_os_error` 或 `from_raw_os_error`）；`PostSendError` / `PostRecvError` 对 `EINVAL`/`ENOMEM`/`EFAULT` 做细分变体（`confirmed` `queue_pair`）。
- QP 状态迁移错误可带 **`InvalidTransition`** / **`InvalidAttributeMask`**（含建议 mask），源于静态 **`QP_STATE_TABLE`** 预检 + `ibv_modify_qp` 返回值（`confirmed`）。

### 6.2 C 返回值习惯（本 crate 处理）

| 模式 | 示例 |
|------|------|
| 指针 NULL | `ibv_open_device`、`ibv_reg_mr` |
| 0 成功 / 非 0 errno | `ibv_modify_qp`、`ibv_wr_complete` |
| poll 返回条数或负 errno | `ibv_poll_cq`；`ibv_start_poll` 空 CQ → `ENOENT` 映射 `CompletionQueueEmpty` |
| 未知枚举值 | 多处 **`panic!`**（WC status、QP type 等） |

### 6.3 `unsafe` 边界

| 区域 | 说明 |
|------|------|
| 全部 `rdma_mummy_sys` 调用 | 在 safe 方法内集中 `unsafe` |
| `ProtectionDomain::reg_mr` | 调用方保证指针与长度 |
| `SetScatterGatherEntry::setup_sge` | 缓冲区生命周期 |
| `CompletionQueue::cq` / `DeviceContext` 等 `unsafe fn` 裸句柄 | 生命周期不与返回值绑定（文档明确要求） |
| `DeviceList::as_device_slice` | 依赖 C 列表在 `free` 前有效 |

### 6.4 类型状态（编译期安全）

- **`PostSendGuard` / `WorkRequestHandle` / `LocalBufferHandle`** 通过 **`&mut` 借用** 互斥，由 **trybuild** 保证无法双开 guard/WR/handle（`confirmed` `tests/compiletest.rs`）。
- **运行时** 仍须遵守 RDMA 内存与 QP 并发语义；类型系统不防止多线程同时 `post`（`inferred`）。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **`Send` / `Sync`** | `DeviceContext`、`ProtectionDomain`、`MemoryRegion`、`Basic/ExtendedQueuePair`、`Basic/ExtendedCompletionQueue`、`CompletionChannel`、RDMA CM `EventChannel`/`Identifier` 等均 **`unsafe impl Send/Sync`**（`confirmed` 源码） |
| **共享所有权** | 资源普遍包在 **`Arc`** 中跨线程传递 |
| **锁** | Rust 层无全局 `Mutex`；依赖 C 库线程安全 |
| **async** | **无** `async fn` 或 executor 集成；示例/测试为同步 + 线程阻塞 |
| **CQ 事件** | 支持 `CompletionChannel` + `AsRawFd`；扩展 poller 文档描述与 `ibv_poll_cq` 协作拉取更多 WC（`completion` 模块注释） |
| **RDMA CM** | 同步 `get_cm_event` 循环；`cmtime` 用多线程 + 通道测时 |

**硬件依赖**：真实 RDMA NIC、RXE 或 CI 软 RD 环境；无纯 Rust mock 层（mummy 仅编译链，`inferred`）。

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| **Compile-fail** | `tests/compiletest.rs` + trybuild：`post_send_guard` 三则不变量（单 QP 单 guard、单 guard 单 WR、单 guard 单 handle） |
| **集成测试** | `test_post_send`（rstest 矩阵 Basic/Extended QP × CQ）；`test_qp`、`test_cq`、`test_mr_and_cq` — 需 RDMA 环境（`inferred`） |
| **单元测试** | `queue_pair` 内 QP 状态迁移与 post 错误分类 |
| **示例** | `show_gids`、`ibv_devinfo`（设备信息）；`rc_pingpong` / `rc_pingpong_split`（RC + OOB TCP）；`cmtime`（RDMA CM 延迟） |
| **CI** | GitHub Actions + Cirrus；`cargo clippy -D warnings`；codecov（`just generate-cov`） |
| **MSRV** | `Cargo.toml` 未写 `rust-version`；edition **2021** |
| **fuzz / systest** | **无** |

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `rdma/category-5/sideway/README.md` | 定位、mummy 编译、运行期 rdma-core、`ibv_wr_*` 重点、范围外组件 |
| `rdma/category-5/sideway/Cargo.toml` | `rdma-mummy-sys` 依赖、许可证、无 features/build.rs |
| `rdma/category-5/sideway/LICENSE` | MPL-2.0 |
| `rdma/category-5/sideway/src/lib.rs` | 模块划分 |
| `rdma/category-5/sideway/src/ibverbs/queue_pair.rs` | PostSendGuard、Basic/Extended QP、WR API、`QP_STATE_TABLE` |
| `rdma/category-5/sideway/src/ibverbs/completion.rs` | Basic/Extended CQ、`ibv_start_poll`、Poller |
| `rdma/category-5/sideway/src/ibverbs/device_context.rs` | `Arc<DeviceContext>`、查询 API、Drop |
| `rdma/category-5/sideway/src/ibverbs/protection_domain.rs` | `Arc`、MR/QP 创建 |
| `rdma/category-5/sideway/src/rdmacm/communication_manager.rs` | RDMA CM 封装范围、不包 `rdma_create_qp` |
| `rdma/category-5/sideway/tests/compiletest.rs` | trybuild 入口 |
| `rdma/category-5/sideway/tests/post_send_guard/*.rs` + `*.stderr` | 类型状态不变量 |
| `rdma/category-5/sideway/examples/*.rs` | 示例清单 |
| `rdma/category-5/sideway/Justfile` | 测试与覆盖率命令 |
| `rdma/category-5/sideway/.github/workflows/test.yml` | CI 步骤 |
| `project-scope.md` | 单项目分析范围、静态分析、12 章节约束 |

---

## 10. 架构决策推断

### 10.1 依赖 `rdma-mummy-sys` 而非本仓 bindgen

| 字段 | 内容 |
|------|------|
| **决策** | 单 crate 仅 safe 层；FFI 全部由 **`rdma-mummy-sys`** 提供 |
| **观察到的 C 侧事实** | rdma-core 头文件庞大且随版本演进 |
| **观察到的 Rust 侧事实** | 无 `build.rs`；README 写明编译不需系统 rdma-core 头库 |
| **推断动机** | 编译/CI/docs 可复现；与 RDMA-Rust 生态 mummy 路线一致；绑定升级与 safe API 解耦 |
| **证据** | `Cargo.toml`、`README.md` |
| **置信度** | `confirmed`（README）+ `inferred`（动机） |

### 10.2 Basic / Extended 双轨 QP 与 CQ

| 字段 | 内容 |
|------|------|
| **决策** | `build`+legacy API vs `build_ex`+`ibv_wr_*` / `ibv_start_poll`；`GenericQueuePair` / `GenericCompletionQueue` 枚举统一 |
| **观察到的 C 侧事实** | `ibv_qp_ex` / `ibv_cq_ex` 为扩展入口；旧 API 仍广泛存在 |
| **观察到的 Rust 侧事实** | Extended 路径注释强调性能；Basic 明确无性能保证 |
| **推断动机** | 渐进迁移：老环境可跑 Basic；新驱动可走扩展 fast path |
| **证据** | `queue_pair.rs`、`completion.rs`、README |
| **置信度** | `inferred` |

### 10.3 `PostSendGuard` 类型状态 + trybuild

| 字段 | 内容 |
|------|------|
| **决策** | 用 `&mut` 借用链（guard → WR handle → buffer handle）模拟 C 侧单次 post 会话；compile-fail 锁定不变量 |
| **观察到的 C 侧事实** | `ibv_wr_*` 在 `ibv_wr_start` 与 `ibv_wr_complete` 之间不宜并发构建多 WR |
| **观察到的 Rust 侧事实** | 三则 trybuild；Extended `Drop` → `ibv_wr_abort` |
| **推断动机** | 在 Rust 类型系统内防止误用比仅靠文档更可靠；对齐 rdma-core 新 API 语义 |
| **证据** | `queue_pair.rs`、`tests/post_send_guard/` |
| **置信度** | `inferred` |

### 10.4 Basic 路径 inline 数据强制拷贝

| 字段 | 内容 |
|------|------|
| **决策** | `BasicPostSendGuard::setup_inline_data` 复制到 `Vec` 再挂 SGE |
| **观察到的 C 侧事实** | `ibv_post_send` 可能在调用返回后才消费 inline 数据（provider 相关） |
| **观察到的 Rust 侧事实** | 注释承认比 C 多一次 memcpy，换接口安全一致 |
| **推断动机** | 允许用户在 `post()` 前释放/修改原 buffer；与 Extended 直接传指针形成对比 |
| **证据** | `queue_pair.rs` `setup_inline_data` 注释 |
| **置信度** | `inferred` |

### 10.5 `Arc` + 显式 `Send`/`Sync`

| 字段 | 内容 |
|------|------|
| **决策** | 上下文/PD/MR/QP/CQ 用 `Arc` 共享；广泛 `unsafe impl Send/Sync` |
| **观察到的 C 侧事实** | libibverbs 对象指针在线程间传递常见 |
| **观察到的 Rust 侧事实** | 无额外 `Mutex` |
| **推断动机** | 多线程 poll/post 与 CM 回调场景需要 `Send`；`Arc` 表达 CQ/PD/QP 交叉引用 |
| **证据** | `device_context.rs`、`protection_domain.rs`、`queue_pair.rs` |
| **置信度** | `inferred` |

### 10.6 QP 状态机静态表预检

| 字段 | 内容 |
|------|------|
| **决策** | `LazyLock` 初始化 RC/UC/UD/RAW 的 `QP_STATE_TABLE`，`modify` 前校验 mask |
| **观察到的 C 侧事实** | `ibv_modify_qp` 对属性 mask 与状态迁移敏感 |
| **观察到的 Rust 侧事实** | 丰富 `ModifyQueuePairError` 变体 + 单元测试 |
| **推断动机** | 比单纯映射 errno 更易调试；减少无效 `ibv_modify_qp` 调用 |
| **证据** | `queue_pair.rs` `QP_STATE_TABLE`、`#[cfg(test)]` |
| **置信度** | `inferred` |

### 10.7 RDMA CM 不封装 `rdma_create_qp`

| 字段 | 内容 |
|------|------|
| **决策** | 只封装事件通道与 Identifier；QP 由 `ibverbs` 手动创建/修改 |
| **观察到的 C 侧事实** | `rdma_create_qp` 封装限制 QP 参数；社区提及 race |
| **观察到的 Rust 侧事实** | 模块文档 + doctest 展示 `build_ex` + `get_qp_attr` + `modify` |
| **推断动机** | 保留扩展 QP/WR API 的完整控制力，与项目 verbs 重点一致 |
| **证据** | `communication_manager.rs` 模块文档 |
| **置信度** | `confirmed`（文档明确）+ `inferred`（与 Extended QP 策略一致） |

---

## 11. 待澄清

- **`rdma-mummy-sys` 0.2.3** 的 allowlist、mummy 与真实 `libibverbs` 符号差异未在本仓库内核对；运行时符号解析行为需结合该 crate 文档。
- **Extended CQ** `Drop` 中 TODO：是否应使用 `ibv_cq_ex_to_cq` 再 destroy（`completion.rs` 注释）— 与 mummy 导出是否完备相关。
- **destroy 返回值**：QP/CQ 失败即 panic，MR/PD dealloc 忽略返回值 — 生产环境策略未在文档说明。
- **Extended inline** 与 **Basic 拷贝** 混用时，用户易误解生命周期契约 — 是否需在类型/API 层进一步区分未可知。
- **未知枚举** 一律 `panic!` — 新内核/驱动新增枚举时的前向兼容性策略不明。
- **MSRV / edition 2024** 路线图、`Cargo.toml` 未声明 `rust-version`。
- 本分析**未构建**，`rdma-mummy-sys` 与当前 rdma-core 头文件版本漂移未验证。
