# FFI 方案：rdma-core-mummy 静态桩库 + bindgen + 运行时 dlopen/dlsym

**代表项目：** [RDMA-Rust/rdma-mummy-sys](https://github.com/RDMA-Rust/rdma-mummy-sys) + [RDMA-Rust/sideway](https://github.com/RDMA-Rust/sideway)

## Bindgen：是否使用、怎么用

| 项 | 说明 |
|----|------|
| **是否使用** | **是**。与工作区 **`rdma-mummy-sys/build.rs`** 一致：构建时用 **`bindgen::Builder`** 生成 **`OUT_DIR/bindings.rs`**。 |
| **头文件 / `-I` 从哪来** | **`clang_arg("-I./rdma-core-mummy/include")`**，配合 **`src/bindings.h`**；不依赖本机 **`libibverbs-dev`** 也能生成 **与桩配套** 的头视图。 |
| **与 DatenLord 类同的配置** | **`allowlist_function("ibv_.*")`、`rdma_.*`**，对疑难类型 **`blocklist_type`**（`ibv_send_wr`、`ibv_wc` 等）、**`bitfield_enum` / `constified_enum_module`**、**`disable_untagged_union()`** 等与 **`rdma-sys`** 高度同源。 |
| **bindgen 回调** | 例如 **`CompatParseCallback`**：把 **`ibv_query_port`** 生成的符号 **改名为 `ibv_query_port_compat`**，避免与手写或其它符号冲突（见工作区 `build.rs`）。 |
| **产物接入** | `src/lib.rs` **`include!(concat!(env!("OUT_DIR"), "/bindings.rs"))`**，再 **`pub use`** **`opcode` / `types` / `verbs`** 等手写模块。 |

此处 bindgen 面对的是 **「已由 mummy 桩导出」的普通 **`extern "C"`** 符号**（桩内在运行时 **`dlsym`** 解析到真实实现），因此 **不必**在 Rust 里再抄写一整套 **`verbs.h` inline**。

## bindgen 之外还做了什么

| 工作 | 说明 |
|------|------|
| **`cmake` 构建 `rdma-core-mummy`** | **`cargo:rustc-link-lib=static=ibverbs`**、**`static=rdmacm`**：链接的是 **C 写的桩静态库**，不是发行版用户直接链的官方 **`libibverbs.so`** 单独静态归档。 |
| **运行时 `dlopen` / `dlsym`** | 桩在初始化时打开 **`libibverbs.so.1`** 等，解析 **`post_send`、`poll_cq`** 等到函数指针；详见 **`rdma-core-mummy`** 源码。 |
| **手写模块 `types.rs` / `verbs.rs` / `opcode.rs`** | 与被 blocklist 的类型、Opcode、以及对绑定结果的 Rust 侧整理配套（与 `rdma-sys` 结构类似）。 |
| **`sideway`** | **不使用 bindgen**；依赖 **`rdma-mummy-sys`** crate，只做 **安全封装与 API 设计**。 |

## 分层（与 DatenLord 是否「同一思路」）

**相似点：** 都是 **底层 `-sys`（原始 FFI 形状）+ 上层 Rust 友好封装**（sideway 对应 async-rdma 之上的那一层）。

**不同点（FFI 机制上关键差异）：**

| 维度 | DatenLord `rdma-sys` | `rdma-mummy-sys` |
|------|---------------------|------------------|
| 链接真实 rdma-core | 编译期 pkg-config，直接链 **`libibverbs.so`** | 编译期链 **mummy 生成的静态桩**；桩内在运行时 **`dlopen` 真实 `.so`** |
| inline 补齐位置 | Rust `verbs.rs` 手写 | C 桩提供 **真实符号**，bindgen 只管 `extern "C"` |
| 是否依赖系统 `-dev`（绑定生成） | 通常需要 | **绑定可用捆绑头文件**（见子模块 `rdma-core-mummy`） |

因此：**分层哲学类似（sys + wrapper），FFI 内核不同**：一边是 **Rust 手写 inline**，一边是 **C 桩 + 动态加载**。

## 补充：进一步阅读

桩侧 **`dlopen` / 符号表** 的细节见 **`rdma-core-mummy`** 源码；文字动机与工作区 **`rust-rdma-io/docs/background/Bindings.md`** 中 mummy 小节。

## sideway 的定位

`sideway` 依赖 crates.io / 路径上的 **`rdma-mummy-sys`**，在之上提供 **Rust 风格的 ibverbs + rdmacm 封装**（工作区 `sideway/src/lib.rs` 模块划分）。与 `async-rdma` 相比：**sideway 更强调新版 verbs API（如 WR builder、`ibv_qp_ex` 路径）与类型安全用法**，具体特性以该仓库 README/CHANGELOG 为准。

## 优缺点摘要

| 优点 | 缺点 |
|------|------|
| 规避「bindgen 看不到 static inline」的一类问题 | 维护 **C 桩 + cmake**  subtree |
| 无 rdma-core dev 包环境下仍可生成绑定（利于 CI/docs） | 运行时 **`dlopen`/符号解析** 与版本矩阵 |
| 失败时可按桩设计降级（例如返回不支持） | 心智模型比「直接链.so」复杂 |

## 小结一句

**两层分工像 DatenLord（sys + 上层），但 `-sys` 不是靠 Rust 抄 inline，而是靠 mummy 桩把调用变成稳定 `extern "C"` 边界。**
