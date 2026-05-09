# FFI 方案：Bindgen + 系统头文件 + 在 `-sys` 层手写 inline 包装

**代表项目：** [datenlord/rdma-sys](https://github.com/datenlord/rdma-sys)（被 [datenlord/async-rdma](https://github.com/datenlord/async-rdma) 依赖）

---

## 方案定位

这条路线是 Rust 社区里最接近 **「教科书式 `-sys`：bindgen + 系统动态库」** 的一种，但对 **libibverbs 特有的 `static inline` + `ops` 表**做了明确补强：**在 `rdma-sys` 内部用 Rust 手写与头文件等价的派发函数**，使用户 **`use rdma_sys::verbs::*` 即可获得 `ibv_post_send` / `ibv_poll_cq` / `ibv_wr_*` 等可调用的 `unsafe fn`**，而不必再依赖第二个 crate 来「补verbs」。

同时 **`rdma-sys` 一体覆盖 `libibverbs` + `librdmacm`**（通过 pkg-config），上层 **`async-rdma`** 专注 **异步语义与更高层抽象**，**不再重复 FFI**。

---

## 架构总览

```text
    ┌──────────────────────────────────────────────────────────┐
    │              async-rdma（异步 / 业务抽象）                  │
    │   Connection、MR 池、与 Tokio 集成等                       │
    │   依赖 rdma-sys 的 FFI + 手写 verbs，不自建 bindgen        │
    └───────────────────────────┬──────────────────────────────┘
                                │
                                ▼
    ┌──────────────────────────────────────────────────────────┐
    │                    rdma-sys（单一 -sys）                   │
    │  ┌────────────────────┐    ┌──────────────────────────┐  │
    │  │ bindings.rs        │    │ verbs.rs（手写 inline）   │  │
    │  │ bindgen 生成        │    │ ibv_post_send, poll_cq,   │  │
    │  │ allowlist/blocklist │    │ ibv_wr_* → ops 指针派发    │  │
    │  └────────────────────┘    └──────────────────────────┘  │
    │  ┌────────────────────┐    ┌──────────────────────────┐  │
    │  │ types.rs 等         │    │ opcode / rdmacm 模块      │  │
    │  │ blocklist 布局手写   │    │                          │  │
    │  └────────────────────┘    └──────────────────────────┘  │
    └───────────────────────────┬──────────────────────────────┘
                                │ extern "C" + 动态链接
                                ▼
              libibverbs.so + librdmacm.so（系统 rdma-core）
```

**设计重心：** **FFI 与「verbs.h 语义补齐」在同一 crate 内闭环**。任何只需裸调 verbs/rdmacm 的项目可以 **只依赖 `rdma-sys`**，架构上与「sys 薄、verbs 全在上层」的 jonhoo 路线形成对照。

---

## FFI 边界与设计抉择

| 组件 | 职责 |
|------|------|
| **`bindings.rs`** | bindgen 根据 **系统头文件** 生成的函数与类型；复杂/union 类型常 **blocklist** |
| **`types.rs`（等）** | 对被 blocklist 的结构（如 **`ibv_send_wr`、`ibv_wc`**）手写 **`#[repr(C)]`**，保证 ABI 与文档化的 C 布局一致 |
| **`verbs.rs`** | **`#[inline] pub unsafe fn`**，实现与 **`verbs.h` static inline** 等价的 **`ibv_*` / `ibv_wr_*`**，内部统一走 **`(*handle).context.ops....unwrap()(…)`** 模式 |
| **链接** | pkg-config 探测到的 **`libibverbs` + `librdmacm`**，动态链接 |

**权衡：** **编译环境必须安装开发包**（`-dev`）；升级发行版或 rdma-core 大版本时，**手写模块可能与上游头文件漂移**，需要维护者跟进 blocklist 与布局。

---

## 热点路径如何闭合（概念说明）

C 侧：`ibv_poll_cq` / `ibv_post_send` 往往不对应「单一可在链接期绑死的 `.so` 符号」，而是 **内联展开后加载 `ops` 函数指针**。Rust 侧若只有 bindgen 的 `extern "C"` 声明而没有实现，会在链接或语义上与真实运行时行为脱节。

**本方案的做法：** 把「从头文件抄下来的那条派发链」用 **少量行的 `unsafe` Rust** 固定下来，作为 **crate 公共 API 的一部分**。这样 **数据路径调用方** 与 **控制路径调用方** 共享同一套 **`rdma-sys`** 世界观，减少「C 类型一套、Rust 封装另一套」的分裂。

---

## 安全 Rust 封装层：`async-rdma`

**与 FFI 的关系：** `async-rdma` **不引入第二层 bindgen**。它假定 **`rdma-sys` 已提供**：

- 完整的 **类型与常量**；
- **可调用的 `verbs.rs` 热点**；
- **rdmacm** 相关符号。

**async-rdma 的职责（归纳）：**

- 把 **连接建立、QP 状态、内存注册** 等组织成 **更易用的 Rust API**；
- 集成 **`async`/await** 与 **运行时**（如 Tokio），把 **阻塞型 verbs 调用**放到合适线程或封装为 Future；
- 在类型层面约束 **资源生命周期**（仍不可能消除全部 RDMA 固有的 unsafe 契约）。

**分层一句话：** **`rdma-sys` = FFI + inline 语义闭环；`async-rdma` = 在该闭环之上的异步与安全抽象**。这与「FFI 在 sys、verbs 派发在 safe」（jonhoo）或「桩 + dlopen」（mummy）的分工都不同。

---

## Bindgen：是否使用、怎么用

| 项 | 说明 |
|----|------|
| **是否使用** | **是**。`build.rs` 中用 **`bindgen::Builder`** 生成 **`OUT_DIR/bindings.rs`**。 |
| **头文件从哪里来** | **`pkg-config`** 探测 **`libibverbs`**、**`librdmacm`**（版本下限写在 `build.rs`），把所有 **`include_paths`** 转成 **`clang_arg("-I...")`**；再包含 **`src/bindings.h`**（内部 `#include <infiniband/verbs.h>`、`rdma/rdma_cma.h` 等）。 |
| **典型 bindgen 选项** | 对 **`ibv_*` / `rdma_*` / `verbs_*`** 做 **allowlist**；对 **`ibv_send_wr`、`ibv_wc`、`sockaddr*`** 等 **`blocklist_type`**；大量 **`bitfield_enum`** / **`constified_enum_module`**；**`disable_untagged_union()`** 等，与工作区 `rdma-sys/build.rs` 一致。 |
| **产物接入方式** | `src/lib.rs` **`include!(concat!(env!("OUT_DIR"), "/bindings.rs"))`**，并与手写模块 **`pub use`**。 |

bindgen 同样 **不生成** `verbs.h` 里 **`static inline`** 的可调用 Rust 实现；本项目选择在 **Rust 手写**（见下节「bindgen 之外」）。

---

## bindgen 之外还做了什么

| 工作 | 说明 |
|------|------|
| **`verbs.rs`（手写 inline）** | 用 **`#[inline] pub unsafe fn`** 实现与 **`verbs.h` inline** 等价的 **`ibv_post_send`、`ibv_poll_cq`、`ibv_wr_*`** 等，内部通过 **`(*qp)` / `(*cq)` / `(*ctx)`** 上的 **函数指针** 派发（见工作区 `rdma-sys/src/verbs.rs`）。 |
| **`types.rs`（手写布局）** | 对被 blocklist 的 **`ibv_send_wr`、`ibv_wc`** 等 **手写 `#[repr(C)]`**，避免 bindgen 对 **union** 生成不可用或不稳定的 Rust。 |
| **`opcode.rs` 等** | 与 verbs  Opcode / 常量相关的辅助模块，配合手写调用约定。 |
| **链接** | 通过 pkg-config 探测到的库信息，由构建脚本/链接器最终 **`libibverbs` + `librdmacm` 动态链接**（与上游惯例一致）。 |
| **上层 `async-rdma`** | **不再跑一层 bindgen**；直接使用 **`rdma-sys`** 提供的 FFI + inline 包装，再做异步与安全抽象。 |

---

## 补充：`libibverbs` / `librdmacm` 版本下限

以工作区 **`rdma-sys/build.rs`** 为准（当前可见为 **libibverbs ≥ 1.8.28**、**librdmacm ≥ 1.2.28**）；不满足则 **`pkg-config` 探测失败**，绑定阶段不会成功。

---

## static inline 的处理方式（总结）

与 rust-ibverbs「把 inline 语义放到上层 safe crate」不同，此处 **把等价实现集中在 `rdma-sys/src/verbs.rs`**，使 **`rdma-sys` 单独即可调用 `ibv_post_send`、`ibv_poll_cq` 等**（见工作区 `rdma-sys/src/verbs.rs` 大量 `#[inline] pub unsafe fn`）。

---

## 与 async-rdma 的关系

用户提到的「一层 sys + 一层更易用更安全」指的就是：**FFI 与 inline 补齐全部在 `rdma-sys`**，`async-rdma` 专注业务抽象与异步运行时集成。

---

## 优缺点摘要

| 优点 | 缺点 |
|------|------|
| `-sys` 自给程度高，上层crate省心 | **编译期必须**安装 `libibverbs-dev`、`librdmacm-dev` |
| 同时覆盖 **ibverbs + rdmacm** | 上游仓库近年迭代放缓时，绑定要自己 fork 跟进 |
| 手写类型与 inline 与 rdma-mummy 路线相比更直观（纯 Rust） | union / 新 API 仍要持续手工维护 |

---

## 小结

典型的 **「系统头文件 + bindgen + `-sys` 内手写 inline + 手写疑难类型」**；和 DatenLord 自己的异步封装 **`async-rdma` 是清晰的两层（sys → safe/async）**，且 **FFI 热点闭合在 sys**。
