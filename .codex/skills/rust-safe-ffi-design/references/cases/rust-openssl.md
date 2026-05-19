# rust-openssl — FFI 单项目分析

**分析对象（源码路径）：** `rust-openssl/`  
**计划序号：** P3（阶段 A3）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`rust-openssl` 绑定 **OpenSSL**（及 LibreSSL、BoringSSL、AWS-LC 等兼容分支），面向 TLS/密码学 Rust 应用。架构为 **多 crate workspace**：`openssl-sys`（FFI + 复杂构建/版本探测）+ `openssl`（safe，广泛模块）+ 辅助 `openssl-macros`、`openssl-errors` + `systest`。Safe 层大量使用 **`foreign-types`** 管理 opaque 指针与 `Drop`。

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| `openssl-sys` | `openssl-sys/` | `links = "openssl"`；FFI + `build/main.rs` |
| `openssl` | `openssl/` | Safe 公开 API |
| `openssl-macros` | `openssl-macros/` | 过程宏（文档 `corresponds`） |
| `openssl-errors` | `openssl-errors/` | 自定义 OpenSSL 错误库支持 |
| `systest` | `systest/` | ABI 测试（`ctest`） |

`Cargo.toml`：`resolver = "2"`，`members` 含上述五者（`confirmed`）。

### 1.2 主要目录（按 crate）

**openssl-sys**

| 路径 | 内容 |
|------|------|
| `build/main.rs` | 发现库、版本 `cfg`、bindgen 调度 |
| `build/find_normal.rs` / `find_vendored.rs` | 系统 vs `openssl-src` |
| `build/run_bindgen.rs` | bindgen 输入头列表 |
| `src/*.rs` | 分模块 FFI（`ssl.rs`、`evp.rs`、`x509.rs` 等） |
| `src/handwritten/` | 手写补充（宏、static inline、栈类型） |

**openssl**

| 路径 | 内容 |
|------|------|
| `src/ssl/` | TLS 连接、上下文、回调 |
| `src/x509/` | 证书、存储、验证 |
| `src/pkey/`、`rsa/`、`ec/` 等 | 密钥与算法 |
| `src/error.rs` | `ErrorStack` |
| `build.rs` | 次要构建逻辑 |

### 1.3 Examples

**无** 根级 `examples/` 目录。用法以 `openssl` crate 文档与 `dev-dependencies` 测试为主（`confirmed`）。

### 1.4 Tests

| 类型 | 位置 | 说明 |
|------|------|------|
| `systest` | `systest/` | 布局/符号；feature `vendored`、`bindgen` |
| 模块测试 | `openssl/src/**/tests.rs`、`ssl/test/` | 大量单元/集成测试 |
| FIPS/AWS-LC 等 | feature  gated 测试 | 需对应后端 |

---

## 2. 原生依赖

### 2.1 库画像

| 字段 | 内容 |
|------|------|
| **C 库** | OpenSSL（libcrypto + libssl）；可选 LibreSSL / BoringSSL / AWS-LC |
| **API 形态** | 大量 opaque 指针（`SSL*`、`EVP_PKEY*`、`X509*`）；OpenSSL 3.x **provider** 模型；错误栈 `ERR_*`；许多 **宏** 与 **static inline** |
| **资源对** | `*_new` / `*_free` 或 `*_up_ref` 引用计数；栈 `OPENSSL_sk` |
| **线程安全** | OpenSSL 1.1.0+ 线程安全需正确 init；1.1.1b+ `OPENSSL_INIT_NO_ATEXIT` |
| **分发方式** | pkg-config / Homebrew / vcpkg；`vendored` → `openssl-src`；BoringSSL/AWS-LC 由独立 `-sys` crate 提供 |
| **绑定难点** | 多版本 `cfg`；宏/inline 需 handwritten；STACK_OF 泛型；ABI 随发行版差异 |

### 2.2 `links`

- `openssl-sys`：`links = "openssl"`（`confirmed`）
- BoringSSL / AWS-LC 路径 early exit，使用 `bssl-sys` / `aws-lc-sys` 的 link name（`build/main.rs`）

