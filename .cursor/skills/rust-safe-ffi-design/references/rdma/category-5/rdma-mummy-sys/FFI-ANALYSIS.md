# rdma-mummy-sys — FFI 单项目分析

**分析对象（源码路径）：** `rdma/category-5/rdma-mummy-sys/`  
**计划序号：** P10（阶段 B6）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）；C 头与 mummy 实现行为参考上游 `RDMA-Rust/rdma-core-mummy` 公开源码（样本树内 `rdma-core-mummy` 子模块未检出）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`rdma-mummy-sys`（crates.io：`rdma-mummy-sys`）是面向 **RDMA 生态下游 crate** 的 **纯 `-sys` 层**：通过 bindgen 绑定 **libibverbs** 与 **librdmacm** 的 C API 表面，并 **静态链接** vendored 的 **rdma-core-mummy**（非完整 rdma-core 源码树）。mummy 在进程启动时用 `dlopen` 加载系统 `libibverbs.so.1` / `librdmacm`；若运行时无真实 RDMA 用户态库，包装函数通过 `RETURN_NOT_EXIST` 将 **`errno` 设为 `EOPNOTSUPP`** 并返回错误码/空指针，使 **CI 与无硬件环境仍能完成链接与编译**，而数据路径调用在运行时会失败。Rust 侧 **无 safe 封装 crate**；`static inline`、含 union 的结构体与 `verbs_context` 分发逻辑大量落在手写模块 `verbs.rs` / `types.rs`。仓库提供 **`client` / `server`** 两个 RDMA CM 示例（默认端口 7471），演示经 `rdma_*` 与手写 inline 完成连接与收发。

### 库画像（§6 字段）

| 字段 | 内容 |
|------|------|
| **C 库名称与角色** | **libibverbs**（verbs 控制/数据面）+ **librdmacm**（连接管理）；由 **rdma-core-mummy** 提供可静态链接的包装符号，运行时再解析真实实现 |
| **API 形态** | Opaque `ibv_*` / `rdma_cm_*` 指针；`ibv_context.ops` 函数表；`ibv_qp_ex` / `ibv_cq_ex` 扩展路径；`ibv_send_wr` / `ibv_wc` 等含 **union**；大量 **static inline** 在 C 头中 |
| **资源对** | 与上游 verbs 一致，如 `ibv_get_device_list` ↔ `ibv_free_device_list`、`ibv_open_device` ↔ `ibv_close_device`、`ibv_reg_mr` ↔ `ibv_dereg_mr`、`rdma_create_ep` ↔ `rdma_destroy_ep` 等（由调用方配对，本 crate 不封装 RAII） |
| **线程安全** | 本 crate **未** 声明 `Send`/`Sync`；真实 libibverbs 在已加载时通常按 context 序列化使用（`inferred`） |
| **分发方式** | **Vendored cmake** 构建 `rdma-core-mummy` → **`cargo:rustc-link-lib=static=ibverbs`** + **`static=rdmacm`**；**无** `links =`、**无** `pkg-config` |
| **绑定难点** | bindgen 对 union / 部分结构体不稳定 → blocklist + 手写；`ibv_query_port` ABI 兼容 → bindgen 重命名 + Rust `ibv_query_port` inline 回退；1300+ 行 `verbs.rs` 复刻 C inline |

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| `rdma-mummy-sys` | 根目录（单 crate） | 唯一库：`build.rs` + bindgen + 手写 bindings |

**无** workspace 多成员、**无** 独立 safe / async crate（`confirmed`，根 `Cargo.toml` 仅 `[package]`）。

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `build.rs` | cmake 构建 `rdma-core-mummy`、bindgen、`CompatParseCallback` |
| `src/lib.rs` | `include!(OUT_DIR/bindings.rs)`；`mod opcode/types/verbs`；`#![deny(warnings)]` |
| `src/bindings.h` | `#include <infiniband/verbs.h>`、`<infiniband/driver.h>`、`<rdma/rdma_cma.h>` |
| `src/types.rs` | blocklist 类型的手写 `#[repr(C)]`（含 `ibv_wc`、`ibv_send_wr`、`rdma_cm_event` 等） |
| `src/verbs.rs` | C `static inline` 与 `verbs_context` 分发（约 1300 行） |
| `src/opcode.rs` | `IBV_OPCODE_*` 宏常量（`paste` 生成） |
| `rdma-core-mummy/` | git 子模块 → `https://github.com/RDMA-Rust/rdma-core-mummy.git` |
| `examples/client.rs` / `examples/server.rs` | RDMA CM TCP 模式 client/server |
| `scripts/run.sh` | SoftRoCE（rxe）+ `cargo test` + 跑 examples |
| `.github/workflows/ci.yml` | checkout 子模块、`apt` 装 cmake/gcc、`cargo build` |

