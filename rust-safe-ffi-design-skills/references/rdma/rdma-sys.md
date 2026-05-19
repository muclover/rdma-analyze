# rdma-sys — FFI 单项目分析

**分析对象（源码路径）：** `rdma/category-4/rdma-sys/`  
**计划序号：** P8（阶段 B4）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）；C 头以 `src/bindings.h` 所 include 的 `infiniband/verbs.h`、`rdma/rdma_cma.h`、`rdma/rdma_verbs.h` 为准  
**文档版本：** 2026-05-18

---

## 0. 摘要

`rdma-sys`（crates.io：`rdma-sys`，仓库 [datenlord/rdma-sys](https://github.com/datenlord/rdma-sys)）是面向 **libibverbs** 与 **librdmacm** 的 **纯 `-sys` 单 crate** 绑定：build 时用 **bindgen** 从 `src/bindings.h` 生成 `OUT_DIR/bindings.rs`，再经 `include!` 与手写模块合并为公开 API。README 说明 bindgen 对 **C `static inline` 与嵌套/union 结构** 处理不佳，故对 blocklist 类型与大量 inline 辅助函数 **逐案手写**（`types.rs`、`verbs.rs`、`opcode.rs`）。**无** safe 封装、**无** RAII、**无** async 层；调用方直接持有 `*mut ibv_*` / `*mut rdma_cm_id` 并遵守 C 生命周期。示例 `server` / `client` 演示 **RDMA CM**（`rdma_getaddrinfo` → `rdma_create_ep` → post/poll）在 Rust 中的裸 FFI 用法。CI 依赖 **SoftRoCE（rxe）** 与 `cargo test` / example 联跑。**无** mock/mummy。

### 库画像（§6 字段）

| 字段 | 内容 |
|------|------|
| **C 库名称与角色** | **libibverbs** — userspace RDMA verbs（设备、PD/CQ/QP/MR、post/poll）；**librdmacm** — RDMA 连接管理（地址解析、`rdma_cm_id`、listen/connect、与 verbs 协同的 `rdma_reg_*` / `rdma_post_*` 便捷 API） |
| **API 形态** | 不透明 `ibv_*` / `rdma_cm_*` 指针；`ibv_context.ops` **函数表**；`ibv_qp_ex` / `ibv_cq_ex` **结构内函数指针**（扩展 WR/CQ 路径）；`ibv_send_wr` / `ibv_wc` 等含 **union**；大量 **bitfield 标志枚举**；C 头中大量 **`static inline`** 与 **宏**（如 `ibv_reg_mr`、`IBV_OPCODE_RC_*`） |
| **资源对** | `ibv_get_device_list` ↔ `ibv_free_device_list`；`ibv_open_device` ↔ `ibv_close_device`；`ibv_alloc_pd` ↔ `ibv_dealloc_pd`；`ibv_create_cq` ↔ `ibv_destroy_cq`；`ibv_create_qp` ↔ `ibv_destroy_qp`；`ibv_reg_mr` / `ibv_reg_mr_iova2` ↔ `ibv_dereg_mr`；`rdma_create_ep` ↔ `rdma_destroy_ep` 等（完整表见 C 文档，本 crate 1:1 暴露） |
| **线程安全** | 遵循 **rdma-core** 文档：libibverbs 在常见部署下可多线程使用同一 context，但 QP/CQ/MR 并发规则由应用保证；本 crate **未** 声明 `Send`/`Sync`（`confirmed` 无相关 impl） |
| **分发方式** | **仅系统库**：`build.rs` 经 **pkg-config** 探测 `libibverbs`（≥ **1.8.28**）、`librdmacm`（≥ **1.2.28**），动态链接（`.statik(false)`）；**无** vendored rdma-core、**无** `links =` 键 |
| **绑定难点** | union（`ibv_wc`、`ibv_send_wr`、`rdma_cm_event` 等）blocklist 后手写；**inline** 通过 `verbs.rs` 转调 `ops` / `ibv_qp_ex` 函数指针；`verbs_context` **container_of**；C 宏 `ibv_reg_mr` / `__builtin_constant_p` 仅部分以 `__ibv_reg_mr*` 近似；`pthread_*` opaque |

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| `rdma-sys` | 根目录单 crate | 唯一成员；纯 FFI（`confirmed`，根 `Cargo.toml` 无 `[workspace]`） |

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `src/bindings.h` | 聚合 include：`verbs.h`、`rdma_cma.h`、`rdma_verbs.h` |
| `build.rs` | pkg-config + bindgen → `OUT_DIR/bindings.rs` |
| `src/lib.rs` | `include!` 绑定；`mod opcode / types / verbs`；`pub use` 子模块 |
| `src/types.rs` | blocklist 的 union/复杂 struct 手写 |
| `src/verbs.rs` | C `static inline` 与 CM 便捷函数 Rust 再实现（~1300+ 行） |
| `src/opcode.rs` | `IBV_OPCODE_*` 宏的 `paste` 生成常量 |
| `examples/server.rs`、`examples/client.rs` | RDMA CM echo 演示 |
| `scripts/run.sh` | CI/本地：rxe SoftRoCE + `cargo test` + server/client |
| `rdma-env-setup/` | git 子模块（`datenlord/rdma-env-setup`），`ci.yml` 中 `setup.sh` 调用 |
| `.github/workflows/ci.yml` | fmt + build/test（Rust **1.61.0**） |

**无** 独立 `tests/`、`fuzz/`、`systest/`；crate 内 **无** `#[cfg(test)]`（`confirmed` 全树 grep）。

### 1.3 Examples（简单阅读）

| 示例 | 文件 | 是否演示 FFI |
|------|------|----------------|
| `server` | `examples/server.rs` | **是** — 被动 `rdma_getaddrinfo` / `rdma_create_ep` / `rdma_listen` / `rdma_get_request` / `rdma_accept`；`rdma_reg_msgs`、`rdma_post_recv` / `rdma_post_send`；`rdma_get_recv_comp` / `rdma_get_send_comp`；手写 `ibv_wc`、`ibv_qp_attr` |
| `client` | `examples/client.rs` | **是** — 主动连接同路径；CLI `server_ip` + 端口（默认 **7471**）；可选 `IBV_SEND_INLINE` |

两例均在 `main` 用 `unsafe` 块直接调用 `rdma_sys::*`，无 safe 中间层。

### 1.4 Tests（§8 交叉引用）

| 类型 | 位置 | 说明 |
|------|------|------|
| 单元 / 布局测试 | — | **无** |
| 集成 | `scripts/run.sh` | `cargo test --all` + SoftRoCE 上跑 examples（`inferred` 需 rxe 与以太网口） |
| CI | `.github/workflows/ci.yml` | `apt install librdmacm-dev`；子模块 `rdma-env-setup`；`./scripts/run.sh` |

---

## 2. 原生依赖

### 2.1 链接与 `links`

- **未** 在 `Cargo.toml` 声明 `links = "ibverbs"` 或 `links = "rdmacm"`（`confirmed`）。
- `build.rs` 通过 **pkg-config** 向 Cargo 传递 link 信息（pkg-config crate 默认 `cargo:rustc-link-lib` 等，`inferred`）。

### 2.2 pkg-config 版本下限

| 库 | pkg-config 名 | 最低版本 | 失败提示包名 |
|----|---------------|----------|--------------|
| libibverbs | `libibverbs` | **1.8.28** | `libibverbs-dev` |
| librdmacm | `librdmacm` | **1.2.28** | `librdmacm-dev` |

`link_rdma_core` 使用 `.statik(false)`，即倾向 **动态链接** 系统 `.so`（`confirmed`，`build.rs`）。

### 2.3 Include 路径

- 收集两库 `include_paths`，去重后追加 **`/usr/include`**，作为 bindgen `clang -I` 参数（`confirmed`）。
- 构建失败时 `panic!("please install {pkg_name} {version})")`（`confirmed`）。

### 2.4 子模块与 CI 主机

- `.gitmodules`：`rdma-env-setup` → `https://github.com/datenlord/rdma-env-setup.git`（branch `main`）。
- `ci.yml`：checkout **submodules**；`git submodule update --remote --recursive`；仅显式 `apt install librdmacm-dev`（libibverbs 通常作为依赖被拉入，`inferred`）；Rust toolchain **1.61.0**。
- `scripts/run.sh`：删除/创建 **`rxe_eth0`** SoftRoCE；取第一块 `eth*` 的 IPv4；`cargo test --all` 后后台 `server`、前台 `client`。

### 2.5 Feature

**无** `[features]` 段（`confirmed`，`Cargo.toml`）。

### 2.6 运行时依赖

| 依赖 | 版本（manifest） | 用途 |
|------|------------------|------|
| `libc` | 0.2 | 与 C 互操作 |
| `memoffset` | 0.6 | `verbs.rs` 中 `container_of!`、`offset_of!(verbs_context, …)` |
| `paste` | 1.0 | `opcode.rs` 生成 `IBV_OPCODE_RC_*` 等常量名 |

---

## 3. 绑定生成

### 3.1 方式：**bindgen + 手写混合**

| 层次 | 机制 |
|------|------|
| **生成** | `build.rs` → `OUT_DIR/bindings.rs`；`lib.rs` `include!(concat!(env!("OUT_DIR"), "/bindings.rs"))` |
| **手写类型** | `types.rs`：blocklist 的 struct/union |
| **手写函数** | `verbs.rs`：C inline / 宏语义 |
| **手写常量** | `opcode.rs`：头文件宏 `concat_ibv_opcode!` |

### 3.2 bindgen 策略（摘要）

| 配置 | 值 |
|------|-----|
| Header | `src/bindings.h` |
| Allowlist 函数 | `ibv_.*`、`rdma_.*` |
| Allowlist 类型 | `ibv_.*`、`rdma_.*`、`verbs_.*`、`ib_uverbs_access_flags` |
| Blocklist 类型 | `in6_addr`；`sockaddr.*`；`timespec`；`ibv_ah_attr`；`ibv_async_event`；`ibv_flow_spec`；`ibv_gid`；`ibv_global_route`；`ibv_mw_bind_info`；`ibv_ops_wr`；`ibv_send_wr`；`ibv_wc`；`rdma_addr`；`rdma_cm_event`；`rdma_ib_addr`；`rdma_ud_param` |
| Opaque | `pthread_.*` |
| 枚举 | 大量 `bitfield_enum`（cap/flags/mask 类）；`constified_enum_module`（`ibv_qp_state`、`rdma_cm_event_type` 等）；`rustified_enum("ibv_event_type")` |
| 其它 | `derive_copy(true)`；`derive_debug/default(false)`；`generate_comments(false)`；`prepend_enum_name(false)`；`rustfmt_bindings(true)`；`size_t_is_usize(true)`；`disable_untagged_union()` |
| 注释掉的选项 | `generate_inline_functions(true)`（未启用，改由 `verbs.rs` 手写） |

bindgen **0.59.2**（`build-dependencies`，`confirmed`）。

### 3.3 C 头暴露的主要 API 形态（经 allowlist + 手写补全）

| 类别 | C 形态 | Rust 侧 |
|------|--------|---------|
| 设备 / 上下文 | `ibv_get_device_list`、`ibv_open_device`、`ibv_query_*` | 生成函数 + `___ibv_query_port` 等 inline 包装 |
| Verbs 数据路径 | `ibv_context.ops.post_send/post_recv/poll_cq` | `ibv_post_send`、`ibv_poll_cq` 等 `verbs.rs` 转 `ops` |
| 扩展 QP/CQ | `ibv_qp_ex` / `ibv_cq_ex` 内函数指针 | `ibv_wr_*`、`ibv_start_poll` 等 `#[inline] unsafe fn` |
| Provider 扩展 | `verbs_context` + `abi_compat == u32::MAX` | `verbs_get_ctx` + `verbs_get_ctx_op!` + `container_of!` |
| 连接管理 | `rdma_*`（`rdma_cma.h`） | 生成 + `rdma_reg_*` / `rdma_post_*` / `rdma_get_*_comp` inline |
| WR / WC | `ibv_send_wr`、`ibv_wc`（union） | **`types.rs` 手写** |
| CM 事件 | `rdma_cm_event`（`param` union） | **`types.rs` 手写** |
| 地址 | `rdma_addr`（sockaddr union） | **`types.rs` 手写**；`rdma_get_local_addr` 等读 `rdma_cm_id.route` |

头文件中 **Flow、ESP、Counters、WQ、TD、DM** 等扩展 API：bindgen 可生成声明，部分能力仅在有 `verbs_context` 对应 op 时由 `verbs.rs` 返回 `EOPNOTSUPP`（`confirmed` 模式如 `ibv_create_flow`）。

### 3.4 再生流程

1. 主机安装 **libibverbs-dev ≥ 1.8.28**、**librdmacm-dev ≥ 1.2.28** 及 clang（bindgen 需要）。
2. 若上游头文件变更：调整 `build.rs` allowlist / blocklist / `bitfield_enum` 列表。
3. 若 blocklist 类型布局变化：同步 `types.rs`（必要时增加布局测试 — 当前 **无**）。
4. 若新增 C inline：在 `verbs.rs` 按函数指针或 `ops` 模式追加。
5. 本分析 **未** 执行构建；绑定符号全集以构建后 `OUT_DIR/bindings.rs` 为准（见 §11）。

---

## 4. 分层与公开 API

### 4.1 单 crate 结构（纯 `-sys`）

```
lib.rs
├── include!(OUT_DIR/bindings.rs)   // bindgen 主体
├── opcode.rs   → pub use opcode::*  // IBV_OPCODE_* 常量
├── types.rs    → pub use types::*   // 手写 union/struct
└── verbs.rs    → pub use verbs::*   // inline 再实现 + CM 辅助
```

- **职责**：尽可能完整地暴露 **libibverbs + librdmacm** FFI；保持 C 签名与语义，**不** 提供 RAII 或 `Result` 包装。
- **crate 属性**：`#![deny(warnings)]` + 大量 `allow`（bindgen 命名、缺失 safety doc 等）（`confirmed`，`lib.rs`）。

### 4.2 模块说明

| 模块 | 内容 |
|------|------|
| **bindings（生成）** | `ibv_*` / `rdma_*` 函数与多数 struct；bitfield 枚举；`constified_enum_module` 子模块 |
| **`types`** | `ibv_gid`、`ibv_wc`、`ibv_send_wr`、`ibv_flow_spec`、`rdma_addr`、`rdma_cm_event`、`ibv_ah_attr`、`rdma_ud_param` 等 |
| **`verbs`** | `ibv_wr_*`（`ibv_qp_ex`）；`ibv_*_poll`（`ibv_cq_ex`）；`ibv_post_*` / `ibv_poll_cq`（`ops`）；`verbs_get_ctx` 分发；`rdma_post_*` / `rdma_get_*_comp`；RDMA CM 常量（`RDMA_UDP_QKEY` 等） |
| **`opcode`** | `ibv_opcode` 模块：`IBV_OPCODE_RC_SEND_FIRST` 等，用 `paste` 拼接 transport + op |

### 4.3 上层适配 crate

**N/A** — 仓库仅 `rdma-sys`，无 workspace 内 safe 或 async 封装。

### 4.4 Verbs / CM 覆盖面（相对绑定意图）

| 能力 | 本 crate |
|------|----------|
| 设备枚举、context、PD、CQ、QP、MR 基础 API | bindgen + 部分 inline（`confirmed` allowlist） |
| `ibv_qp_ex` 扩展 post、`ibv_cq_ex` 扩展 poll | **`verbs.rs` 手写**（`confirmed`） |
| `ibv_send_wr` / `ibv_wc` / CM 事件 | **`types.rs` 手写** |
| RDMA CM 便捷 post/poll/reg | **`verbs.rs`**（`rdma_reg_msgs`、`rdma_get_send_comp` 等） |
| Safe API / RAII | **无** |
| Async / executor | **无** |

---

## 5. 资源与生命周期

### 5.1 所有权模型

本 crate **不** 包装所有权：所有 `ibv_*` / `rdma_cm_id` 均为 **裸指针**，由调用方配对 C API 释放（`confirmed` 设计）。

典型配对（调用方责任，与 C 文档一致）：

| 资源 | 创建（示例） | 释放（示例） |
|------|--------------|--------------|
| 设备列表 | `ibv_get_device_list` / `__ibv_get_device_list` | `ibv_free_device_list` |
| 上下文 | `ibv_open_device` | `ibv_close_device` |
| PD | `ibv_alloc_pd` | `ibv_dealloc_pd` |
| CQ | `ibv_create_cq` / `ibv_create_cq_ex` | `ibv_destroy_cq` |
| QP | `ibv_create_qp` / `ibv_create_qp_ex` | `ibv_destroy_qp` |
| MR | `ibv_reg_mr` / `__ibv_reg_mr` / `ibv_reg_mr_iova2` | `ibv_dereg_mr` |
| CM endpoint | `rdma_create_ep` | `rdma_destroy_ep` |
| CM 事件 | `rdma_get_cm_event` | `rdma_ack_cm_event` |

**无** Rust `Drop` impl（`confirmed`）。

### 5.2 WR 与缓冲区

- `ibv_post_send` / `ibv_post_recv` 文档（C）要求 WR 与 SGE 指向的内存在完成前有效；`verbs.rs` 注释：**`IBV_SEND_INLINE` 时 send 缓冲区可在调用返回后立即复用**（`confirmed` 自 `ibv_post_send` 上方注释）。
- `rdma_post_recv` 内对 `addr` 范围有 **`assert!`**（相对 `mr->addr` / `length`）（`confirmed`，`verbs.rs`）。

### 5.3 CM 与 MR

- `rdma_reg_msgs` / `rdma_reg_read` / `rdma_reg_write` 封装 `ibv_reg_mr` 与固定 access flags（`confirmed`）。
- Example 在断开前 **未** 展示 `rdma_dereg_mr` / `rdma_destroy_ep` 的完整清理路径（`inferred` 自 example 提前 `return` 分支）。

### 5.4 `Drop` 失败策略

**N/A** — 无 Rust `Drop`；错误由 C 返回码 / `errno` 表示。

---

## 6. 错误与安全边界

### 6.1 错误模型

- **无** Rust `Error` trait 或统一枚举；与 C 一致：
  - 指针 API：`NULL` 表示失败（如 `ibv_reg_mr`）。
  - `c_int` 返回：`0` 成功，负值常为 **`-errno`**（如 `ibv_modify_qp`）；部分 inline 返回 **`libc::EOPNOTSUPP`** 正数（如 `ibv_destroy_flow` 无 op 时）（`confirmed` `verbs.rs`）。
  - `rdma_seterrno`：非 0 时写 `errno` 并返回 **-1**（`confirmed`）。
- Example 用 `ret != 0` 与 `io::Error::from_raw_os_error(-ret)` 打印（`confirmed` `examples/server.rs`）。

### 6.2 C 返回值习惯（本 crate 暴露）

| 模式 | 示例 |
|------|------|
| 指针成功 / `NULL` 失败 | `ibv_open_device`、`rdma_reg_msgs`（经 `ibv_reg_mr`） |
| `ops` 派发 | `ibv_poll_cq` → `(*context).ops.poll_cq` |
| 可选能力 | `ibv_create_flow` → `None` + `errno = EOPNOTSUPP` |
| `Option<*mut T>` | 多个 `ibv_create_*_ex` 包装 |

### 6.3 `unsafe` 边界

| 区域 | 说明 |
|------|------|
| **整个 crate** | 几乎所有公开函数为 `unsafe` 或需在 `unsafe` 块调用 C ABI |
| **`verbs.rs` inline** | 普遍 `unwrap()` 函数指针（`ops` / `ibv_qp_ex` 字段）；假定 C 侧已初始化 |
| **`verbs_get_ctx`** | `container_of` 与 `abi_compat` 判断；错误指针将导致 UB |
| **`types` union** | `ibv_wc.imm_data_invalidated_rkey_union` 等需按 `wc_flags` 解释（与 C 相同） |
| **`rdma_get_*_comp`** | 内含 `assert!` 校验 CQ 与 `rdma_cm_id` 一致 |

### 6.4 Blocklist 与 union 处理

| C 类型 | Rust 策略 |
|--------|-----------|
| `ibv_wc` | 具名字段 `imm_data_invalidated_rkey_union`（非 anonymous union 字段名） |
| `ibv_send_wr` | `wr_t`、`qp_type_t`、`bind_mw_tso_union_t` 等显式 union |
| `rdma_cm_event` | `param_t` union（`conn` / `ud`） |
| `rdma_addr` | `src_addr_union` / `dst_addr_union` / `addr` |

bindgen 对 union 的 `disable_untagged_union` + blocklist 与 README「nested structure not handled properly」一致（`confirmed`）。

### 6.5 未完全移植的 C 宏

| C 宏 / 行为 | Rust 状态 |
|-------------|-----------|
| `ibv_reg_mr` / `ibv_reg_mr_iova` + `__builtin_constant_p` | 提供 `__ibv_reg_mr` / `__ibv_reg_mr_iova` + `is_access_const` 参数；**无** 与 C 完全等价的宏入口（`TODO` 注释，`confirmed`） |
| `ibv_get_device_list` + `RDMA_STATIC_PROVIDERS` | `__ibv_get_device_list` 当前直接调 `ibv_get_device_list`（静态 provider **TODO**，`confirmed`） |
| `ibv_static_providers` variadic | **未** 实现（`TODO`） |

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **`Send` / `Sync`** | **未** 为任何类型实现；裸指针默认 **不** 自动 `Send`/`Sync` |
| **锁** | bindgen 生成含 `pthread_mutex_t` 的 struct（opaque）；`ibv_create_wq` inline 会 `pthread_mutex_init`（`confirmed` `verbs.rs`） |
| **async** | **无**；`rdma_get_send_comp` / `rdma_get_recv_comp` 为 **阻塞** 循环（poll → notify → `ibv_get_cq_event`） |
| **CQ 通知** | Example 依赖 `rdma_get_*_comp` 内建逻辑，非 Tokio/mio 集成 |
| **数据路径并发** | 与 libibverbs 相同：由应用保证 QP/CQ 并发规则；crate 不额外约束 |

**硬件 / 内核依赖**：真实 RDMA 或 **SoftRoCE（rxe）**；`scripts/run.sh` 创建 `rxe_eth0`（`confirmed`）。

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| **单元 / 布局测试** | **无** |
| **集成测试** | **无** `tests/*.rs`；`cargo test --all` 在 CI 中执行但 crate 无 test 模块（`inferred` 可能仅编译依赖树） |
| **示例** | `server` + `client`：**是** FFI 演示（RDMA CM + post/poll + 手写 `ibv_wc`） |
| **CI 环境** | Ubuntu + `librdmacm-dev` + 子模块 setup + `run.sh`（rxe + examples） |
| **MSRV** | **1.61.0**（`ci.yml` `CI_RUST_TOOLCHAIN`） |
| **fuzz / systest / mock** | **无** |

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `rdma/category-4/rdma-sys/README.md` | 定位：low-level binding；inline/嵌套结构需手写 |
| `rdma/category-4/rdma-sys/Cargo.toml` | 单 crate；`libc` / `memoffset` / `paste`；bindgen 0.59.2 |
| `rdma/category-4/rdma-sys/build.rs` | pkg-config 版本、blocklist、bitfield/constified 枚举、bindgen 选项 |
| `rdma/category-4/rdma-sys/src/bindings.h` | 三头文件聚合 |
| `rdma/category-4/rdma-sys/src/lib.rs` | `include!`、模块树、`deny(warnings)` |
| `rdma/category-4/rdma-sys/src/types.rs` | 手写 `ibv_wc`、`ibv_send_wr`、`rdma_cm_event` 等 |
| `rdma/category-4/rdma-sys/src/verbs.rs` | `ibv_qp_ex` / `ops` / `verbs_context` / `rdma_post_*` inline |
| `rdma/category-4/rdma-sys/src/opcode.rs` | `paste` 生成 opcode 常量 |
| `rdma/category-4/rdma-sys/examples/server.rs` | CM server + recv/send comp |
| `rdma/category-4/rdma-sys/examples/client.rs` | CM client + inline 发送路径 |
| `rdma/category-4/rdma-sys/scripts/run.sh` | SoftRoCE + test + examples |
| `rdma/category-4/rdma-sys/.github/workflows/ci.yml` | Rust 1.61、apt、run.sh |
| `rdma/category-4/rdma-sys/.gitmodules` | `rdma-env-setup` 子模块 |

---

## 10. 架构决策推断

### 10.1 单 crate 纯 `-sys`（无 safe 层）

| 字段 | 内容 |
|------|------|
| **决策** | 仅发布 `rdma-sys`，不拆分 `rdma` safe crate |
| **观察到的 C 侧事实** | verbs + CM 两套 API 面大、生命周期复杂 |
| **观察到的 Rust 侧事实** | 全部 API 为 FFI + `unsafe`；README 定位为「low-level binding」 |
| **推断动机** | 供上层项目（如自研栈）自由封装；避免在 `-sys` 层锁定错误模型与所有权 |
| **证据** | `Cargo.toml`、`README.md`、`lib.rs` |
| **置信度** | `inferred` |

### 10.2 blocklist + `types.rs` 手写 union 结构

| 字段 | 内容 |
|------|------|
| **决策** | bindgen `.blocklist_type` 对 `ibv_wc`、`ibv_send_wr`、`rdma_cm_event` 等，在 `types.rs` 用 `#[repr(C)]` union/struct 重写 |
| **观察到的 C 侧事实** | 上述类型含 **anonymous union**、嵌套 struct；bindgen 对 untagged union 受限（`disable_untagged_union`） |
| **观察到的 Rust 侧事实** | 具名 union 字段（如 `imm_data_invalidated_rkey_union`）；与 C 布局对齐依赖手工维护 |
| **推断动机** | 在稳定编译的前提下仍暴露完整 WR/WC/CM 事件布局供上层零拷贝构造 |
| **证据** | `build.rs` blocklist 列表、`types.rs`、`README.md` |
| **置信度** | `inferred` |

### 10.3 `verbs.rs` 用函数指针复现 C `static inline`

| 字段 | 内容 |
|------|------|
| **决策** | 不启用 `generate_inline_functions(true)`；在 `verbs.rs` 用 `#[inline] unsafe fn` 调用 `ibv_qp_ex` / `ibv_cq_ex` / `ibv_context.ops` / `verbs_context` 字段 |
| **观察到的 C 侧事实** | `verbs.h` 大量 inline（`ibv_wr_send`、`ibv_poll_cq` 等）；扩展 API 经 `verbs_context` 函数表 |
| **观察到的 Rust 侧事实** | ~1300 行手写；`memoffset` 实现 `container_of`；`verbs_get_ctx_op!` 检查 `sz` 与字段是否存在 |
| **推断动机** | bindgen 对 inline 与宏支持不足；手写可与 rdma-core 头文件语义对齐并集中处理 `EOPNOTSUPP` |
| **证据** | `build.rs` 注释掉的 inline 选项、`verbs.rs`、`README.md` |
| **置信度** | `confirmed`（README 明确 inline 问题）+ `inferred`（规模与 op 表策略） |

### 10.4 pkg-config 双库 + 版本下限（无 vendored）

| 字段 | 内容 |
|------|------|
| **决策** | 构建期 **必须** 系统安装 libibverbs / librdmacm，且版本 ≥ 1.8.28 / 1.2.28 |
| **观察到的 C 侧事实** | 头文件与 `.so` 随发行版 rdma-core 发布 |
| **观察到的 Rust 侧事实** | 无 `vendor/rdma-core`、无 cmake 子模块；仅 `pkg_config::probe` |
| **推断动机** | 减小 crate 体积与构建时间；与 datenlord 生态假设「目标机已装 RDMA 栈」一致 |
| **证据** | `build.rs`、`ci.yml` apt |
| **置信度** | `inferred` |

### 10.5 `opcode.rs` + `paste` 复现宏拼接 opcode

| 字段 | 内容 |
|------|------|
| **决策** | 独立模块用 `concat_ibv_opcode!` 生成 `IBV_OPCODE_RC_*` 等常量 |
| **观察到的 C 侧事实** | 头文件用宏将 transport nibble 与 op 拼接 |
| **观察到的 Rust 侧事实** | `paste` 在编译期生成 `pub const` 名与值 |
| **推断动机** | bindgen 不导出 C 宏常量；避免魔数散落 |
| **证据** | `opcode.rs` |
| **置信度** | `inferred` |

### 10.6 同一 `-sys` crate 同时绑定 verbs 与 rdmacm

| 字段 | 内容 |
|------|------|
| **决策** | `bindings.h` 同时 include verbs 与 rdma_cma / rdma_verbs |
| **观察到的 C 侧事实** | CM 层 `rdma_reg_*`、`rdma_post_*` 直接调用 verbs |
| **观察到的 Rust 侧事实** | `verbs.rs` 末尾实现 `rdma_post_send` 等，依赖手写 `ibv_send_wr` |
| **推断动机** | 避免两个 `-sys` crate 间类型重复与链接顺序问题；example 仅依赖 `rdma_sys` 一个 crate |
| **证据** | `bindings.h`、`verbs.rs`、`examples/*.rs` |
| **置信度** | `inferred` |

### 10.7 `#![deny(warnings)]` 与宽松 `allow`

| 字段 | 内容 |
|------|------|
| **决策** | crate 级拒绝 warning，但对 bindgen 命名、缺失 safety doc 等大量 `allow` |
| **观察到的 Rust 侧事实** | `lib.rs` 顶部 `deny` + `allow(non_snake_case, …)` |
| **推断动机** | 在 CI 中强制清洁构建，同时不逐个修复 bindgen 风格问题 |
| **证据** | `src/lib.rs` |
| **置信度** | `inferred` |

---

## 11. 待澄清

- 构建后 **`OUT_DIR/bindings.rs` 完整符号列表** 未枚举；allowlist 实际覆盖的 `ibv_*` / `rdma_*` 函数数量需一次成功 bindgen 才能确认。
- **`links` 缺失** 是否会导致同依赖图中多 crate 重复链接 libibverbs — 需结合 Cargo 链接器行为与上层 workspace 实测。
- C 宏 **`ibv_reg_mr` / `ibv_get_device_list`（静态 provider）** 与 Rust `__ibv_*` 的语义差距在生产静态链接场景下是否可接受。
- **`ibv_wc_read_invalidated_rkey`** 实现调用 `read_imm_data` 函数指针（与 C 头 `#ifdef __CHECKER__` 分支），是否与目标 rdma-core 版本完全一致需对照头文件。
- **`types.rs` 无布局测试**；上游头文件升级时的 ABI 回归风险。
- CI 仅 apt **`librdmacm-dev`** 时，**libibverbs-dev** 是否恒被依赖拉取 — 干净镜像上需验证。
- 子模块 **`rdma-env-setup`** 在样本未 `git submodule update` 时内容未知；`setup.sh` 具体安装项未在本分析中展开。
- Example **资源泄漏路径**（错误分支未 `rdma_destroy_ep` / `ibv_dereg_mr`）是否仅为演示简化，不影响库契约。