### 2.3 发现与链接（摘要）

```
check_ssl_kind → boringssl / awslc 分支（专用 sys crate）
else find_openssl:
  vendored (unless OPENSSL_NO_VENDOR) → openssl-src
  else find_normal → pkg-config / 环境变量 / vcpkg
postprocess → 大量 osslXXX / libresslXXX cfg
可选 bindgen → OUT_DIR/bindings.rs
默认 → 手写模块 + handwritten/
```

环境变量：`OPENSSL_DIR`、`OPENSSL_STATIC`、`TARGET_OPENSSL_*` 等（`openssl` crate 文档 `confirmed`）。

### 2.4 Feature（`openssl` / `openssl-sys`）

| Feature | 作用 |
|---------|------|
| `vendored` | 静态编译 OpenSSL（`openssl-src`） |
| `bindgen` | 构建时 bindgen 替代部分手写 |
| `unstable_boringssl` | `bssl-sys` |
| `aws-lc` / `aws-lc-fips` | AWS-LC 系列 |

---

## 3. 绑定生成

### 3.1 **混合**：手写模块 + `handwritten/` + 可选 bindgen

- 默认：`openssl-sys/src/` 分文件 `include!` 式模块 + `handwritten/*` 补宏/inline（`confirmed` `src/lib.rs`）。
- `bindgen` feature：`run_bindgen.rs` 列出大量 `#include <openssl/*.h>`，按版本条件包含 QUIC、provider 等（`confirmed`）。
- BoringSSL：可 `include!(OUT_DIR/bindings.rs)` 或 `bssl-sys`（`src/lib.rs`）。
- AWS-LC：pregenerated + 纯 Rust 实现部分 `ERR_GET_*` 宏（`awslc_pregenerated` cfg）。

### 3.2 C 头与 API 形态

- SSL：`SSL_CTX` 配置 + `SSL` 连接；回调（verify、info、ALPN 等）。
- EVP：高层密码操作 OpenSSL 3 推荐路径。
- 错误：线程局部错误栈，非简单返回码。

### 3.3 版本适配

- `build/main.rs` 打印数十个 `cargo:rustc-cfg=ossl*` / `libressl*`（`confirmed`）。
- `build/expando.c` 探测 OpenSSL 编译选项 → `osslconf`（用于裁剪不支持算法）。

### 3.4 再生流程

- 维护者可用 `bindgen` feature 再生；日常依赖手写 + systest。
- `openssl-macros` 的 `corresponds` 链到 OpenSSL man 页，非绑定生成。

---

## 4. 分层与公开 API

### 4.1 `openssl-sys`

- 纯 FFI；`init()` / `assume_init()`（`Once` + `OPENSSL_init_ssl`）（`confirmed`）。
- 按 `cfg(openssl)` / `boringssl` / `awslc` 切换整模块（`src/lib.rs`）。

### 4.2 `openssl`（safe）— 模块树摘要

| 模块 | 职责 |
|------|------|
| `ssl` | `SslConnector`、`SslStream`、上下文、回调 |
| `x509` | 证书、CRL、扩展、验证 store |
| `pkey` / `rsa` / `ec` / `dsa` / `dh` | 非对称密钥 |
| `symm` / `cipher` / `md` | 对称与摘要 |
| `error` | `ErrorStack` |
| `stack` | `Stack<T>` 包装 `OPENSSL_STACK` |
| `rand`、`pkcs7`、`pkcs12`、`cms`、`ocsp` 等 | 专题 API |
| `provider`、`lib_ctx` | OpenSSL 3 provider 模型 |

**模式：** `foreign_types::ForeignType` / `ForeignTypeRef` + `Opaque` 新类型；`Stackable` trait 约束栈元素。

### 4.3 `openssl-macros`

- `#[corresponds(fn_name)]`：为 safe 函数生成指向 OpenSSL 文档的 doc 链接（`openssl-macros/src/lib.rs`）。

### 4.4 `openssl-errors`