### 1.3 Examples（简单阅读）

| 示例 | 文件 | 是否演示 FFI |
|------|------|----------------|
| `server` | `examples/server.rs` | **是** — 直接 `use rdma_mummy_sys::*`：`rdma_getaddrinfo`、`rdma_create_ep`、`rdma_listen`、`rdma_get_request`、`ibv_query_qp`、`rdma_reg_msgs`、`rdma_post_*`、`rdma_get_*_comp` |
| `client` | `examples/client.rs` | **是** — 同上；`rdma_connect`、命令行传入 server IP/端口 |

两者均为 **unsafe 裸调 C API**，无 safe 中间层；默认端口 **7471**（`confirmed` 自 example 注释与 `server` 中 `PORT`）。

### 1.4 Tests

| 类型 | 位置 | 说明 |
|------|------|------|
| `#[test]` / `tests/` | **无** | 全库未检出单元测试 |
| CI | `.github/workflows/ci.yml` | 仅 **`cargo build`**（绑定生成 + 静态链 mummy），**不**跑 `cargo test` 或 examples |
| 本地集成 | `scripts/run.sh` | 需以太网口 + **rxe** SoftRoCE；`cargo test --all` 后后台 server、前台 client |

---

## 2. 原生依赖

### 2.1 链接策略

| 项 | 内容 |
|----|------|
| **`links` 键** | **无**（`Cargo.toml` 未声明 `links = "ibverbs"` 等） |
| **构建** | `cmake::Config::new("rdma-core-mummy")`，`RelWithDebInfo`，`build_target("all")`（`confirmed`，`build.rs`） |
| **链接** | `cargo:rustc-link-search=native={build}/build`；**`static=ibverbs`**、**`static=rdmacm`** |
| **头文件** | bindgen `-I./rdma-core-mummy/include`；子模块提供与 rdma-core 兼容的 `infiniband/*.h`、`rdma/*.h` |

### 2.2 rdma-core-mummy 运行时模型

| 阶段 | 行为 |
|------|------|
| **链接时** | 可执行文件/测试二进制 **静态嵌入** mummy 中的包装符号 |
| **加载时** | mummy 构造函数 `dlopen("libibverbs.so.1", …)`（及 rdmacm 对应库）；`dlsym` 填充函数指针（`inferred` 自上游 `src/ibverbs.c`、`utility.h`） |
| **无系统 RDMA 库时** | `FUNC_PTR` 为空 → `RETURN_NOT_EXIST` 设 **`errno = EOPNOTSUPP`** 并返回 `EOPNOTSUPP` / `NULL` / `-EOPNOTSUPP`（`confirmed` 自上游 `utility.h` 宏与 wrapper 默认值） |
| **有系统库时** | 转发至真实 `ibv_*` / `rdma_*` 实现（`confirmed` 自上游 README） |

说明：这与「完整 vendored rdma-core 静态库」不同——mummy **不**捆绑整套 rdma-core 实现，而是 **薄包装 + 动态解析**（`confirmed` 自 `rdma-core-mummy` README）。

### 2.3 子模块与 CI 主机

- `.gitmodules`：`rdma-core-mummy` → `RDMA-Rust/rdma-core-mummy`，分支 `main`。
- CI：Ubuntu，`submodules: true`，安装 **cmake、gcc**，**不**安装 `libibverbs-dev`（`confirmed` 自 `ci.yml`）——依赖 mummy 使 **无开发包环境仍可 `cargo build`**。
- 样本检出时 **`rdma-core-mummy/` 目录为空**，本分析未对照子模块内具体符号表。

