# zstd-rs — FFI 单项目分析

**分析对象（源码路径）：** `zstd-rs/`  
**计划序号：** P4（阶段 A4）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`zstd-rs` 绑定 **Zstandard（zstd）** 压缩库，面向需要压缩/解压的 Rust 应用。架构为 **三层递进**：`zstd-sys`（`-sys`，vendored C + 预生成/可选 bindgen 绑定）→ `zstd-safe`（薄 safe 封装，`CCtx`/`DCtx` 等）→ `zstd`（`Read`/`Write` stream 与便捷函数）。默认 **内嵌编译** vendored `zstd` 子模块；可选 `pkg-config` 链接系统 `libzstd`。

---

## 1. 仓库结构

### 1.1 Crate 分层（非 Cargo workspace）

| Crate | 路径 | 角色 |
|-------|------|------|
| `zstd` | 根 `Cargo.toml` | 高层：`stream`、`bulk`、`dict`；重导出 `zstd_safe` |
| `zstd-safe` | `zstd-safe/` | 低层 safe：上下文、字典、`SafeResult` |
| `zstd-sys` | `zstd-safe/zstd-sys/` | `links = "zstd"`；FFI + `build.rs` 编 C |

根目录 **无** `[workspace]`；三 crate 以 path 依赖串联（`confirmed`）。

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `zstd-safe/zstd-sys/zstd/` | vendored zstd 源码（submodule） |
| `zstd-safe/zstd-sys/build.rs` | `cc` 编译、`bindgen` 可选、pkg-config 分支 |
| `zstd-safe/zstd-sys/src/bindings_*.rs` | 预生成绑定（多 feature 组合） |
| `zstd-safe/zstd-sys/zstd.h` | bindgen 入口（含子模块或系统头） |
| `zstd-safe/src/lib.rs` | `CCtx`/`DCtx`/字典/流 API |
| `zstd-safe/src/seekable.rs` | seekable 格式（feature） |
| `src/stream/` | `Encoder`/`Decoder`、`copy_encode`/`copy_decode` |
| `src/bulk.rs`、`src/dict.rs` | 缓冲区与字典辅助 |
| `zstd-safe/fuzz/` | fuzz 目标（存在则 §8 提及） |

### 1.3 Examples（简单阅读）

| 示例 | 文件 | 是否演示 FFI |
|------|------|----------------|
| `basic` | `examples/basic.rs` | 间接 — 顶层 `zstd` API |
| `stream` | `examples/stream.rs` | 是 — `Read`/`Write` 包装 |
| `zstd` / `zstdcat` | `examples/zstd.rs`、`zstdcat.rs` | 是 — CLI 式压缩/解压 |
| `benchmark` | `examples/benchmark.rs` | 性能演示，非 FFI 契约 |
| `train` | `examples/train.rs` | 字典训练（需 `zdict_builder`） |
| `zstd-sys` | `zstd-safe/zstd-sys/examples/it_work.rs` | 是 — 直接测 sys 层 |

### 1.4 Tests

| 类型 | 位置 | 说明 |
|------|------|------|
| 单元 | `zstd/src/lib.rs` 内 `test_cycle*`、`zstd-safe/src/tests.rs` | 编解码往返 |
| `zstd-safe/fuzz/` | fuzz 压缩/解压路径 | 存在（`confirmed` 目录） |
| 上游 | `zstd-safe/zstd-sys/zstd/tests/` | vendored C 测试，不纳入 Rust 分析深读 |

---

## 2. 原生依赖

### 2.1 库画像

| 字段 | 内容 |
|------|------|
| **C 库** | Zstandard（Facebook/Meta zstd） |
| **API 形态** | 上下文式：`ZSTD_CCtx`/`ZSTD_DCtx`；流式 `ZSTD_compressStream`；返回值 `size_t` 成功字节数或 **错误码**（`ZSTD_isError`）；字典 `ZSTD_CDict`/`ZSTD_DDict` |
| **资源对** | `ZSTD_createCCtx` ↔ `ZSTD_freeCCtx`（及 DCtx、CDict、DDict 等） |
| **线程安全** | 单上下文非线程安全；`zstdmt` feature 启用库内多线程压缩 |
| **分发方式** | 默认 vendored 静态 `libzstd.a`；`pkg-config` / `ZSTD_SYS_USE_PKG_CONFIG` 用系统库 |
| **绑定难点** | 实验 API 需 `ZSTD_STATIC_LINKING_ONLY`；wasm 需 `wasm-shim`；符号隐藏避免与系统 libzstd 冲突 |

### 2.2 `links`

- `zstd-sys`：`links = "zstd"`（`confirmed`）
- `-fvisibility=hidden` + `ZSTDLIB_VISIBILITY` 等减少 ODR 冲突（`build.rs` 注释 issue #58）

### 2.3 Vendored 范围（§7.4）

- 编译 `zstd/lib/common`、`compress`、`decompress` 等目录下 `.c`（跳过 xxhash 独立 .c，内联头文件）。
- 可选：`legacy`、`dictBuilder`、`seekable_format`、AMD64 ASM（`no_asm`/Windows 禁用）。
- **未读** vendored `.c` 实现细节。