- 允许注册 **自定义错误库** 与 OpenSSL `ERR` 机制集成（README/描述；面向扩展场景）。

### 4.5 上层适配

无独立 tonic/quinn 式适配 crate；作为 **依赖项** 被 `curl-sys`、`reqwest` 等使用。

---

## 5. 资源与生命周期

| 模式 | 示例 | Rust 处理 |
|------|------|-----------|
| 单一所有者 | `SSL`、`X509` | `ForeignType` → `Drop` 调 `*_free` |
| 引用计数 | `EVP_PKEY`、`X509` up_ref | `clone` 增加引用 |
| 借用引用 | `SslRef` | `ForeignTypeRef`，不拥有 |
| 栈结构 | `Stack<T>` | `Stack`/`StackRef` + `Stackable` |

**风险点：**

- 回调从 C 进入 Rust 时须保证 `Ssl`/`SslCtx` 指针在回调期间有效（`ssl/callbacks.rs`）。
- `Signer`/`Verifier` 等带生命周期的包装显式 `Send`/`Sync`（`sign.rs`）。
- 多版本下部分 API `cfg` 不存在 — 误用会在编译期排除，但 feature 组合复杂（`inferred`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

- 主类型：`Result<T, ErrorStack>`（`error.rs`）。
- `ErrorStack::get()` Drain OpenSSL 线程错误队列为 `Vec<Error>`。
- I/O 错误可 `From<ErrorStack>` 映射（`confirmed` 模块文档）。

### 6.2 `unsafe` 集中处

- `openssl-sys`：全部 FFI。
- `openssl`：每个 `ForeignType::drop`、回调、指针解引用、`init()` 调用点。
- 安全契约：文档 `# Safety` + OpenSSL 原版语义；`corresponds` 宏链文档。

### 6.3 C 错误习惯

- 多数返回 `1`/`0` 或指针 NULL；细节在 `ERR` 栈。
- 部分 API 返回 `long` 错误码（BoringSSL/AWS-LC 差异由 `cfg` 处理）。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **初始化** | `openssl_sys::init()` — `OPENSSL_init_ssl`（OpenSSL）；LibreSSL no-op；AWS-LC `CRYPTO_library_init`（`confirmed` 各分支） |
| **调用点** | 各模块 `new` 路径常调 `ffi::init()`（惰性）（`grep` 可见） |
| **`Send`/`Sync`** | 按类型实现，如 `Cipher: Send+Sync`、`Stack` 条件实现（`symm.rs`、`stack.rs`） |
| **async** | **N/A** — `SslStream` 为同步 `Read`/`Write`；async TLS 由其他 crate（如 tokio-rustls）负责 |
| **FIPS** | `fips` 模块（feature/版本相关） |

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| `systest` | `ctest` + `openssl-sys`；验证 C 布局 |
| `openssl` 内测 | SSL 握手、证书、算法向量等，数量大 |
| `ssl/test/server.rs` | 测试用 TLS server 辅助 |
| Examples | 无独立 examples 目录；文档示例在 rustdoc |
| CI | 仓库含 GitHub Actions（未在本分析运行） |

**硬件依赖：** 无；部分 SSL 测试需 localhost 或网络（`inferred`）。

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `rust-openssl/Cargo.toml` | workspace 五成员 |
| `rust-openssl/openssl-sys/Cargo.toml` | `links`、vendored/aws-lc features |
| `rust-openssl/openssl-sys/build/main.rs` | 发现逻辑、版本 cfg、SSL 种类分支 |
| `rust-openssl/openssl-sys/build/run_bindgen.rs` | bindgen 头列表 |
| `rust-openssl/openssl-sys/src/lib.rs` | openssl/boringssl/awslc 模块切换、`init` |
| `rust-openssl/openssl-sys/src/handwritten/` | 手写补充绑定 |
| `rust-openssl/openssl/Cargo.toml` | `foreign-types`、`ffi` 依赖 |
| `rust-openssl/openssl/src/error.rs` | `ErrorStack` 模型 |
| `rust-openssl/openssl/src/ssl/mod.rs` | TLS safe 层入口 |
| `rust-openssl/openssl/src/stack.rs` | `Stack`/`Stackable` |
| `rust-openssl/openssl-macros/src/lib.rs` | `corresponds` 宏 |
| `rust-openssl/openssl-errors/Cargo.toml` | 自定义错误库 crate |
| `rust-openssl/systest/Cargo.toml` | ABI 测试 |