### 2.4 Feature

| Feature | 作用 |
|---------|------|
| （无 `[features]`） | 单一路径：始终 cmake + 静态 mummy + bindgen |

### 2.5 Rust 依赖

| Crate | 用途 |
|-------|------|
| `libc` | `errno`、`pthread`、`sockaddr` 等与 C 互操作 |
| `memoffset` | `verbs.rs` 中 `container_of!` 宏（`verbs_context`） |
| `paste` | `opcode.rs` 生成 `IBV_OPCODE_RC_*` 等常量 |

---

## 3. 绑定生成

### 3.1 方式：**bindgen + 大量手写混合**

| 组件 | 机制 |
|------|------|
| **生成** | `build.rs` 对 `src/bindings.h` 运行 bindgen → `OUT_DIR/bindings.rs`，`lib.rs` `include!` |
| **手写类型** | `types.rs`：bindgen **blocklist** 的 union/复杂结构 |
| **手写函数** | `verbs.rs`：C 头中的 **inline**、provider 扩展路径 |
| **手写常量** | `opcode.rs`：头文件宏 `IBV_OPCODE_*` |
| **手写常量（链路层）** | `verbs.rs` 末尾 `IBV_LINK_LAYER_*`（CHANGELOG：`confirmed` 匿名 enum 需手动生成） |

### 3.2 bindgen 策略（摘要）

| 配置 | 值 |
|------|-----|
| Header | `src/bindings.h`（含 `driver.h` 以暴露 `verbs_context` 等） |
| Include | `-I./rdma-core-mummy/include` |
| Allowlist | 函数 `ibv_.*`、`_ibv_.*`、`rdma_.*`；类型 `ibv_.*`、`rdma_.*`、`verbs_.*`、`ib_uverbs_access_flags` |
| Blocklist 类型 | `in6_addr`、`sockaddr*`、`timespec`、`ibv_ah_attr`、`ibv_async_event`、`ibv_flow_spec`、`ibv_gid*`、`ibv_*_wr`、`ibv_wc`、`rdma_addr`、`rdma_cm_event`、`rdma_ib_addr`、`rdma_ud_param` 等 |
| Opaque | `pthread_.*` |
| 枚举 | 大量 `bitfield_enum`；多组 `constified_enum_module`；`ibv_event_type` → `rustified_enum` |
| 其它 | `prettyplease` formatter；`size_t_is_usize`；`disable_untagged_union`；`derive_copy(true)`（flow spec 相关）；`derive_debug/default(false)` |
| **ParseCallbacks** | **`CompatParseCallback`**：将 C 符号 **`ibv_query_port`** 生成为 Rust **`ibv_query_port_compat`** |

### 3.3 `ibv_query_port` 兼容链

| 层 | 行为 |
|----|------|
| **C mummy** | `#undef ibv_query_port` 后包装 `query_port`，参数为 `_compat_ibv_port_attr *`（`inferred` 自上游 `ibverbs.c`） |
| **bindgen** | 导出 `ibv_query_port_compat`，避免与 Rust 侧 `pub unsafe fn ibv_query_port` 冲突 |
| **Rust `verbs.rs`** | 公开 `ibv_query_port`：若存在 `verbs_context` 且 `query_port` op → 调 provider；否则 **`ibv_query_port_compat`**（`confirmed` 自 `verbs.rs`） |

CHANGELOG v0.2.0 记录引入 `_compat` 后缀（`confirmed`）。

### 3.4 C 头暴露的主要 API 形态（`bindings.h` 所 include）

| 类别 | 形态 |
|------|------|
| **设备/上下文** | `ibv_get_device_list` → `ibv_open_device` → `ibv_close_device`；`ibv_context` + **`ibv_context_ops`** |
| **扩展上下文** | `verbs_context`（`driver.h`），`abi_compat == usize::MAX` 时经 `container_of` 取扩展 ops（`confirmed` 自 `verbs.rs`） |
| **内存/QP/CQ** | `ibv_reg_mr`、`ibv_create_qp`、`ibv_create_cq`、`ibv_post_send`/`recv`（Rust 经 `ops` 指针） |
| **扩展 verbs** | `ibv_qp_ex` / `ibv_cq_ex` 上函数指针；Rust `ibv_wr_*`、`ibv_start_poll` 等 inline |
| **RDMA CM** | `rdma_cm_id`、`rdma_getaddrinfo`、`rdma_create_ep`、`rdma_connect`；`rdma_cm_event` 含 **union `param`** |
| **错误习惯** | 多数返回 `c_int`，失败设 **`errno`**；指针 API 失败常返回 `NULL` |

