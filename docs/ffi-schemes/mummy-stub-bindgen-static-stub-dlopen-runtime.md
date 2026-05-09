# FFI 方案：rdma-core-mummy 静态桩库 + bindgen + 运行时 dlopen/dlsym

**代表项目：** [RDMA-Rust/rdma-mummy-sys](https://github.com/RDMA-Rust/rdma-mummy-sys) + [RDMA-Rust/sideway](https://github.com/RDMA-Rust/sideway)

---

## 方案定位

这条路线解决的是 **两类耦合问题** 的交集：

1. **bindgen 与 `verbs.h` static inline：** 与 DatenLord 相同，单纯生成 `extern "C"` 往往不够用。
2. **rdma-core 的版本化 / 私有符号与链接约束：** 分发二进制时，「编译期链到的 rdma-core」与「运行环境 `.so`」组合复杂。

**mummy 的思路：** 把 **「可被 bindgen 消费的稳定 C ABI」** 放到 **`rdma-core-mummy` 静态桩**里；桩在 **运行时 `dlopen`** 系统 **`libibverbs.so` / `librdmacm.so`**，用 **`dlsym`** 解析真实实现并填入跳转表。Rust 侧 **`rdma-mummy-sys`** 只与 **桩暴露的符号**对接，从而 **弱化生成绑定对「本机 `-dev` + 系统头」的硬依赖**，并让 **FFI 边界落在明确的 `extern "C"`** 上。

**上层 `sideway`** 在此基础上提供 **现代 ibverbs / rdmacm 的 Rust API**（builder、CQ/QP Ex、错误语义化等），**不再单独跑 bindgen**。

---

## 架构总览

```text
  ┌─────────────────────────────────────────────────────────────┐
  │                      sideway（Rust API）                     │
  │   现代 verbs；部分接口刻意保留 unsafe；不含第二层 bindgen      │
  └────────────────────────────┬────────────────────────────────┘
                               │ 依赖
                               ▼
  ┌─────────────────────────────────────────────────────────────┐
  │                 rdma-mummy-sys（ -sys ）                     │
  │   bindgen → bindings.rs（对着 mummy 头里的 extern "C"）       │
  │   + types.rs / verbs.rs / opcode（与 rdma-sys 结构相似）      │
  └────────────────────────────┬────────────────────────────────┘
                               │ 链接静态桩
                               ▼
  ┌─────────────────────────────────────────────────────────────┐
  │           rdma-core-mummy（C + cmake → libibverbs.a 等）       │
  │   进程启动：dlopen 真实 libibverbs.so.1、dlsym 填表           │
  │   对外：稳定的转发符号（bindgen 可见）                         │
  └────────────────────────────┬────────────────────────────────┘
                               │ dlopen / dlsym
                               ▼
              系统安装的 libibverbs.so / librdmacm.so（「真 .so」）
```

**与 DatenLord「两层分工」的相似性：** 都是 **`xxx-sys` + 上层 Rust 封装**。  
**FFI 内核的差异：** DatenLord 用 **Rust `verbs.rs` 手写 inline**；mummy 用 **C 桩 + 动态加载** 把 **inline/符号问题消化在 native 层**，bindgen 面对的是 **更像普通 C 库的导出函数集合**。

---

## FFI 边界与设计抉择

| 层级 | 做什么 | 不做什么 |
|------|--------|----------|
| **真实 rdma-core** | 提供厂商驱动对接与 **`ibv_*` 实现** | 不参与 Rust 编译期的 bindgen 输入（通过 dlopen 延后绑定） |
| **mummy 静态桩** | **稳定 ABI、符号转发、初始化加载** | 不试图在 Rust 内复制整套 verbs 语义 |
| **`rdma-mummy-sys`** | **类型 + `extern "C"` + 与 `rdma-sys` 同构的手写模块** | 通常不需要像 `rdma-sys` 那样大规模 Rust 手写 inline（具体以仓库为准） |
| **`sideway`** | **人体工程学 API、错误类型、Ex API 统一** | 不把全部 post/reg_mr 强行 safe 化（项目自述的取舍） |

**运行时代价：** 首次加载 **`dlopen`/符号解析**；稳定后数据路径仍落在 **native 转发 → 真实 `.so`**，多数场景开销相对 RDMA 本身可忽略，但 **调试符号版本、mismatch** 需要纳入 CI。

---

## 与 DatenLord / jonhoo 的对比（架构视角）

| 维度 | DatenLord `rdma-sys` | `rdma-mummy-sys` |
|------|---------------------|------------------|
| 链接真实 rdma-core | 编译期 pkg-config，直接链 **`libibverbs.so`** | 编译期链 **mummy 静态桩**；桩内在运行时 **`dlopen` 真实 `.so`** |
| inline 补齐位置 | Rust `verbs.rs` 手写 | **C 桩 + dlsym** 提供可调用的 **转发符号**；bindgen 消费 `extern "C"` |
| 绑定生成是否强依赖系统 `-dev` | 通常需要 | **可用捆绑头文件**（见子模块 `rdma-core-mummy`），利于 CI/docs |

**结论：** **分层哲学类似（sys + wrapper），FFI 内核不同**：一边是 **Rust 手写 inline**，一边是 **C 桩 + 动态加载**。

---

## 安全 Rust 封装层：`sideway`

**依赖关系：** `sideway` **依赖 `rdma-mummy-sys`**（及生态内其它 crate），**构建时不重复 bindgen rdma-core**。

**封装取向（结合公开文档与博客叙述归纳）：**

- **控制路径：** builder、枚举化常量、`modify_qp` 类错误携带 **invalid mask / GID / sgid_index** 等可观测信息。
- **数据路径：** 对齐 **`ibv_wr_*`、CQ/QP Ex** 等高效 API；**刻意保留** `ibv_reg_mr`、`post` 相关 **`unsafe`**，避免为「全 safe」牺牲性能或排斥 GDR/GPU 等用法。
- **与 `async-rdma` 的差异：** `sideway` 强调 **同步 verbs + thread-per-core 友好**；异步集成留给更上层或未来探索，而非 crate 核心假设。

**小结：** **`sideway` = 在 mummy FFI 之上的语义化 Rust 层**；**unsafe 边界清晰**，完整「证明安全」留给调用方或更高层库。

---

## Bindgen：是否使用、怎么用

| 项 | 说明 |
|----|------|
| **是否使用** | **是**。与工作区 **`rdma-mummy-sys/build.rs`** 一致：构建时用 **`bindgen::Builder`** 生成 **`OUT_DIR/bindings.rs`**。 |
| **头文件 / `-I` 从哪来** | **`clang_arg("-I./rdma-core-mummy/include")`**，配合 **`src/bindings.h`**；不依赖本机 **`libibverbs-dev`** 也能生成 **与桩配套** 的头视图。 |
| **与 DatenLord 类同的配置** | **`allowlist_function("ibv_.*")`、`rdma_.*`**，对疑难类型 **`blocklist_type`**（`ibv_send_wr`、`ibv_wc` 等）、**`bitfield_enum` / `constified_enum_module`**、**`disable_untagged_union()`** 等与 **`rdma-sys`** 高度同源。 |
| **bindgen 回调** | 例如 **`CompatParseCallback`**：把 **`ibv_query_port`** 生成的符号 **改名为 `ibv_query_port_compat`**，避免与手写或其它符号冲突（见工作区 `build.rs`）。 |
| **产物接入** | `src/lib.rs` **`include!(concat!(env!("OUT_DIR"), "/bindings.rs"))`**，再 **`pub use`** **`opcode` / `types` / `verbs`** 等手写模块。 |

此处 bindgen 面对的是 **「已由 mummy 桩导出」的普通 `extern "C"` 符号**（桩内在运行时 **`dlsym`** 解析到真实实现），因此 **不必**在 Rust 里再抄写一整套 **`verbs.h` inline**（具体符号集合以桩为准）。

---

## bindgen 之外还做了什么

| 工作 | 说明 |
|------|------|
| **`cmake` 构建 `rdma-core-mummy`** | **`cargo:rustc-link-lib=static=ibverbs`**、**`static=rdmacm`**：链接的是 **C 写的桩静态库**，不是发行版用户直接链的官方 **`libibverbs.so`** 单独静态归档。 |
| **运行时 `dlopen` / `dlsym`** | 桩在初始化时打开 **`libibverbs.so.1`** 等，解析 **`post_send`、`poll_cq`** 等到函数指针；详见 **`rdma-core-mummy`** 源码。 |
| **手写模块 `types.rs` / `verbs.rs` / `opcode.rs`** | 与被 blocklist 的类型、Opcode、以及对绑定结果的 Rust 侧整理配套（与 `rdma-sys` 结构类似）。 |
| **`sideway`** | **不使用 bindgen**；依赖 **`rdma-mummy-sys`** crate，只做 **安全封装与 API 设计**。 |

---

## 分层（与 DatenLord 是否「同一思路」）

**相似点：** 都是 **底层 `-sys`（原始 FFI 形状）+ 上层 Rust 友好封装**（sideway 对应 async-rdma 之上的那一层）。

**不同点：** 见上文「与 DatenLord 对比」表。

---

## 补充：进一步阅读

桩侧 **`dlopen` / 符号表** 的细节见 **`rdma-core-mummy`** 源码；文字动机与工作区 **`rust-rdma-io/docs/background/Bindings.md`** 中 mummy 小节。

---

## 优缺点摘要

| 优点 | 缺点 |
|------|------|
| 规避「bindgen 看不到 static inline」的一类问题 | 维护 **C 桩 + cmake**  subtree |
| 无 rdma-core dev 包环境下仍可生成绑定（利于 CI/docs） | 运行时 **`dlopen`/符号解析** 与版本矩阵 |
| 失败时可按桩设计降级（例如返回不支持） | 心智模型比「直接链.so」复杂 |

---

## 小结

**两层分工像 DatenLord（sys + 上层），但 `-sys` 不是靠 Rust 抄 inline，而是靠 mummy 桩把调用变成稳定 `extern "C"` 边界；`sideway` 在该 FFI 之上提供现代 Rust verbs 封装。**
