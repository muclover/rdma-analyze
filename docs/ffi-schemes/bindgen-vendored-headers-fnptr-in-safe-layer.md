# FFI 方案：Bindgen + 内嵌 rdma-core 头文件 + 安全层直接走 ops 函数指针

**代表项目：** [jonhoo/rust-ibverbs](https://github.com/jonhoo/rust-ibverbs)（crate：`ibverbs-sys` + `ibverbs`）

## Bindgen：是否使用、怎么用

| 项 | 说明 |
|----|------|
| **是否使用** | **是**。`ibverbs-sys` 在 **`build.rs`** 里调用 **`bindgen::Builder`**，每次构建 crate 时生成绑定。 |
| **输入头文件** | 默认 **`vendor/rdma-core/libibverbs/verbs.h`**（经 cmake 配置得到一致的 `-I` include 树）；也可用 **`RDMA_CORE_INCLUDE_DIR`** / **`IBVERBS_HEADER_DIR`** 覆盖。 |
| **典型 bindgen 选项** | `allowlist_function("ibv_.*")`、`allowlist_type("ibv_.*")`、若干 **`bitfield_enum`**、`blocklist_type("ibv_wc")`（避免难生成的 union 布局）、`size_t_is_usize(true)` 等。 |
| **产物位置** | **`$OUT_DIR/bindings.rs`**，由 `ibverbs-sys/src/lib.rs` **`include!`** 进来。 |

bindgen **不负责** `verbs.h` 里大量 **`static inline`**：默认不会生成等价可调用的 Rust 函数体（即便开 `generate_inline_functions`，也与真实 ops 派发语义难对齐）。

## bindgen 之外还做了什么

| 工作 | 说明 |
|------|------|
| **vendor / cmake** | 更新 **`vendor/rdma-core` 子模块**，跑 **cmake**（`no_build_target(true)`）主要是为了得到稳定头文件路径；可选缩短 `CMAKE_INSTALL_PREFIX` 以避免过长路径问题。 |
| **链接指令** | `cargo:rustc-link-lib=ibverbs`，运行时加载系统 **`libibverbs.so`**；可选 `cargo:rustc-link-search` 指向 vendor 构建出的 lib 目录。 |
| **手写或绕行类型** | 如对 **`ibv_wc` blocklist**，需在 Rust 侧维护与 C ABI 一致的类型定义（策略见该 crate `build.rs` / `lib.rs`）。 |
| **`ibverbs` 安全 crate** | 封装资源生命周期与高层 API；对 **`ibv_post_send` 等 inline API**，在 **safe 层**手写 **`context.ops.post_send(...)`** 等函数指针调用，而不是在 `-sys` 再生成一层包装函数。 |
| **仅 ibverbs** | 不包含 **librdmacm**；CM 需其它 crate。 |

## 分层

| Crate | 角色 |
|--------|------|
| `ibverbs-sys` | 通过 bindgen 生成 `extern "C"`；类型与非内联符号来自生成的 `bindings.rs` |
| `ibverbs` | 安全封装；对 **`verbs.h` 里的 static inline 数据路径**（如 `ibv_post_send`）**不在 sys 层重复实现**，而是在 Rust 里解引用 `ibv_context.ops` / `ibv_qp` 相关字段，直接调用 **函数指针** |

## 补充：与工作区 `ibverbs-sys/build.rs` 对齐的常见开关

若不走默认 vendor：**同时设置** **`RDMA_CORE_INCLUDE_DIR`** 与 **`RDMA_CORE_LIB_DIR`**；可选 **`IBVERBS_HEADER_DIR`** 指向自定义 **`verbs.h`**。其余 cmake/git submodule 行为见该文件注释。

## static inline 的处理方式

`verbs.h` 里大量 API（如 `ibv_post_send`、`ibv_poll_cq`）是 inline + 经 `context->ops` 派发。bindgen 通常 **不会** 为这些生成可调用的独立 Rust 实现。

本项目的做法是：**在 safe `ibverbs` crate 中手写等价逻辑**，例如从 `qp` 取出 `context`、`ops`，再调用 `ops.post_send.unwrap()(qp, ...)`（见工作区 `rust-ibverbs/ibverbs/src/lib.rs` 中 `post_send`）。

也就是说：**「inline 语义」消化在安全封装层，而不是 `-sys` 里的 Rust `#[inline]` 包装函数集合**。

## rdmacm

经典 `rust-ibverbs` 路线聚焦 **libibverbs**，**不包含 librdmacm**。需要 CM 时要叠其它 crate（例如 DatenLord 的 `rdma-sys` / `async-rdma`，或 `sideway` / `rust-rdma-io` 等）。

## 优缺点摘要

| 优点 | 缺点 |
|------|------|
| 头文件版本相对可控（vendor），便于在无完整 `-dev` 的机器上生成绑定 | 子模块体积与 cmake 依赖 |
| 社区老牌、文档与示例多 | 数据路径逻辑泄漏到 safe 层，`-sys` 单独用时对 inline API 不完整 |
| crates.io 上「ibverbs」心智强 | 与 async-rdma / sideway 等新栈相比，stars 不等于「唯一首选」 |

## 小结一句

**「最广为人知的 ibverbs 教学型栈」之一**；FFI 上属于 **bindgen + vendor 头文件 + 安全层函数指针派发**，与「在 `-sys` 写一整份手写 inline 包装」（datenlord）不是同一种拆分。