### 3.5 再生流程

1. 更新 `rdma-core-mummy` 子模块（头文件/包装符号）。  
2. 调整 `build.rs` allowlist/blocklist（若 API 增减）。  
3. 同步 `types.rs` / `verbs.rs` 手写部分。  
4. `cargo build` 触发 bindgen（无提交 `OUT_DIR` 快照到 git）。

---

## 4. 分层与公开 API

### 4.1 Crate 分层

```
应用 / 上层 rdma-* crate
        │
        ▼  unsafe 直接调用
┌───────────────────┐
│  rdma-mummy-sys   │  ← 本仓库唯一 crate（纯 -sys）
│  bindings.rs      │     bindgen 生成
│  types / verbs    │     手写补全
│  opcode           │     宏常量
└─────────┬─────────┘
          │ static link
          ▼
┌───────────────────┐
│ rdma-core-mummy   │  ibverbs.a + rdmacm.a
│ (dlopen @ runtime)│
└─────────┬─────────┘
          │ 若存在
          ▼
   libibverbs.so.1 / librdmacm
```

**无上层适配 crate** — §7.2 专节：**N/A — 本仓库仅发布 `-sys`，safe/async 由依赖方（如 RDMA-Rust 其它 crate）自行实现。**

### 4.2 公开模块树（逻辑）

| 模块/导出 | 内容 |
|-----------|------|
| `include!` 生成项 | 大量 `ibv_*` / `rdma_*` 函数与类型、`verbs_*` 结构 |
| `opcode` | `ibv_opcode` 子模块常量 |
| `types` | 手写 `ibv_wc`、`ibv_send_wr`、`ibv_gid`、`rdma_cm_event` 等 |
| `verbs` | 手写 inline、`rdma_post_*`、`rdma_reg_*`、`ibv_query_port` 等 |

`lib.rs` 通过 `pub use` 扁平再导出（`confirmed`）。

### 4.3 典型调用面（机制级）

| 场景 | 入口符号（示例） |
|------|------------------|
| 列设备 | `ibv_get_device_list` / `ibv_free_device_list` |
| 建连（CM） | `rdma_getaddrinfo` → `rdma_create_ep` → `rdma_connect` / `rdma_listen` |
| 注册内存 | `ibv_reg_mr` 或 inline `rdma_reg_msgs` |
| 收发 | `ibv_post_send` / `ibv_post_recv` 或 `rdma_post_send` / `rdma_post_recv` |
| 完成事件 | `ibv_poll_cq`、`rdma_get_send_comp` / `rdma_get_recv_comp` |

---

## 5. 资源与生命周期

本 crate **不提供** `Drop`、RAII 或 Rust 所有权类型；所有 `ibv_*` / `rdma_*` 资源由调用方按 C API 契约管理。

| 资源 | 创建 | 释放 | 备注 |
|------|------|------|------|
| 设备列表 | `ibv_get_device_list` | `ibv_free_device_list` | 指针切片由 C 分配 |
| 上下文 | `ibv_open_device` | `ibv_close_device` | |
| PD / MR / CQ / QP | `ibv_alloc_pd`、`ibv_reg_mr`、`ibv_create_cq`、`ibv_create_qp` 等 | 对应 `dealloc`/`dereg`/`destroy` | mummy 未加载时可能得 `NULL` + `EOPNOTSUPP` |
| CM 端点 | `rdma_create_ep` | `rdma_destroy_ep` | examples 路径 |
| WR 链表 | 栈上或调用方分配 | post 返回后可复用（inline 发送除外见 C 注释） | `ibv_send_wr` 在 `types.rs` 手写 |

**泄漏风险点（调用方责任）：**

