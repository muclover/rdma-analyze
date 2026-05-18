# curl-rust — FFI 单项目分析

**分析对象（源码路径）：** `curl-rust/`  
**计划序号：** P2（阶段 A2）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`curl-rust` 绑定 **libcurl**，面向需要 HTTP/HTTPS 等协议客户端能力的 Rust 应用。架构为 **两层**：`curl-sys`（`-sys`，手写 FFI + 复杂 `build.rs`）+ 顶层 `curl`（safe API，模块 `easy` / `multi`）。默认倾向 **内嵌编译 vendored libcurl**（无系统库或 feature 不满足时），也可链接系统 `libcurl`（macOS / pkg-config / vcpkg）。

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| 主 crate | 根 `Cargo.toml` → `curl` | Safe 封装：`easy`、`multi`、错误、版本探测 |
| `curl-sys` | `curl-sys/` | `links = "curl"`；手写 `lib.rs` 绑定；vendored `curl/` 子模块 |
| `systest` | `systest/` | ABI/布局与 C 头一致性（`ctest2`） |

根 `Cargo.toml` 仅 `members = ["systest"]`；`curl` 与 `curl-sys` 为 path 依赖，非 workspace 多 member 发布形态（`confirmed`）。

### 1.2 主要目录

| 路径 | 内容 |
|------|------|
| `curl-sys/lib.rs` | 全部 `extern "C"` 与类型别名（单文件，~1100+ 行） |
| `curl-sys/build.rs` | 系统库探测 / vendored `cc` 编译 libcurl |
| `curl-sys/curl/` | vendored libcurl 源码（submodule） |
| `src/lib.rs` | 全局 `init`、`Error`、模块导出 |
| `src/easy/` | `Easy` / `Easy2` / `Transfer`、表单、列表、回调 |
| `src/multi.rs` | `CURLM` 多路复用 |
| `src/error.rs` | `CURLcode` → `Error` |
| `src/panic.rs` | C 回调内 panic 捕获 |
| `examples/` | 7 个 Rust 示例 |
| `.github/workflows/main.yml` | CI |

### 1.3 Examples（简单阅读）

| 示例 | 文件 | 是否演示 FFI |
|------|------|----------------|
| `https` | `examples/https.rs` | 是 — `Easy` 基本 GET |
| `multi-dl` | `examples/multi-dl.rs` | 是 — `Multi` 并发下载 |
| `ssl_proxy` / `ssl_cert_blob` / `ssl_client_auth` | 对应 `examples/*.rs` | 是 — TLS/证书选项 |
| `doh` | `examples/doh.rs` | 是 — DNS over HTTPS |
| `aws_sigv4` | `examples/aws_sigv4.rs` | 是 — 签名请求（需 `static-curl` + `ssl`） |

vendored `curl-sys/curl/docs/examples/` 为上游 C 示例，本分析不逐例深读。

### 1.4 Tests

| 类型 | 位置 | 说明 |
|------|------|------|
| 单元/集成 | `tests/`（如 `atexit`） | Rust 层行为 |
| `systest` | `systest/` | C/Rust ABI 对照 |
| 上游 | `curl-sys/curl/tests/` | vendored，不在 Rust crate 直接跑 |

---

## 2. 原生依赖

### 2.1 库画像

| 字段 | 内容 |
|------|------|
| **C 库** | libcurl（客户端 URL 传输） |
| **API 形态** | Opaque `CURL` / `CURLM` handle；`curl_easy_setopt` 变参选项；大量 C 回调（read/write/header/ssl_ctx 等）；`CURLcode` 错误码 |
| **资源对** | `curl_easy_init` ↔ `curl_easy_cleanup`；`curl_multi_init` ↔ `curl_multi_cleanup`；`curl_slist` / `curl_httppost` 需单独释放 |
| **线程安全** | 需 `curl_global_init` 一次（主线程）；单 `CURL` 句柄非线程安全；`CURLSH` 可共享（本 crate 未重点封装） |
| **分发方式** | 默认 vendored 静态编译；macOS 系统 `-l curl`；Unix `pkg-config`；Windows `vcpkg`；`static-curl` 强制源码构建 |
| **绑定难点** | `CURLOPT` 类型多样；回调从 C 进入 Rust；`curl_httppost` 布局不稳定故字段未绑定；TLS 后端可选（OpenSSL / Schannel / rustls 等） |

