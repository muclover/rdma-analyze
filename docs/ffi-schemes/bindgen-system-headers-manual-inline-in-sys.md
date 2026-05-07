# FFI 方案：Bindgen + 系统头文件 + 在 `-sys` 层手写 inline 包装

**代表项目：** [datenlord/rdma-sys](https://github.com/datenlord/rdma-sys)（被 [datenlord/async-rdma](https://github.com/datenlord/async-rdma) 依赖）

## Bindgen：是否使用、怎么用

| 项 | 说明 |
|----|------|
| **是否使用** | **是**。`build.rs` 中用 **`bindgen::Builder`** 生成 **`OUT_DIR/bindings.rs`**。 |
| **头文件从哪里来** | **`pkg-config`** 探测 **`libibverbs`**、**`librdmacm`**（版本下限写在 `build.rs`），把所有 **`include_paths`** 转成 **`clang_arg("-I...")`**；再包含 **`src/bindings.h`**（内部 `#include <infiniband/verbs.h>`、`rdma/rdma_cma.h` 等）。 |
| **典型 bindgen 选项** | 对 **`ibv_*` / `rdma_*` / `verbs_*`** 做 **allowlist**；对 **`ibv_send_wr`、`ibv_wc`、`sockaddr*`** 等 **`blocklist_type`**；大量 **`bitfield_enum`** / **`constified_enum_module`**；**`disable_untagged_union()`** 等，与工作区 `rdma-sys/build.rs` 一致。 |
| **产物接入方式** | `src/lib.rs` **`include!(concat!(env!("OUT_DIR"), "/bindings.rs"))`**，并与手写模块 **`pub use`**。 |

bindgen 同样 **不生成** `verbs.h` 里 **`static inline`** 的可调用 Rust 实现；本项目选择在 **Rust 手写**（见下节「bindgen 之外」）。

## bindgen 之外还做了什么

| 工作 | 说明 |
|------|------|
| **`verbs.rs`（手写 inline）** | 用 **`#[inline] pub unsafe fn`** 实现与 **`verbs.h` inline** 等价的 **`ibv_post_send`、`ibv_poll_cq`、`ibv_wr_*`** 等，内部通过 **`(*qp)` / `(*cq)` / `(*ctx)`** 上的 **函数指针** 派发（见工作区 `rdma-sys/src/verbs.rs`）。 |
| **`types.rs`（手写布局）** | 对被 blocklist 的 **`ibv_send_wr`、`ibv_wc`** 等 **手写 `#[repr(C)]`**，避免 bindgen 对 **union** 生成不可用或不稳定的 Rust。 |
| **`opcode.rs` 等** | 与 verbs  Opcode / 常量相关的辅助模块，配合手写调用约定。 |
| **链接** | 通过 pkg-config 探测到的库信息，由构建脚本/链接器最终 **`libibverbs` + `librdmacm` 动态链接**（与上游惯例一致）。 |
| **上层 `async-rdma`** | **不再跑一层 bindgen**；直接使用 **`rdma-sys`** 提供的 FFI + inline 包装，再做异步与安全抽象。 |

## 分层

| Crate | 角色 |
|--------|------|
| `rdma-sys` | **单一 `-sys` crate**：bindgen 生成大部分符号；**手动 `verbs.rs`** 实现 `ibv_*` / `ibv_wr_*` 等与 **`verbs.h` static inline 等价**的 `#[inline] pub unsafe fn`，内部调用 `(*cq).poll_cq.unwrap()` 等 |
| `async-rdma` | 在 `rdma-sys` 之上的异步与安全抽象（Tokio 等），**不再复制一层 FFI** |

## 补充：`libibverbs` / `librdmacm` 版本下限

以工作区 **`rdma-sys/build.rs`** 为准（当前可见为 **libibverbs ≥ 1.8.28**、**librdmacm ≥ 1.2.28**）；不满足则 **`pkg-config` 探测失败**，绑定阶段不会成功。

## static inline 的处理方式

与 rust-ibverbs「把 inline 语义放到上层 safe crate」不同，此处 **把等价实现集中在 `rdma-sys/src/verbs.rs`**，使 **`rdma-sys` 单独即可调用 `ibv_post_send`、`ibv_poll_cq` 等**（见工作区 `rdma-sys/src/verbs.rs` 大量 `#[inline] pub unsafe fn`）。

## 与 async-rdma 的关系

用户提到的「一层 sys + 一层更易用更安全」指的就是：**FFI 与 inline 补齐全部在 `rdma-sys`**，`async-rdma` 专注业务抽象与异步运行时集成。

## 优缺点摘要

| 优点 | 缺点 |
|------|------|
| `-sys` 自给程度高，上层crate省心 | **编译期必须**安装 `libibverbs-dev`、`librdmacm-dev` |
| 同时覆盖 **ibverbs + rdmacm** | 上游仓库近年迭代放缓时，绑定要自己 fork 跟进 |
| 手写类型与 inline 与 rdma-mummy 路线相比更直观（纯 Rust） | union / 新 API 仍要持续手工维护 |

## 小结一句

典型的 **「系统头文件 + bindgen + `-sys` 内手写 inline + 手写疑难类型」**；和 DatenLord 自己的异步封装 **`async-rdma` 是清晰的两层（sys → safe/async）**。