### 2.4 Feature 矩阵（三层透传）

| Feature | 典型作用 |
|---------|----------|
| `legacy` | 旧格式支持 |
| `zdict_builder` | 字典构建 API |
| `zstdmt` | `ZSTD_MULTITHREAD` + pthread |
| `experimental` | 静态链接专用扩展 API |
| `bindgen` | 构建时生成 `OUT_DIR/bindings.rs` |
| `pkg-config` | 系统 libzstd |
| `thin` / `fat-lto` / `thin-lto` | 体积与 LTO |
| `seekable` | contrib seekable 格式 |

---

## 3. 绑定生成

### 3.1 默认：**预生成** + 可选 **bindgen**

- `zstd-sys/src/lib.rs` 按 feature `include!` 不同 `bindings_*.rs`（`experimental`、`zdict_builder`、`seekable` 组合）（`confirmed`）。
- `bindgen` feature：`build.rs` 中 `generate_bindings` 写 `OUT_DIR/bindings.rs`（`zstd.h`、`zdict.h` 等）（`confirmed`）。
- `zstd.h` 包装：PKG_CONFIG 时用 `<zstd.h>`，否则 `zstd/lib/zstd.h`（`confirmed`）。

### 3.2 bindgen 配置要点

- `use_core()`、`rustified_enum`、按 feature 加 `-DZSTD_STATIC_LINKING_ONLY` 等（`build.rs`）。
- `seekable` 时 `blocklist_function("ZSTD_seekable_initFile")`（文件 I/O 不绑定）。

### 3.3 C API 形态（头文件）

`zstd/lib/zstd.h`：上下文生命周期清晰；流 API 使用 `ZSTD_inBuffer`/`ZSTD_outBuffer`；错误通过返回值 + `ZSTD_isError`（与 zlib 风格不同）。

### 3.4 再生流程

- 仓库可含 `update_consts.sh` / bindgen feature；预生成文件提交在 `src/bindings_*.rs`。
- `zstd-safe/build.rs` 仅 wasm/hermit 时强制 `std` cfg（无绑定逻辑）。

---

## 4. 分层与公开 API

### 4.1 `zstd-sys`

- `#![no_std]`；wasm 有 `wasm_shim`。
- 纯 FFI + 常量；doctest 关闭（C 文档无法在 Rust 测）（`Cargo.toml`）。

### 4.2 `zstd-safe`

| 类型/模块 | 职责 |
|-----------|------|
| `CCtx`/`DCtx` | `NonNull` + 生命周期参数 `'a`；`Drop` 调 `ZSTD_free*` |
| `CDict`/`DDict` | 字典准备与引用 |
| `SafeResult` | `Result<usize, ErrorCode>`，`parse_code` + `ZSTD_isError` |
| `WriteBuf` trait | 输出缓冲区抽象（`std` feature 为 `Vec` 等） |
| `seekable` | `ZSTD_seekable_*` 封装（feature） |

**1:1 映射**：多数方法文档写明对应 C 函数名（`confirmed` 模块 docs）。

### 4.3 `zstd`

| 模块 | 职责 |
|------|------|
| `stream` | `Encoder`/`Decoder` 实现 `Read`/`Write`；`copy_encode`/`copy_decode` |
| `bulk` | 一次性压缩/解压缓冲区 |
| `dict` | 字典相关高层辅助 |
| 错误 | `map_error_code` → `io::Error`（`src/lib.rs`） |

### 4.4 上层适配

无独立运行时适配 crate；README 指向 **`async-compression`** 做 async 集成（非本仓库）。

---

## 5. 资源与生命周期

| 资源 | Rust 类型 | 释放 |
|------|-----------|------|
| `ZSTD_CCtx` | `CCtx<'a>` | `ZSTD_freeCCtx` in `Drop` |
| `ZSTD_DCtx` | `DCtx<'a>` | `ZSTD_freeDCCtx` in `Drop` |
| `ZSTD_CDict` | `CDict<'a>` | 对应 `free` |
| Seekable 流 | `SeekableCStream` 等 | `seekable.rs` 内 `Drop` |

- `DCtx: Send + Sync`（注释：非线程安全方法已要求 `&mut self`）（`confirmed` `lib.rs`）。
- 字典内容由 C 侧复制，`CDict::create` 文档说明无需保留原 buffer（`confirmed`）。

**风险点：**

- `experimental` 下 `try_clone` 仅在未处理数据前有效（文档 `confirmed`）。
- 与系统 libzstd 同时链接可能符号冲突 — build 用 hidden visibility 缓解（`inferred`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

| 层 | 模型 |
|----|------|
| `zstd-safe` | `SafeResult` / `ErrorCode`（`usize`）；`get_error_name` |
| `zstd` | 转为 `std::io::Error` |

C 习惯：成功返回正数字节数，失败返回特殊错误码，**非** errno。

### 6.2 `unsafe` 集中处

- `zstd-sys`：全部 FFI。
- `zstd-safe`：每个包装函数内单次 `unsafe` 调 C；`WriteBuf::write_from` 闭包内。
- `zstd`：调用 `zstd-safe`，少量 stream 内部 unsafe。