### 2.2 `links` 与传递依赖

- `curl-sys`：`links = "curl"`（`confirmed`）
- 传递：`libz-sys`（zlib）、可选 `openssl-sys`、`libnghttp2-sys`、`rustls-ffi`（`curl-sys/Cargo.toml`）

### 2.3 链接策略（`curl-sys/build.rs` 摘要）

```
force-system-lib-on-osx (apple) → -l curl
!static-curl:
  apple (+ http2 检查 curl-config) → -l curl
  windows → vcpkg probe
  else → pkg-config libcurl（http2 feature 需 curl-config 含 HTTP2）
失败或 static-curl → vendored cc 编译，cargo:rustc-cfg=libcurl_vendored
```

vendored 构建时复制头文件到 `OUT_DIR`、生成 `libcurl.pc`、大量 `CURL_DISABLE_*` 裁剪协议、定义 `CURL_STATICLIB`（`confirmed` 自 `build.rs`）。

### 2.4 Feature 矩阵（顶层 `curl` / `curl-sys`）

| Feature | 作用 |
|---------|------|
| `ssl`（default） | OpenSSL + `curl-sys/ssl` |
| `http2` | nghttp2 |
| `static-curl` | 强制内嵌 libcurl |
| `static-ssl` / `windows-static-ssl` | 静态 OpenSSL |
| `rustls` | rustls-ffi 后端 |
| `protocol-ftp`、`ntlm`、`spnego` 等 | 透传编译选项 |

Unix 上 `curl` 可选依赖 `openssl-sys` + `openssl-probe`（证书路径）；Windows MSVC 用 `schannel`。

---

## 3. 绑定生成

### 3.1 方式：**手写**，非 bindgen

- 全部绑定在 `curl-sys/lib.rs`：`#[repr(C)]` 有限、opaque `enum CURL {}`、`extern "C"` 函数、`CURLoption`/`CURLcode` 等类型别名（`confirmed`）。
- **无 `wrapper.h`**：直接对照 vendored `curl/include/curl/*.h` 维护。
- `curl_httppost` 仅 opaque enum，注释说明字段跨版本不稳定（`confirmed`）。

### 3.2 C 头暴露的主要 API 形态（§4.4）

自 `curl/include/curl/easy.h` 等：

- **Easy API**：`curl_easy_init` / `setopt` / `perform` / `cleanup` — 单连接阻塞传输。
- **Multi API**：`multi.h` — 应用驱动 I/O 的多句柄。
- **回调契约**：write/read/header/progress/ssl 等从 C 调用用户函数，返回值控制继续/中止。

### 3.3 再生流程

无 bindgen 流水线；升级 libcurl 需同步改 `lib.rs` 与 `build.rs` 源文件列表。`systest` 用于防 ABI 漂移。

---

## 4. 分层与公开 API

### 4.1 `curl-sys`

- 薄层：几乎 1:1 暴露 libcurl C API + 常量。
- 条件 `extern crate`：`link_libz` / `link_openssl` / `link_libnghttp2` 由 `build.rs` 打印的 cfg 控制（`confirmed`）。

### 4.2 `curl`（safe）

| 模块 | 职责 |
|------|------|
| `easy` | `Easy`（闭包回调）、`Easy2<H: Handler>`（泛型 handler）、`Transfer`（栈上回调，免 `'static`） |
| `multi` | `Multi`、`EasyHandle` / `Easy2Handle`、socket/timer 回调 |
| `error` | `Error`（`CURLcode`）、`MultiError`、`ShareError`、`FormError` |
| `version` | `Version::get()` 运行时能力探测 |

**设计要点：**

