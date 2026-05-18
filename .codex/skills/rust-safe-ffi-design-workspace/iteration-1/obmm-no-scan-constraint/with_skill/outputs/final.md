已按你指定的 `rust-safe-ffi-design` 方向输出。说明：我尝试读取该 `SKILL.md`，但当前沙箱执行命令失败：`bwrap: loopback: Failed RTM_NEWADDR: Operation not permitted`。因此下面基于该 skill 的已知适用范围执行：Rust FFI 架构、`-sys`/safe wrapper 分层、未知项显式标注；不探索代码库、不读头文件或文档。

**obmm Rust FFI 初级设计方案**

目标：先做一个最小可用、可发布前继续收敛的 Rust FFI 设计，不假设 obmm 的真实 API 细节。所有未确认内容都标记为 `UNKNOWN`，避免把猜测写成事实。

**1. 分层结构**

建议拆成两个 crate：

```text
obmm-sys
obmm
```

`obmm-sys`：

- 只负责暴露低层 C ABI bindings。
- 包含 `build.rs`，处理链接 obmm 原生库。
- 尽量不写业务逻辑。
- 所有 unsafe FFI 声明集中在这里。
- 若 obmm 只有 C++ API，则新增 C shim 层，不直接绑定 C++ ABI。

`obmm`：

- 面向 Rust 用户的安全封装。
- 封装生命周期、错误、字符串、资源释放、线程安全约束。
- 对外尽量不暴露裸指针。
- 所有 `unsafe` 留在内部小范围模块中。

**UNKNOWN**

- obmm 是否提供稳定 C ABI。
- obmm 是 C、C++、还是混合库。
- 是否已有动态库或静态库产物。
- 是否存在官方 release/package manager。
- 资源模型、错误模型、线程模型均未知。

**2. `obmm-sys` 设计**

初版目录：

```text
crates/
  obmm-sys/
    Cargo.toml
    build.rs
    src/lib.rs
  obmm/
    Cargo.toml
    src/lib.rs
```

如果存在 C 头文件，后续有两种绑定策略：

- `bindgen`：适合 API 面较大、头文件较稳定。
- 手写 bindings：适合 API 很小、只暴露少量核心函数。

本次由于不读头文件，初版建议先预留手写 bindings 结构：

```rust
// obmm-sys/src/lib.rs

#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(non_upper_case_globals)]

use std::os::raw::{c_char, c_int, c_void};

#[repr(C)]
pub struct obmm_context {
    _private: [u8; 0],
}

pub type obmm_status_t = c_int;

// UNKNOWN: function names and signatures are placeholders.
// Do not publish these until confirmed against real headers.
unsafe extern "C" {
    pub fn obmm_create(ctx: *mut *mut obmm_context) -> obmm_status_t;
    pub fn obmm_destroy(ctx: *mut obmm_context);
    pub fn obmm_last_error(ctx: *mut obmm_context) -> *const c_char;
}
```

这里的函数名只是设计占位，不代表 obmm 真实 API。

**3. 链接策略**

`build.rs` 初版只做最小链接：

```rust
fn main() {
    println!("cargo:rustc-link-lib=obmm");
}
```

后续根据实际分发方式扩展：

- `pkg-config`：如果 obmm 提供 `.pc` 文件。
- `vcpkg`：如果 Windows 支持较重要。
- bundled build：如果需要从源码构建 obmm。
- environment override：例如 `OBMM_LIB_DIR`、`OBMM_INCLUDE_DIR`。

**UNKNOWN**

- 库名是否为 `obmm`。
- 是否需要链接额外依赖。
- 是否支持 Windows/macOS/Linux。
- 是否有静态链接要求。
- 是否需要运行时查找动态库。

**4. 安全封装 API**

初版 Rust 用户 API 可以围绕一个 owning handle：

```rust
pub struct Context {
    raw: NonNull<obmm_sys::obmm_context>,
}
```

关键规则：

- `Context::new()` 负责创建原生资源。
- `Drop` 负责释放资源。
- 不暴露 `*mut obmm_context`。
- 所有 FFI 调用集中在 `impl Context` 内部。
- 每次调用后把状态码转换成 Rust `Result<T, Error>`。

示意：

```rust
pub type Result<T> = std::result::Result<T, Error>;

pub struct Context {
    raw: std::ptr::NonNull<obmm_sys::obmm_context>,
}

impl Context {
    pub fn new() -> Result<Self> {
        // unsafe block only around FFI boundary
        todo!("call obmm_create after real signature is known")
    }
}

impl Drop for Context {
    fn drop(&mut self) {
        // call obmm_destroy
    }
}
```

**5. 错误模型**

初版设计：

```rust
#[derive(Debug)]
pub struct Error {
    code: i32,
    message: Option<String>,
}
```

转换规则：

