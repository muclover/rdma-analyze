# FFI 方案：薄 C wrapper 展开 inline + 预生成 Rust FFI（bnd）+ 系统头文件

**代表项目：** [youyuanwu/rust-rdma-io](https://github.com/youyuanwu/rust-rdma-io) 中的 **`rdma-io-sys`**

---

## 方案定位

这条路线把 **`verbs.h` static inline** 的问题 **完全交给 C 编译器**：为每个 Rust 需要直接调用的 **`ibv_*` / `ibv_wr_*` / rdmacm API** 编写 **`rdma_wrap_*` 非 static 函数**，函数体内调用真实 inline API。这样 **链接器看到的是「普通全局符号」**，任意 FFI 生成器（本项目选用 **bnd**）都能生成 **`extern "C"` Rust 声明**。

与 mummy 的差异：**不采用运行时 dlopen 桩**；与用户构建时 bindgen 的差异：**绑定文件预生成进仓库**，用户 **`cargo build` 只编译薄 wrapper**，**不执行 bindgen/libclang 重量级流水线**。

---

## 架构总览

```text
  ┌──────────────────────────────────────────────────────────────┐
  │     rdma-io / 其它上层 crate（若存在：进一步 safe / IO 抽象）    │
  └────────────────────────────┬─────────────────────────────────┘
                               │ 依赖 rdma-io-sys
                               ▼
  ┌──────────────────────────────────────────────────────────────┐
  │                  rdma-io-sys                                  │
  │  · src/rdma/*.rs（预生成，含 ibverbs / rdmacm / wrapper 分区）  │
  │  · build.rs：仅用 cc 编译 wrapper.c → 静态库 rdma_wrapper      │
  └───────────────┬────────────────────────────┬─────────────────┘
                  │ 链接静态 wrapper             │ Rust extern 声明指向 rdma_wrap_* 等
                  ▼                             │
       ┌─────────────────────┐                  │
       │ wrapper.o (静态)    │                  │
       │ rdma_wrap_ibv_* …   │ ───调用──────────┼──► ibv_* inline（在 .c 内展开）
       └─────────┬───────────┘                  │
                 │ 仍依赖动态库                  │
                 ▼                             ▼
            libibverbs.so + librdmacm.so（系统 rdma-core）
```

**维护者侧流水线（非终端用户每次构建）：**

```text
系统头文件 + wrapper.h ──► bnd-rdma-gen ──► 生成 src/rdma/*.rs ──► git 提交
```

**终端用户侧：** **`cc` + 链接器**，无需 libclang。

---

## FFI 边界与设计抉择

| 边界 | 实现方式 | 作用 |
|------|----------|------|
| **inline → 可调符号** | `wrapper.c` 里的 **`rdma_wrap_*`** | C 编译器负责语义与优化；Rust 只看到稳定符号名 |
| **Rust 声明来源** | **bnd** 预生成模块（检入仓库） | **可 diff、可审计**；docs.rs / CI 无需 clang |
| **链接模型** | **静态 wrapper + 动态 libibverbs/rdmacm** | 与 mummy「静态桩 + dlopen」不同；与 DatenLord「直接链 .so」相近，但多了一层 wrapper |

**扩展新 API 的路径：** 修改 **`wrapper.c/.h`** → 重跑 **bnd-rdma-gen** → 提交 Rust 变更。两步缺一不可。

---

## 安全 Rust 封装层：`rust-rdma-io` 仓库内的上层

**`rdma-io-sys` 自身：** 主要是 **FFI + wrapper**，暴露 **`unsafe` 友好的底层形状**（具体模块名以仓库为准）。

**同仓库的 `rdma-io`（若使用）：** 在 **`rdma-io-sys`** 之上提供 **更高阶抽象**（连接、队列、与 IO 相关的封装）。要点：

- **FFI 不在上层重复**；
- **safe 程度**取决于上层 API 设计——底层仍是 RDMA，**不可能仅凭封装消除全部 UB 风险**；
- 与 **sideway / async-rdma** 不同栈，**哲学接近「sys 清晰 + 自选上层厚度」**。

（细节以 `rust-rdma-io` README 与 crate 分层为准。）

---

## Bindgen：是否使用、怎么用

| 项 | 说明 |
|----|------|
| **`rdma-io-sys` 消费者构建** | **不使用 bindgen**。`rdma-io-sys/build.rs` **只做一件事**：用 **`cc` crate** 编译 **`wrapper/wrapper.c`** → 静态库 **`rdma_wrapper`**（名称以仓库为准）。 |
| **Rust FFI 从哪来** | **`src/rdma/`** 下 **已提交进仓库的预生成模块**（ibverbs、rdmacm、wrapper 分区），由维护者在开发机/CI 上运行 **`bnd-rdma-gen`**（**bnd / windows-bindgen 系工具链**，**不是** `bindgen` crate）从 **系统头文件 + `wrapper.h`** 生成后再入库。 |
| **与 bindgen 的关系** | 同属「从头文件生成 Rust `extern` + 类型」这一类问题，但 **生成器不同**；设计上避免了 **`cargo build` 依赖 libclang/bindgen**。 |

若只讨论「整个 rust-rdma-io **仓库**」：开发者侧用的是 **bnd**，不是 bindgen。

---

## bindgen 之外还做了什么

| 工作 | 说明 |
|------|------|
| **`wrapper.c` / `wrapper.h`** | 为每个需在 Rust 侧可调用的 **`static inline`**（及同类符号）提供 **`rdma_wrap_*` 非 inline C 函数，由 **C 编译器**完成 **inline 展开** 与 **ABI 正确的调用**。 |
| **`cc` 编译静态 wrapper** | 与用户 crate 链在一起时：**静态 `rdma_wrapper` + 动态 `libibverbs` / `librdmacm`**（与工作区 `rdma-io-sys/build.rs`、`Cargo.toml` 一致）。 |
| **`bnd-rdma-gen` + 分区** | 从系统 **`infiniband/*.h`、`rdma/*.h`** 与 **自建 wrapper 头** 抽取类型与函数签名；与 **`bnd-linux`（如 `__be*`）、`bnd-posix`** **交叉引用**，避免重复定义 POSIX 类型。 |
| **维护流程** | API 变更时：**改 wrapper → 重跑生成器 → 提交 diff**；与 bindgen 方案的「改 `build.rs`/flags → 每次 OUT_DIR 再生」流程不同，但 **人工审计绑定 diff** 更直观。 |

---

## 动机（与本仓库文档一致）

`verbs.h` 中大量 **`static inline`** 无法被 bindgen 可靠全覆盖；若全靠 Rust 手写，会与 **rdma-sys / Nugine** 一样维护量大。mummy 用 C 桩 + dlopen 也很强，但本项目选择 **更直白的一条路：让 C 编译器展开 inline**，导出 **`rdma_wrap_*` 真实符号**，再由绑定生成器产出 Rust 声明。

---

## 分层

| 组件 | 角色 |
|------|------|
| `wrapper/wrapper.c` + `wrapper.h` | 对每个热点 inline API 写一层 **非 inline** C 函数，内部直接调用 `ibv_post_send`、`ibv_poll_cq`、`ibv_wr_*` 等 |
| `build.rs`（`cc` crate） | 把 `wrapper.c` 编成 **静态库 `rdma_wrapper`**，再与用户程序一起链 **`libibverbs.so` / `librdmacm.so`** |
| `src/rdma/` 下预生成模块 | 开发者用 **bnd / windows-bindgen 流水线**（见仓库 `bnd-rdma-gen` 与 `docs/background/Bindings.md`）从系统头文件 + `wrapper.h` 生成 **提交进库的 Rust FFI**，**用户构建时不再跑 bindgen** |

---

## 与工作区文件对应关系

- `rust-rdma-io/rdma-io-sys/build.rs`：仅 **`cc::Build`** 编译 wrapper。
- `rust-rdma-io/rdma-io-sys/wrapper/wrapper.c`：`rdma_wrap_ibv_*` 系列函数。
- `rust-rdma-io/docs/background/Bindings.md`：**Option D** 与设计决议（为何不用 mummy、为何用 bnd 等）。

---

## 与 bindgen 路线的差异（归纳）

不是「bindgen 即时生成 + blocklist/manual」，而是 **「C 负责语义正确的符号边界 + 另一套生成器产出 crate 内已提交的 Rust 声明」**；并与 **`bnd-linux` / `bnd-posix`** 等共享 POSIX/Linux 类型 story。

---

## 优缺点摘要

| 优点 | 缺点 |
|------|------|
| inline 问题由 **C 编译器**解决，Rust 侧手写量少 | **编译期仍要** `-dev` 头文件（与设计取舍一致） |
| 调用链仅多一层 **薄 wrapper**，开销通常可忽略 | 工具链是 **bnd**，与主流 bindgen 技能栈不同 |
| 绑定可 **预生成**，利于复核与 docs.rs 策略 | 新增 API 要同时改 **wrapper + 生成管线** |

---

## 小结

**FFI =「系统头文件 + 薄 C wrapper 导出符号 + 预生成 Rust（bnd）」**；与 mummy 一样借用 **C 语言处理 inline**，但链接模型是 **直接动态依赖正式 `libibverbs`，而非 dlopen 桩**。上层 **`rdma-io`** 等在 **`rdma-io-sys`** 之上继续封装，**不再生成第二层 FFI**。