- `Easy` 包装 `Easy2<EasyData>`，用 `Cell`/闭包实现 C 回调到 Rust。
- `Transfer` 允许非 `'static` 回调，仅限单次 `perform` 作用域。
- `Easy2` + `Handler` trait 面向 multi / 异步风格集成（文档称 async I/O 场景）。

### 4.3 上层适配

无独立「协议适配」crate；TLS 通过 feature 选后端，不在本仓库再包一层。

---

## 5. 资源与生命周期

| 资源 | 创建 | 释放 | Rust 封装 |
|------|------|------|-----------|
| `CURL` | `curl_easy_init` | `curl_easy_cleanup` | `Easy2` 的 `Drop`（`handler.rs`） |
| `CURLM` | `curl_multi_init` | `curl_multi_cleanup` | `RawMulti` 的 `Drop`（`multi.rs`） |
| `curl_slist` | `curl_slist_append` | `curl_slist_free_all` | `easy/list.rs` `List` |
| `curl_httppost` | form API | `curl_formfree` | `easy/form.rs` `Form` |
| 全局 | `curl_global_init` | **故意不调用** `curl_global_cleanup` | `init()` + `INIT_CTOR`（`lib.rs`） |

**风险点：**

- 加入 `Multi` 后 easy 句柄须先 `curl_multi_remove_handle` 再 cleanup — `DetachGuard` 保证顺序（`multi.rs`）。
- 全局清理因线程安全文档放弃，进程退出依赖 OS 回收（`inferred`）。
- C 回调中 panic 经 `panic::catch` 转为错误/中止，避免跨 FFI unwind（`panic.rs`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

- 主路径：`CURLcode` → `Error::new`，可选 `CURLOPT_ERRORBUFFER` 文本经 `set_extra` 附加（`error.rs`、`handler.rs` `cvt`）。
- 提供大量 `is_*` 方法对应各 `CURLE_*`（`confirmed`）。

### 6.2 `unsafe` 集中处

- `curl-sys`：整个 crate 为 unsafe 调用面。
- `curl`：`perform`、`setopt`、回调 trampoline、`Version` 读 C 字符串、`multi::action` 等。
- 契约：回调须可重入 libcurl 规则；`userptr` 指向的 `Inner<H>` 生命周期由 `Easy2` 拥有。

### 6.3 C 错误习惯

- 多数 API 返回 `CURLcode`，`CURLE_OK` 为成功。
- Multi 侧另有 `CURLMcode` → `MultiError`。

---

## 7. 并发与 async

| 项 | 结论 |
|----|------|
| **全局初始化** | `std::sync::Once`；Linux/macOS/Windows 用 `.init_array` / `.CRT$XCU` 构造函数提前 `init()`（`confirmed`） |
| **OpenSSL** | `need_openssl_init` 时 `openssl_sys::init()` + `openssl_probe`（`lib.rs`） |
| **`Send`/`Sync`** | `EasyData: Send`；`MultiWaker: Send + Sync`；加入 multi 的 easy 通过 `PhantomData` 失去 `Send` |
| **async** | **无** `async`/`.await`；`multi` 为同步事件循环（`poll`/`select` 风格），可与外部 runtime 手动集成 |
| **线程** | 文档要求 `curl_global_init` 在主线程、早于其他线程；crate 尽量自动满足 |

---

## 8. 测试与示例

| 类型 | 说明 |
|------|------|
| `systest` | `ctest2` 对照 C 头；feature `static-ssl` 等 |
| Rust `tests/` | 含 `atexit` 等特殊 harness |
| CI | `.github/workflows/main.yml` — 多 feature/平台矩阵（未运行验证） |
| Examples | 见 §1.3；主流程均通过 safe API，无直接 `curl_sys` 演示 |

