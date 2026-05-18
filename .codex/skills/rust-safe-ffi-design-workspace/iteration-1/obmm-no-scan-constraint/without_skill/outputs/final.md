下面是一个**不探索代码库、不读取头文件/文档**前提下，给 `obmm` 做的最初级 Rust FFI 设计尝试。所有接口名、类型、生命周期都只是设计占位，必须等头文件确认后才能落地。

**目标**

为 `obmm` 提供两层 Rust 封装：

1. `obmm-sys`
   原始 FFI 绑定层，只负责暴露 C ABI。

2. `obmm`
   安全封装层，负责生命周期、错误处理、资源释放、类型转换。

**未知项**

- `obmm` 是否是 C ABI 库，还是 C++ 库。
- 是否有稳定的 `extern "C"` API。
- 是否需要初始化/反初始化函数，例如 `obmm_init()` / `obmm_shutdown()`。
- 是否有全局状态，是否线程安全。
- 资源句柄类型未知，例如 `obmm_context_t*`、`obmm_handle_t*`。
- 错误模型未知：返回 `int`、枚举、NULL、还是 `errno` 风格。
- 字符串编码未知：UTF-8、平台编码、还是二进制 buffer。
- 内存所有权未知：调用方释放、库释放、还是借用。
- 回调、异步、日志、配置结构体均未知。

**建议结构**

```text
obmm-rs/
  crates/
    obmm-sys/
      Cargo.toml
      build.rs
      src/lib.rs
    obmm/
      Cargo.toml
      src/lib.rs
      src/error.rs
      src/context.rs
```

**`obmm-sys` 设计**

职责：

- 链接原生 `obmm`。
- 暴露不安全 C 函数。
- 不做业务封装。
- 不猜测生命周期。
- 所有外部函数放在 `unsafe extern "C"` 中。

初始占位：

```rust
// obmm-sys/src/lib.rs

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]

use std::os::raw::{c_char, c_int, c_void};

#[repr(C)]
pub struct obmm_context_t {
    _private: [u8; 0],
}

unsafe extern "C" {
    pub fn obmm_init() -> c_int;
    pub fn obmm_shutdown() -> c_int;

    pub fn obmm_context_create(out: *mut *mut obmm_context_t) -> c_int;
    pub fn obmm_context_destroy(ctx: *mut obmm_context_t);

    pub fn obmm_last_error_message() -> *const c_char;
}
```

这些函数名只是占位，真实名称必须以后从头文件确认。

**`build.rs` 初始策略**

如果 `obmm` 已经安装在系统路径：

```rust
// obmm-sys/build.rs

fn main() {
    println!("cargo:rustc-link-lib=obmm");
}
```

如果需要本地路径，后续再加：

- `OBMM_LIB_DIR`
- `OBMM_INCLUDE_DIR`
- `pkg-config`
- `cmake`
- `vcpkg`

初始阶段不要过早引入 `bindgen`，除非头文件规模较大或 ABI 类型很多。

**安全层设计**

核心原则：

- Rust 用户不直接接触裸指针。
- 资源用 `Drop` 自动释放。
- fallible API 返回 `Result<T, ObmmError>`。
- 所有 `unsafe` 集中在很少几个模块。
- 先不承诺 `Send` / `Sync`，除非确认线程安全。

```rust
// obmm/src/context.rs

use std::ptr::NonNull;

pub struct Context {
    raw: NonNull<obmm_sys::obmm_context_t>,
}

impl Context {
    pub fn new() -> Result<Self, crate::ObmmError> {
        let mut raw = std::ptr::null_mut();

        let code = unsafe {
            obmm_sys::obmm_context_create(&mut raw)
        };

        if code != 0 {
            return Err(crate::ObmmError::from_code(code));
        }

        let raw = NonNull::new(raw).ok_or(crate::ObmmError::NullPointer)?;

        Ok(Self { raw })
    }

    pub(crate) fn as_ptr(&self) -> *mut obmm_sys::obmm_context_t {
        self.raw.as_ptr()
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            obmm_sys::obmm_context_destroy(self.raw.as_ptr());
        }
    }
}
```

**错误类型**

```rust
// obmm/src/error.rs

#[derive(Debug)]
pub enum ObmmError {
    Code(i32),
    NullPointer,
    InvalidString,
}

impl ObmmError {
    pub fn from_code(code: i32) -> Self {
        Self::Code(code)
    }
}
```

后续如果 `obmm` 有明确错误枚举，再改成强类型 enum。

**线程安全策略**

默认：

```rust
// 不实现 Send / Sync
```

除非确认：

- 每个 context 可跨线程移动，则可考虑 `unsafe impl Send for Context`。
- 同一个 context 可多线程共享，则可考虑 `unsafe impl Sync for Context`。

在没有文档前，不实现是保守选择。

**字符串策略**

Rust -> C：

- 使用 `CString`。
- 拒绝内部 NUL 字节。
- 暴露 `&str` API，不让用户手写 C 字符串。

C -> Rust：

- 如果返回 `const char*` 且库拥有内存：复制成 `String`。
- 如果返回需要释放的字符串：必须绑定对应释放函数，例如 `obmm_free_string()`。
- 如果长度单独返回：优先按 `*const u8 + len` 处理。

未知释放规则前，不设计公开字符串返回 API。

**内存所有权策略**

建议规则：

- `create/open/new` 对应 Rust `struct`。
- `destroy/free/delete` 放进 `Drop`。
- 借用出来的指针不暴露给用户。
- 从 C 返回的 buffer 必须确认释放函数后再封装。

**最小公开 API 草案**

```rust
pub use error::ObmmError;

pub struct Context { /* private */ }

impl Context {
    pub fn new() -> Result<Self, ObmmError>;
}
```

先只交付初始化和 context 生命周期。不要一开始包装全部 API。

**验证计划**

在拿到真实头文件后，第一轮验证应包括：

- `cargo check -p obmm-sys`
- `cargo check -p obmm`
- 创建并销毁 context 的 smoke test
- 错误路径测试
- 若有动态库，运行时确认 loader 能找到 `libobmm.so` / `obmm.dll` / `libobmm.dylib`

**当前结论**

这版方案只能作为 `obmm` 的 Rust FFI 起点：先建立 `obmm-sys` 与安全层分离，围绕 opaque handle、`Drop`、`Result`、保守线程安全来设计。所有具体函数签名、错误码、链接方式、字符串和内存释放规则都必须等真实 ABI 信息确认后再定。