- examples 中部分错误路径未展示完整 `dereg_mr` / `destroy_ep` 清理（`inferred` 自 example 结构，非库缺陷）。
- `verbs.rs` 中 `ibv_create_wq` 成功时会 **`pthread_mutex_init`** — 须与 C 侧 `destroy_wq` 配对（`confirmed` 自 `verbs.rs`）。
- 无 Rust 类型系统防止「双重 free」或 use-after-free。

---

## 6. 错误与安全边界

### 6.1 错误模型

| 机制 | 说明 |
|------|------|
| **返回值** | `c_int`：0 成功，负值失败；部分 API 返回指针，`NULL` 失败 |
| **`errno`** | mummy `RETURN_NOT_EXIST`、部分 inline（如 `ibv_create_flow`）显式写 **`EOPNOTSUPP`** |
| **无系统库 / 无硬件** | 静态链接仍成功；运行时 verbs 调用多返回 **`EOPNOTSUPP`** 或空指针（`confirmed` 自 mummy `utility.h` + 用户场景描述） |
| **Rust `Result`** | **无** — 调用方自行映射 |

### 6.2 `unsafe` 边界

| 区域 | 契约 |
|------|------|
| **整个 crate** | 几乎所有公开函数为 `unsafe fn` 或需在 `unsafe` 块中调用 C ABI |
| **`verbs.rs` inline** | 调用方保证 QP/CQ/指针有效；`unwrap_unchecked` 用于热路径 `ops`/`qp_ex` 函数指针（CHANGELOG v0.2.2 `confirmed`） |
| **`rdma_post_recv`（inline）** | `assert!` 检查 `addr` 落在 MR 范围内 |
| **`types.rs` union** | `ibv_wc.imm_data_invalidated_rkey_union` 等须按 `wc_flags` 解释（与 man page 一致，`inferred`） |

### 6.3 安全相关 lint

- `#![deny(warnings)]` 于 `lib.rs`（`confirmed`）。
- 允许 `non_snake_case`、`missing_safety_doc` 等 FFI 常见豁免。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **`Send` / `Sync`** | **未** 为任何句柄实现；裸 `*mut ibv_*` 默认 !Send/!Sync |
| **锁** | 手写 `ibv_create_wq` 初始化 `pthread_mutex_t` / `cond`；其余依赖 C 库 |
| **async** | **无** Tokio/async 集成 |
| **CQ 等待** | examples 使用 `rdma_get_*_comp` 内联逻辑（`ibv_poll_cq` + `ibv_get_cq_event` 循环），同步阻塞 |
| **线程模型** | 库不约束；真实 libibverbs 已加载时遵循上游 per-context 惯例（`inferred`） |

**硬件 / 内核依赖：**

- **编译/CI**：无需 RDMA 设备、无需 `libibverbs-dev`（mummy 静态链）。
- **功能/示例**：需系统 **运行时** `libibverbs.so.1` 等；`scripts/run.sh` 另需 **SoftRoCE（rxe）** 与以太网口（`confirmed` 自 `scripts/run.sh`）。

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| **单元测试** | **无** |
| **CI** | `cargo build` only；失败时可选 tmate 调试（`confirmed` 自 `ci.yml`） |
| **本地脚本** | `scripts/run.sh`：`rdma link add … type rxe`，`cargo test --all`，`server` + `client` |
| **示例** | `client` / `server`：**演示完整 RDMA CM + verbs FFI**；需真实/SoftRoCE 环境与运行时 rdma 库 |
| **fuzz / systest / mock Rust 层** | **无**；「mock」体现在 **C mummy 包装**，非 Rust mock 框架 |