**硬件依赖：** 无；网络测试依赖 CI 环境可达性（`inferred`）。

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `curl-rust/Cargo.toml` | 两层依赖、feature 矩阵、examples 声明 |
| `curl-rust/curl-sys/Cargo.toml` | `links`、传递依赖、feature |
| `curl-rust/curl-sys/build.rs` | 链接决策树、vendored 编译、pkg-config/http2 探测 |
| `curl-rust/curl-sys/lib.rs` | 手写绑定、opaque 类型、条件 link crate |
| `curl-rust/curl-sys/curl/include/curl/easy.h` | C easy API 形态 |
| `curl-rust/src/lib.rs` | `init` / `INIT_CTOR`、不调用 global_cleanup |
| `curl-rust/src/easy/handler.rs` | `Easy2` Drop、`curl_easy_cleanup`、回调 `cvt` |
| `curl-rust/src/easy/handle.rs` | `Easy` / `Transfer` 设计 |
| `curl-rust/src/multi.rs` | `Multi`、`DetachGuard`、socket 回调 |
| `curl-rust/src/error.rs` | `CURLcode` 错误封装 |
| `curl-rust/src/panic.rs` | C 回调 panic 边界 |
| `curl-rust/systest/` | ABI 测试策略 |
| `curl-rust/examples/https.rs` | 典型 safe 用法入口 |

---

## 10. 架构决策推断

### 10.1 独立 `curl-sys` + 手写绑定

| 字段 | 内容 |
|------|------|
| **决策** | `-sys` crate 单文件手写 FFI，不用 bindgen |
| **C 侧事实** | API 稳定但宏/选项极多；`CURLOPT` 需大量 Rust 常量 |
| **Rust 侧事实** | `curl-sys/lib.rs` 维护；`systest` 防漂移 |
| **推断动机** | libcurl 绑定成熟、变更可控；手写可避免 bindgen 对宏/变参的噪声；与 alexcrichton 系 `-sys` 风格一致 |
| **证据** | `curl-sys/lib.rs`、`systest/` |
| **置信度** | `inferred` |

### 10.2 默认 vendored libcurl + 多后端探测

| 字段 | 内容 |
|------|------|
| **决策** | 系统库优先（macOS/pkg-config），否则源码编译；feature 控制 TLS/HTTP2 |
| **C 侧事实** | 编译期 `#define` 决定协议与 SSL 后端；运行时 `curl_version_info` |
| **Rust 侧事实** | `build.rs` 长列表源文件 + `CURL_DISABLE_*`；`Version` 运行时 feature 位 |
| **推断动机** | 保证 CI/Windows 可复现构建；同时尊重发行版自带 libcurl；HTTP2 等与系统包能力对齐 |
| **证据** | `curl-sys/build.rs`、`src/version.rs` |
| **置信度** | `inferred` |

### 10.3 `Easy` / `Easy2` / `Transfer` 三轨 API

| 字段 | 内容 |
|------|------|
| **决策** | 闭包版 `Easy`、泛型 `Easy2`、作用域 `Transfer` 并存 |
| **C 侧事实** | 回调 `userptr` + 非 `'static` 栈数据在 C 中合法但 Rust 默认要求 `'static` |
| **Rust 侧事实** | `Transfer` 放宽 bounds；`Easy2` 供 multi；文档区分 sync/async 集成 |
| **推断动机** | 兼顾易用性与类型安全；避免全局 `Box<dyn>` 仅为了 `'static` |
| **证据** | `src/easy/handle.rs`、`src/easy/handler.rs` |
| **置信度** | `inferred` |

### 10.4 全局 init 且无 global_cleanup

| 字段 | 内容 |
|------|------|
| **决策** | `Once` + 平台构造函数；永不 `curl_global_cleanup` |
| **C 侧事实** | 文档要求 cleanup 时无其他线程 |
| **Rust 侧事实** | `lib.rs` 注释明确拒绝 cleanup |
| **推断动机** | Rust 库无法保证进程内线程状态；泄漏全局状态换安全 |
| **证据** | `src/lib.rs` |
| **置信度** | `confirmed`（注释直述） |

---

## 11. 待澄清

- vendored 构建默认启用的协议/TLS 组合与 docs.rs 展示是否一致（需对照具体 feature 默认矩阵）。
- `rustls` feature 与 OpenSSL 路径在 `build.rs` 中的互斥/优先级细节未全文核对。
- `Share` API（`CURLSH`）是否有计划中的 safe 封装 — 当前 crate 未暴露模块。