### 6.3 安全契约

- 调用者通过 `CCtx`/`DCtx` 所有权保证指针有效。
- 输入/输出缓冲区生命周期须在调用期间有效（`WriteBuf` 抽象约束）。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **线程** | 单上下文需外部同步；`zstdmt` feature 在 C 库内用线程池 |
| **`Send`/`Sync`** | `DCtx` 显式 `Send`+`Sync`；压缩上下文类似（需查 `CCtx` — 同模式 `inferred`） |
| **async** | **N/A** — 本 crate 同步；README 推荐 `async-compression` |
| **wasm** | `wasm-shim` 补 C 标准库；`wasm32` 强制 `std` cfg |

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| 单元测试 | `zstd`、`zstd-safe` 内往返测试 |
| fuzz | `zstd-safe/fuzz/`（压缩/流/字典等，未逐文件深读） |
| CI | `.github/workflows/linux.yml`、`windows.yml`、`macos.yml`、`wasm.yml` |
| Examples | §1.3；展示 stream/bulk，非底层 FFI |

**硬件依赖：** 无。

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `zstd-rs/Cargo.toml` | 三层 path 依赖、feature 透传 |
| `zstd-rs/zstd-safe/zstd-sys/Cargo.toml` | `links`、include 白名单、default features |
| `zstd-rs/zstd-safe/zstd-sys/build.rs` | vendored 编译、bindgen、pkg-config、wasm |
| `zstd-rs/zstd-safe/zstd-sys/src/lib.rs` | 预生成绑定 include 逻辑 |
| `zstd-rs/zstd-safe/zstd-sys/zstd.h` | bindgen 头入口 |
| `zstd-rs/zstd-safe/src/lib.rs` | `CCtx`/`DCtx`、`SafeResult`、`Drop` |
| `zstd-rs/zstd-safe/src/seekable.rs` | seekable RAII |
| `zstd-rs/src/lib.rs` | stream 重导出、`io::Error` 映射 |
| `zstd-rs/Readme.md` | async-compression 指引、submodule 说明 |
| `zstd-rs/examples/stream.rs` | 典型高层用法 |

---

## 10. 架构决策推断

### 10.1 三层 crate 拆分

| 字段 | 内容 |
|------|------|
| **决策** | `zstd-sys` → `zstd-safe` → `zstd` |
| **C 侧事实** | API 面大但模式统一（上下文 + 流）；适合中间层统一 `ZSTD_isError` |
| **Rust 侧事实** | 下游可选依赖 `-sys` 或 `safe`；顶层专注 `std::io` |
| **推断动机** | 分离 FFI 维护与 idiomatic Rust；与 libpng-zstd 生态常见三层一致 |
| **证据** | 三 `Cargo.toml`、`Readme.md` |
| **置信度** | `inferred` |

### 10.2 预生成绑定 + 可选 bindgen

| 字段 | 内容 |
|------|------|
| **决策** | 默认提交 `bindings_*.rs`；`bindgen` feature 用于再生/定制 |
| **C 侧事实** | 实验 API 依赖额外宏；多 header 组合 |
| **Rust 侧事实** | feature 矩阵对应多份预生成文件 |
| **推断动机** | 避免用户构建依赖 libclang；CI/docs.rs 可预测 |
| **证据** | `zstd-sys/src/lib.rs`、`build.rs` `generate_bindings` |
| **置信度** | `inferred` |

### 10.3 默认 vendored + 符号隐藏

| 字段 | 内容 |
|------|------|
| **决策** | 内嵌 zstd 子模块；`-fvisibility=hidden` |
| **C 侧事实** | 静态链接常见；xxhash 私有 API 模式 |
| **Rust 侧事实** | `compile_zstd()` 扫目录编 `.c`；issue #58 注释 |
| **推断动机** | 可复现构建；避免与应用内其他 zstd 副本符号冲突 |
| **证据** | `zstd-sys/build.rs` |
| **置信度** | `confirmed`（注释）+ `inferred`（动机） |

### 10.4 `zstd-safe` 薄封装而非直接 `zstd` 调 sys

| 字段 | 内容 |
|------|------|
| **决策** | `zstd-safe` 提供 `SafeResult` 与 `NonNull` 上下文 |
| **C 侧事实** | 错误码与成功返回值共用 `size_t` |
| **Rust 侧事实** | 统一 `parse_code`；`DCtx` 的 `Send`/`Sync` 文档化 |
| **推断动机** | 避免每个高层 API 重复 `ZSTD_isError` 检查 |
| **证据** | `zstd-safe/src/lib.rs` |
| **置信度** | `inferred` |

---

## 11. 待澄清

- 默认 release 是否启用 `bindgen` feature（`zstd-sys` default 含 `bindgen` — 与预生成文件如何协同，需构建日志确认，静态阅读见 `Cargo.toml` default）。
- `CCtx` 的 `Sync`/`Send` 实现是否与 `DCtx` 完全对称（未全文检索）。
- `fat-lto`/`thin-lto` 在实际 CI 矩阵中的启用范围。