**Examples 结论：** 二者均为 **直连 `rdma_mummy_sys` 的 FFI 演示**，适合作为上层 safe crate 的参考调用序列；在 **仅 CI build、无 rxe** 的机器上运行 example **预期失败**（`EOPNOTSUPP` 或 CM 错误，`inferred`）。

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `rdma/category-5/rdma-mummy-sys/Cargo.toml` | 单 crate、依赖 `libc`/`memoffset`/`paste`、无 `links` |
| `rdma/category-5/rdma-mummy-sys/build.rs` | cmake mummy、静态 `ibverbs`/`rdmacm`、bindgen 策略、`CompatParseCallback` |
| `rdma/category-5/rdma-mummy-sys/src/lib.rs` | `include!` 绑定、模块划分、`deny(warnings)` |
| `rdma/category-5/rdma-mummy-sys/src/bindings.h` | 所绑 C 头范围（verbs + driver + rdma_cma） |
| `rdma/category-5/rdma-mummy-sys/src/types.rs` | union/复杂结构手写 |
| `rdma/category-5/rdma-mummy-sys/src/verbs.rs` | inline、`ibv_query_port` 兼容、`EOPNOTSUPP` 回退路径 |
| `rdma/category-5/rdma-mummy-sys/src/opcode.rs` | `IBV_OPCODE_*` 宏移植 |
| `rdma/category-5/rdma-mummy-sys/examples/client.rs` | client 侧 CM + post + poll FFI 流程 |
| `rdma/category-5/rdma-mummy-sys/examples/server.rs` | listen/accept 侧 FFI 流程 |
| `rdma/category-5/rdma-mummy-sys/.gitmodules` | `rdma-core-mummy` 子模块 URL |
| `rdma/category-5/rdma-mummy-sys/.github/workflows/ci.yml` | CI 仅 build、子模块、无 libibverbs-dev |
| `rdma/category-5/rdma-mummy-sys/scripts/run.sh` | SoftRoCE + examples 集成脚本 |
| `rdma/category-5/rdma-mummy-sys/CHANGELOG.md` | `_compat`、`unwrap_unchecked`、link layer 等变更记录 |
| `RDMA-Rust/rdma-core-mummy` `src/utility.h` | `RETURN_NOT_EXIST` → `errno = EOPNOTSUPP` |
| `project-scope.md` | 单项目 12 章范围、静态分析、无跨项目对比 |

---

## 10. 架构决策推断

### 10.1 采用 rdma-core-mummy 静态链而非系统 `pkg-config`

| 字段 | 内容 |
|------|------|
| **决策** | `build.rs` 始终 cmake vendored mummy，**强制 `static=ibverbs` + `static=rdmacm`** |
| **观察到的 C 侧事实** | 多数发行版不提供 rdma-core **静态库**；完整静态 rdma-core 体积大且受 `IBVERBS_PRIVATE` ABI 影响 |
| **观察到的 Rust 侧事实** | 无 `pkg-config` 探测分支；CI 不装 `-dev` 包仍可 build |
| **推断动机** | 让依赖方 **二进制可静态链接符号**，同时在有系统 `.so` 时 **运行时转发**；CI/docs 无 RDMA 开发包也能生成绑定 |
| **证据** | `build.rs`、`ci.yml`；上游 mummy README |
| **置信度** | `inferred` |

### 10.2 bindgen + `CompatParseCallback` 处理 `ibv_query_port`

| 字段 | 内容 |
|------|------|
| **决策** | bindgen 生成 `ibv_query_port_compat`；Rust 提供语义完整的 `ibv_query_port` inline |
| **观察到的 C 侧事实** | mummy 用 `_compat_ibv_port_attr` 适配旧/new 端口属性布局 |
| **观察到的 Rust 侧事实** | `ParseCallbacks::generated_name_override` 仅重命名此一函数；`verbs.rs` 分支 `verbs_context.query_port` |
| **推断动机** | 避免 bindgen 与手写同名冲突；保留与 C 头一致的调用名供下游使用 |
| **证据** | `build.rs`、`verbs.rs`、CHANGELOG v0.2.0 |
| **置信度** | `confirmed`（CHANGELOG）+ `inferred`（动机） |

### 10.3 大块 blocklist + `types.rs` / `verbs.rs` 手写

| 字段 | 内容 |
|------|------|
| **决策** | union 与 inline 不从 bindgen 生成，分文件手写 |
| **观察到的 C 侧事实** | `ibv_wc`、`ibv_send_wr`、`rdma_cm_event` 等含 union；verbs.h 大量 **static inline** |
| **观察到的 Rust 侧事实** | `types.rs` ~300 行；`verbs.rs` ~1300 行；README 写明 inline/嵌套结构需 case-by-case |
| **推断动机** | bindgen 对 union/bitfield/inline 支持受限；与 RDMA-Rust 其它 `-sys` crate 维护模式一致（仅描述本项目） |
| **证据** | `README.md`、`build.rs` blocklist、`types.rs`、`verbs.rs` |
| **置信度** | `inferred` |