- 原生返回成功码时返回 `Ok`.
- 原生返回错误码时返回 `Err(Error)`.
- 如果 obmm 提供 last-error message，则拷贝成 Rust-owned `String`.
- 不让 Rust 持有 C 分配的错误字符串，除非明确释放规则。

**UNKNOWN**

- 成功码是否为 `0`。
- 错误码是否是 enum、int、errno 风格或回调风格。
- 错误消息生命周期未知。
- 是否有线程局部错误状态。

**6. 字符串和 buffer**

默认策略：

- Rust `&str` 输入转为 `CString`。
- 拒绝包含内部 NUL 的字符串。
- C 返回 `const char*` 时立即拷贝为 `String` 或 `Cow<'_, str>`，不长期借用。
- C 返回 buffer 时优先设计为 Rust 分配 buffer、C 写入，或者 C 分配后由对应 free 函数释放。

**UNKNOWN**

- obmm 使用 UTF-8、locale encoding、binary bytes 还是其他编码。
- 返回字符串是否需要手动释放。
- 是否存在二进制数据 API。

**7. 内存所有权**

FFI 设计必须为每类对象明确：

```text
Rust owns?
C owns?
borrowed?
must call free?
valid until next call?
thread-local?
```

初版原则：

- 由 obmm 创建的对象，由 obmm 销毁函数释放。
- Rust 不直接 `free` C 分配的内存。
- C 不保存 Rust 指针，除非 API 明确要求，并且 wrapper 固定生命周期。
- 对跨 FFI 的 callback/userdata 使用 pinned owner 或 registry，不能裸借用临时栈变量。

**UNKNOWN**

- obmm 是否有多种 handle 类型。
- 是否存在全局 init/shutdown。
- 是否有对象树或 parent-child ownership。

**8. 线程安全**

保守默认：

```rust
// Do not implement Send/Sync initially.
pub struct Context {
    raw: NonNull<obmm_sys::obmm_context>,
    _not_send_sync: PhantomData<Rc<()>>,
}
```

只有确认 obmm 文档说明线程安全后，才考虑：

```rust
unsafe impl Send for Context {}
unsafe impl Sync for Context {}
```

**UNKNOWN**

- context 能否跨线程移动。
- 多线程并发调用是否安全。
- 是否有全局状态。
- 是否需要每线程初始化。

**9. callback 设计**

如果 obmm 有 callback，初版不急着暴露复杂泛型 API。

推荐阶段：

1. `obmm-sys` 暴露原始 callback ABI。
2. `obmm` 先提供少量同步、安全 callback 包装。
3. 后续再设计 async/channel/stream API。

callback 重点：

- Rust closure 必须被稳定存放，不能把栈上引用传给 C 长期保存。
- panic 不能跨 FFI 边界，必须 `catch_unwind`。
- callback 中返回错误时，需要映射成 obmm 可理解的状态码。

**UNKNOWN**

- obmm 是否使用 callback。
- callback 是否同步调用、异步保存、跨线程调用。
- 是否允许 callback 返回错误。

**10. async 设计**

初版不直接做 async crate，除非 obmm 本身是异步或事件驱动。

建议：

- 第一版只做 blocking safe wrapper。
- 如果 obmm 有 file descriptor/event loop/callback completion，再加 feature：
  - `tokio`
  - `async-std`
  - `futures`

**UNKNOWN**

- obmm 是否有阻塞操作。
- 是否支持取消。
- 是否有进度回调或异步 completion API。

**11. 测试策略**

在不知道 API 的情况下，先定义测试层级：

`obmm-sys`：

- link test：确认能链接。
- smoke test：调用 version/init 之类无副作用函数，若存在。
- layout test：只在有明确 ABI layout 时做。

`obmm`：

- create/drop context。
- error conversion。
- string roundtrip。
- resource leak smoke test。
- callback panic boundary，若有 callback。

CI matrix 初版：

```text
Linux stable Rust
macOS stable Rust
Windows stable Rust, if obmm supports Windows
```

**UNKNOWN**

- 是否能在 CI 安装 obmm。
- 是否需要 test fixture 数据。
- 是否有 deterministic smoke API。

**12. 发布前阻塞项**

在真正提交 crate 设计前，必须确认：

- obmm 的真实语言边界：C ABI 还是 C++ ABI。
- 头文件位置和许可证。
- 动态/静态链接方式。
- 所有 opaque handle 的 create/free 函数。
- 错误码和错误消息规则。
- 字符串编码。
- 线程安全说明。
- callback 生命周期。
- 是否需要全局 init/shutdown。
- 最小可运行 smoke test。

**结论**

最初级方案是：先做 `obmm-sys` + `obmm` 双 crate，`obmm-sys` 保持薄绑定，`obmm` 用 owning handle、`Result`、`Drop`、字符串拷贝和保守 `!Send + !Sync` 建一个最小安全壳。当前所有 API 名称、签名、链接参数都必须视为 `UNKNOWN/placeholder`，不能当作真实 obmm 绑定发布。