---

## 10. 架构决策推断

### 10.1 Workspace 拆分 sys / safe / macros / errors

| 字段 | 内容 |
|------|------|
| **决策** | 多 crate；`openssl` 仅依赖 `openssl-sys` + 小宏 crate |
| **C 侧事实** | API 面极大，分 libcrypto/libssl |
| **Rust 侧事实** | `openssl-sys` 可单独被本地工具使用；宏与错误扩展独立发布 |
| **推断动机** | 控制编译时间、semver（sys 0.9 vs openssl 0.10）、可选功能隔离 |
| **证据** | 根 `Cargo.toml`、各 crate `Cargo.toml` |
| **置信度** | `inferred` |

### 10.2 手写 + handwritten + 可选 bindgen + 海量 version cfg

| 字段 | 内容 |
|------|------|
| **决策** | 默认手写；难绑部分进 `handwritten/`；bindgen 可选 |
| **C 侧事实** | 宏、`STACK_OF`、多版本符号差异 |
| **Rust 侧事实** | `ossl300` 等 cfg 裁剪 API；`expando.c` 探测 |
| **推断动机** | 控制绑定质量与编译时间；bindgen 用于追赶新版本 |
| **证据** | `openssl-sys/src/lib.rs`、`build/run_bindgen.rs` |
| **置信度** | `inferred` |

### 10.3 `foreign-types` 作为 safe 层核心抽象

| 字段 | 内容 |
|------|------|
| **决策** | `ForeignType`/`ForeignTypeRef`/`Opaque` 贯穿 |
| **C 侧事实** | 指针即句柄；无统一「对象头」 |
| **Rust 侧事实** | `ssl`、`x509`、`evp` 等模块一致模式 |
| **推断动机** | 减少样板 `Drop`/`Clone`；与 OpenSSL 指针语义对齐 |
| **证据** | `openssl/Cargo.toml`、`src/ssl/mod.rs` |
| **置信度** | `inferred` |

### 10.4 多 SSL 后端（OpenSSL / LibreSSL / BoringSSL / AWS-LC）

| 字段 | 内容 |
|------|------|
| **决策** | `build/main.rs` 分支 + 不同 `cfg` 模块 |
| **C 侧事实** | 分叉 ABI 与宏差异 |
| **Rust 侧事实** | `bssl-sys`、`aws-lc-sys` 重导出或 bindgen |
| **推断动机** | 满足 Linux 发行版、FIPS、Chrome 系栈等部署需求 |
| **证据** | `openssl-sys/build/main.rs`、`src/lib.rs` |
| **置信度** | `confirmed`（代码结构）+ `inferred`（产品动机） |

### 10.5 `ErrorStack` 而非 errno 风格

| 字段 | 内容 |
|------|------|
| **决策** | 公开 API 普遍 `Result<_, ErrorStack>` |
| **C 侧事实** | OpenSSL 用 ERR 队列存多帧错误 |
| **Rust 侧事实** | `ErrorStack::get()` 一次性取出 |
| **推断动机** | 保留 C 库诊断信息；符合 Rust `Error` trait 链 |
| **证据** | `openssl/src/error.rs` |
| **置信度** | `confirmed` |

---

## 11. 待澄清

- 当前默认发布构建是否启用 `bindgen`（`openssl-sys` default features 无 bindgen — `Cargo.toml` 显示 bindgen 为 optional，默认手写路径）。
- `openssl-errors` 与主 crate 的典型集成路径（仅读 Cargo 描述，未读使用示例）。
- QUIC API（`ossl302` + `quic.h`）在 safe 层的暴露范围未逐项核对。