### 10.4 运行时 `EOPNOTSUPP` 作为「无 RDMA 环境」可观测失败

| 字段 | 内容 |
|------|------|
| **决策** | 依赖 mummy 在 `dlopen`/`dlsym` 失败时不崩溃，而以 **`EOPNOTSUPP`** 失败 |
| **观察到的 C 侧事实** | `RETURN_NOT_EXIST` 统一设 errno 并返回 |
| **观察到的 Rust 侧事实** | CI 只验证 **编译**；无 Rust 层 mock；examples 假定真实栈 |
| **推断动机** | **测试友好/CI 友好**：无硬件、无 `libibverbs.so` 的机器仍能编译；运行期失败可预测，便于上层 crate  feature-gate 或跳过集成测试 |
| **证据** | 上游 `utility.h`；`ci.yml`；`scripts/run.sh` 对比 |
| **置信度** | `inferred` |

### 10.5 仅发布 `-sys`、不提供 safe 层

| 字段 | 内容 |
|------|------|
| **决策** | 单 crate 扁平导出 FFI，无 `Arc`/RAII |
| **观察到的 C 侧事实** | API 均为指针 + 调用方管理生命周期 |
| **观察到的 Rust 侧事实** | 无第二 member crate；examples 全 `unsafe` |
| **推断动机** | 作为 **RDMA-Rust 工具链底层** 供多个上层库共用；避免在 `-sys` 中重复 safe 设计 |
| **证据** | `Cargo.toml`、`examples/*` |
| **置信度** | `inferred` |

### 10.6 热路径 `unwrap_unchecked` on `ops` 函数指针

| 字段 | 内容 |
|------|------|
| **决策** | `ibv_poll_cq`、`ibv_post_send` 等对 `context.ops.*` 使用 `unwrap_unchecked()` |
| **观察到的 C 侧事实** | 合法 `ibv_context` 在成功 `open_device` 后 ops 应非空 |
| **观察到的 Rust 侧事实** | CHANGELOG v0.2.2 明确 data path 优化 |
| **推断动机** | 减少分支开销；假定调用方已保证 context 有效（与 C inline 假设一致） |
| **证据** | `verbs.rs`、`CHANGELOG.md` |
| **置信度** | `confirmed`（CHANGELOG）+ `inferred`（契约） |

---

## 11. 待澄清

- 样本树内 **`rdma-core-mummy` 子模块未检出**，未核对当前子模块版本与 bindgen 生成差异列表。
- mummy 除 `libibverbs.so.1` 外对 **rdmacm** 的 `dlopen` 符号表、以及 **ibumad/mlx5** 静态库是否在 `build.rs` 中链接但未使用 — 未读子模块 CMake 全量目标。
- **`cargo test --all`** 在无测试用例时是否仅为空跑 — 未执行构建验证。
- examples 在 **仅有 mummy、无系统 `.so`** 时的具体失败点（`rdma_getaddrinfo` vs `ibv_open_device`）未逐项静态推演。
- 是否计划像其它 RDMA-Rust crate 一样增加 **提交 bindgen 快照** 以支持 docs.rs 无 cmake 环境 — 当前 `build.rs` 未见 `docs.rs` 特殊分支。
- `verbs.rs` TODO：`ibv_get_device_list` / `ibv_reg_mr` 等 C 侧 `__ibv_*` 重定义是否需在 Rust 再包一层。
- **无 `links` 键** 对 Cargo 本地依赖解析/重复链接的影响 — 依赖方是否需自行声明 `links`。

---

## 完成定义自检（project-scope §8）

- [x] §0–§11 共 12 章均存在  
- [x] 分析对象为 `rdma/category-5/rdma-mummy-sys/`（P10）  
- [x] 静态阅读；未执行 `cargo build` / 硬件实测  
- [x] 无跨项目对比  
- [x] §10 架构决策推断含 C 头 / mummy / Rust 分层依据  
- [x] Examples 简单阅读（client/server）  
- [x] §9 证据索引 8–15 条量级  
