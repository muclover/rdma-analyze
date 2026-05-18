# obmm Rust FFI 初始设计方案

状态：初始架构尝试。按任务约束，本方案没有探索 obmm 代码库，没有读取头文件、README、构建脚本或文档，没有编译 C 项目，也没有安装依赖。所有 C API、ABI、构建、许可证、线程安全和资源语义相关判断均为 `Unknown`，并在相应章节标出。

一句话决策摘要：在缺少源码事实时，建议先按可发布 Rust 生态的保守默认设计为 `obmm-sys` + `obmm` 双 crate；`obmm-sys` 负责原始 FFI、链接和 ABI 守卫，`obmm` 负责 RAII、错误映射、生命周期、`Send`/`Sync` 保守策略和 panic 边界。绑定路径以“预生成 `bindgen` + allowlist/blocklist + ABI 检查”为初始假设，但必须在允许读取头文件后确认；若发现大量宏、`static inline`、bitfield、union 或平台条件类型，则改为 `wrapper.c` 或手写替代。

## 阅读顺序

1. `ARCHITECTURE`：总体 crate 划分、库画像、发布目标和风险。
2. `BINDING`：绑定路径、链接策略、再生流程和排除路径。
3. `SAFE API`：safe 层类型、RAII、错误、`unsafe` 边界、线程策略。
4. `ASYNC ANALYZE`：async 和上层运行时集成判断。
5. `CHECKLIST`：允许扫描后到发布前的执行清单。

## ARCHITECTURE

### 设计输入

| 项 | 当前结论 |
|----|----------|
| C 项目 | obmm |
| C 项目路径 | `Unknown`，本次禁止探索代码库 |
| 主头文件 | `Unknown` |
| 上游版本范围 | `Unknown` |
| 发布意图 | `Unknown`；本方案按 crates.io 可发布设计 |
| 输出位置 | 用户指定的 outputs 目录中的 `final.md` |
| 已有 Rust 草稿 | `Unknown`，本次禁止扫描 |

### 库画像

| 画像项 | 当前结论 | 影响 |
|--------|----------|------|
| API 形态 | `Unknown`：opaque 指针、值结构体、ops 表、回调均未确认 | safe 类型和 `repr(C)` 暴露策略必须延后确认 |
| 资源模型 | `Unknown`：create/destroy、引用计数、borrow、全局 init/cleanup 未确认 | `Drop`、`Clone`、生命周期和父子资源顺序不得臆造 |
| 错误模型 | `Unknown`：返回码、`errno`、错误栈、out param、回调错误未确认 | safe 层先预留统一 `Error`，具体枚举需后续映射 |
| 并发模型 | `Unknown`：线程安全、回调线程、阻塞调用、fd/event 机制未确认 | 默认不实现 `Send`/`Sync`，除非后续有明确依据 |
| 构建模型 | `Unknown`：系统库、pkg-config、vendored、多后端、生成头未确认 | `build.rs` 只能给出探测顺序草案 |
| 许可证 | `Unknown` | vendored、静态链接和再分发策略不能定稿 |

### Q1-Q10 决策问卷

| # | 问题 | 当前答案 | 设计影响 |
|---|------|----------|----------|
| Q1 | opaque / ops / 值结构体？ | `Unknown` | safe 层先按 opaque owner 设计，不暴露值结构体布局 |
| Q2 | inline / 宏 / union / bitfield？ | `Unknown` | 初始用预生成 `bindgen`，但保留 `wrapper.c` 切换点 |
| Q3 | create/destroy / refcount / borrow？ | `Unknown` | 不定义具体 `Drop` 调用名，只定义 RAII 映射规则 |
| Q4 | errno / 返回码 / 错误栈 / out param？ | `Unknown` | `Error` 先设计为可承载 code、message、operation 的非穷尽类型 |
| Q5 | 系统库 / vendored / 多后端？ | `Unknown` | `build.rs` 草案按 env override -> pkg-config -> system path -> optional vendored |
| Q6 | CI 无 dev 包 / 无硬件 / 平台限制？ | `Unknown` | CI 设计需有无原生库的仅编译/跳过路径 |
| Q7 | 是否独立 `-sys`？ | `Unknown`；按可发布默认为 Yes | 采用 `obmm-sys` + `obmm` |
| Q8 | 许可证与下游传染？ | `Unknown` | 不默认启用 vendored，不承诺 static link |
| Q9 | 是否需要协议/运行时层？ | `Unknown` | 初始不建第三层，后续按 API 复杂度升级 |
| Q10 | 是否需要 async？ | `Unknown` | async 不进入 `-sys`；需要时放 `obmm-async` 或 feature-gated adapter |

### Crate / workspace 划分

建议初始 workspace：

```text
obmm-rs/
  Cargo.toml
  obmm-sys/
  obmm/
```

`obmm-sys` 职责：

- 暴露原始 `extern "C"` 函数、常量、枚举和值类型。
- 包含生成后的 bindings 或手写 extern。
- 提供 `build.rs`，负责链接 obmm 原生库。
- 使用 `links = "obmm"`，前提是上游库名确认确为 `obmm`；否则改为实际 native library name。
- 不承诺 Rust safe 语义，不实现资源所有权封装。

`obmm` 职责：

- 提供 Rust 用户直接使用的 safe API。
- 封装资源生命周期、错误映射、回调、panic 边界和线程安全策略。
- 将原始指针隐藏在 owner/borrowed 类型中。
- 暴露少量必要 `unsafe` escape hatch，并为每个入口写 `# Safety`。

暂不建议初始三层 crate。只有在确认 obmm 包含独立协议、复杂 IO runtime、可组合高层工作流或 async reactor 集成后，再增加 `obmm-async`、`obmm-protocol` 或 `obmm-client`。

### 发布目标与 semver

- `obmm-sys` 版本应反映绑定的上游 ABI 范围，例如用 crate metadata 和 README 标出支持的 obmm C library version。
- `obmm` 可以有独立 semver；safe API 的破坏性变化不应被原始 C ABI 细节直接牵动。
- `obmm-sys` 的公开 raw binding 面较大时，升级上游头文件可能造成 Rust semver 破坏；需要绑定再生 diff 和 ABI 检查。
- 若后续确认已有社区 `obmm-sys`，应优先评估复用，而不是制造重复 `links` 冲突。

### 风险与未决项

| 风险 | 当前处理 |
|------|----------|
| API 形态未知导致架构可能偏厚或偏薄 | 采用双 crate 保守默认，扫描后再调整 |
| 宏/inline/bitfield 未知 | `bindgen` 只是初始假设，保留 `wrapper.c` 方案 |
| 资源释放函数未知 | 不写具体 destructor 名，后续逐类型确认 |
| 线程安全未知 | 默认 `!Send`/`!Sync`，用 `PhantomData<Rc<()>>` 或等价策略阻止自动派生 |
| 许可证未知 | 不默认 vendored 或 static link |
| 原生库名未知 | `links = "obmm"` 只是占位，必须确认后落地 |

## BINDING

### 绑定路径选择

初始选择：预生成 `bindgen` + allowlist/blocklist + ABI/layout 检查。

理由：

- 在 API 面未知时，预生成 `bindgen` 是对中大型 C API 较稳妥的默认路径。
- 预生成文件使 docs.rs 和普通用户构建不依赖 libclang。
- 绑定再生可以由维护者在升级上游时执行，避免用户安装完整 C 开发工具链。

此选择不是最终事实判断。允许读取头文件后，如果 obmm API 很小且稳定，可切换为手写 `extern`；如果头文件依赖大量 C 编译器语义，可切换为 `wrapper.c` 或混合方案。

### 明确排除的路径

暂不选择构建时 `bindgen` 作为默认路径。

原因：

- 用户构建会依赖 libclang 和目标平台头文件，发布体验较脆。
- 本次无法确认 obmm 是否需要跟随系统安装头文件频繁变化。
- docs.rs 和 CI 更容易因缺少原生 dev 包失败。

暂不选择纯手写 `extern` 作为默认路径。

原因：

- 当前完全未知 API 大小、符号数量和 ABI 稳定性。
- 手写适合小而稳定的 C API；对未知库先采用可再生绑定更保守。

### 可能的混合策略

| 条件 | 调整 |
|------|------|
| 发现大量 `static inline` | 用 `wrapper.c` 导出真实函数，Rust 只绑定 wrapper 符号 |
| 发现复杂宏表达式 | 在 `wrapper.c` 或手写常量测试中固化宏值 |
| 发现 bitfield | blocklist 相关结构体，提供 C helper getter/setter |
| 发现 union | 优先保持 raw 层透明，safe 层提供明确 variant wrapper |
| 发现平台条件类型 | 按 target cfg 分组生成或提供平台独立 wrapper |

### `obmm-sys` 文件布局草案

```text
obmm-sys/
  Cargo.toml
  build.rs
  src/lib.rs
  src/bindings.rs
  wrapper/
    obmm_wrapper.h
    obmm_wrapper.c   # 仅在确认需要 wrapper 时加入
```

### allowlist / blocklist 草案

在未读取头文件前，只能给出规则：

- allowlist 函数：以确认后的 obmm 公共前缀为准，例如 `obmm_.*`。
- allowlist 类型：以确认后的公共类型前缀为准，例如 `obmm_.*`、`OBMM_.*`。
- blocklist：编译器内建类型、平台私有实现细节、不可安全暴露的 bitfield/union、仅供宏展开的内部类型。
- opaque：对不应被 Rust 直接构造的 C handle 使用 opaque 类型。

### `build.rs` 链接策略草案

探测顺序建议：

1. 显式环境变量覆盖：`OBMM_LIB_DIR`、`OBMM_INCLUDE_DIR`、`OBMM_STATIC`。
2. `pkg-config`：包名暂定 `obmm`，必须后续确认。
3. 系统路径：仅在目标平台和库名确认后启用。
4. `vendored` feature：默认关闭，只有许可证和构建复杂度确认后再支持。

链接输出草案：

```text
cargo:rustc-link-lib=obmm
cargo:rustc-link-search=native=<confirmed-lib-dir>
```

这里的 `obmm` 是占位 native library name。若上游实际库名不同，`links` 和 `rustc-link-lib` 必须同步改名。

### docs.rs 与 CI 降级

- docs.rs：设置 `DOCS_RS` 路径，使文档构建不要求真实原生库；必要时用预生成 bindings 和 `cfg(docsrs)` 避免链接。
- CI 无原生库：至少运行 `cargo check --all-features` 中不触发链接的部分，或使用 feature gate 区分 `sys-link` 与 pure Rust tests。
- 真实集成：在有 obmm dev package 的 runner 上执行 smoke test。

### 绑定再生流程

后续允许扫描并确认头文件后，应固定再生命令：

```text
bindgen <confirmed-wrapper-or-header> \
  --allowlist-function '<confirmed-prefix>.*' \
  --allowlist-type '<confirmed-prefix>.*' \
  --allowlist-var '<confirmed-prefix>.*' \
  --blocklist-type '<confirmed-private-types>' \
  --output obmm-sys/src/bindings.rs
```

发布前必须检查：

- 生成文件 diff。
- layout/constant/signature 检查。
- 最小链接 smoke test。
- 上游版本号与 crate metadata 一致。

## SAFE API

### 模块树草案

```text
obmm/
  src/lib.rs
  src/error.rs
  src/handle.rs
  src/context.rs       # 若存在全局/上下文资源
  src/callback.rs      # 若存在回调 API
  src/types.rs
```

模块名均为占位。实际模块应由 obmm 的领域概念决定，而不是机械映射 C 函数前缀。

### 核心类型草案

| Rust 类型 | C 对应 | 当前状态 | 所有权策略 |
|-----------|--------|----------|------------|
| `Context` | `Unknown` | 待确认是否存在全局/context handle | 若有 create/destroy，则 owner + `Drop` |
| `Handle` | `Unknown` | 待确认主要 opaque handle | 非空指针封装，不允许用户构造 |
| `BorrowedHandle<'a>` | `Unknown` | 待确认 borrow API | 用生命周期绑定父资源 |
| `Error` | `Unknown` | 待确认错误模型 | 非穷尽错误类型，承载 code/message/source |

不要在 safe 层公开裸 `*mut T`，除非方法名明确为 `as_raw`、`from_raw` 等 escape hatch，并带 `unsafe` 契约。

### RAII 规则

待确认 create/destroy 对后，对每个资源执行以下映射：

| C 资源语义 | Rust 设计 |
|------------|-----------|
| `create -> destroy` | `struct X(NonNull<sys::obmm_x>); impl Drop for X` |
| `init(out*) -> cleanup` | `MaybeUninit` 只留在内部，safe API 返回初始化完成的 owner |
| 引用计数 retain/release | `Clone` 调用 retain，`Drop` 调用 release |
| 借用子对象 | 子对象携带生命周期，不能超过父对象 |
| 全局 init/cleanup | 用显式 `Library`/`Context` guard 或 `OnceLock`，避免隐式全局析构竞态 |

具体析构函数名全部为 `Unknown`，不得在实现前臆造。

### 错误模型

初始 `Error` 设计：

```rust
#[non_exhaustive]
pub enum Error {
    Code { code: i32, operation: &'static str },
    Null { operation: &'static str },
    Message { message: String, operation: &'static str },
    CallbackPanic,
    Unknown { operation: &'static str },
}
```

这只是结构草案。允许读取 C API 后，应根据真实错误模型调整：

- 若 C API 返回负数错误码：映射非零/负数为 `Error::Code`。
- 若使用 `errno`：在 FFI 调用后立即捕获 errno。
- 若有错误栈：safe 层拉取并保存错误栈内容。
- 若使用 out param：先检查返回码，再初始化 Rust 值。
- 若回调返回错误：定义回调错误传播和取消语义。

### `unsafe` 边界

safe crate 应尽量封装所有常规操作。允许保留的公开 `unsafe` 入口：

| 入口 | 目的 | `# Safety` 契约草案 |
|------|------|---------------------|
| `from_raw` | 接管 C 侧已创建资源 | 指针必须非空、有效、来自兼容 obmm 版本，且调用后 Rust 成为唯一 owner 或遵守引用计数 |
| `as_raw` / `as_raw_mut` | 与外部 C 集成 | 调用方不得在 Rust owner 存活期间释放或破坏资源不变量 |
| `borrow_raw` | 构造借用 wrapper | 指针必须在生命周期 `'a` 内有效，且父资源不得提前释放 |
| callback trampoline | C 调用 Rust closure | 不允许 panic 跨越 FFI；`user_data` 必须指向有效 closure state |

### 回调与 panic 策略

若 obmm 有回调 API：

- Rust closure 存入稳定地址，例如 `Box<CallbackState>`。
- C `user_data` 只保存不透明指针。
- trampoline 内使用 `catch_unwind` 捕获 panic。
- panic 后按 C API 支持能力返回错误码、设置取消标志，或记录为 `Error::CallbackPanic`。
- 不允许 unwinding 跨过 `extern "C"` 边界。

若 obmm 没有回调 API，本节为 N/A。

### `Send` / `Sync`

默认策略：

- 所有 owner 类型默认不实现 `Send`/`Sync`。
- 只有在上游文档或头文件注释明确线程安全，且资源释放、回调、全局状态都满足跨线程要求时，才手写 `unsafe impl Send` 或 `unsafe impl Sync`。
- 若只允许同一线程使用，则用 marker 阻止自动线程迁移。
- 若只有部分方法线程安全，则不要给整个类型实现 `Sync`；改用显式锁或分离类型。

### 暂不封装范围

在允许扫描前，以下内容不进入 safe API：

- 未确认生命周期或所有权的 borrowed pointer。
- 未确认布局的值结构体构造。
- 未确认线程语义的全局状态 API。
- 未确认 panic/错误传播规则的回调 API。
- 仅供内部宏或平台私有实现使用的符号。

## ASYNC ANALYZE

当前结论：`Unknown`，不在初始 `obmm-sys` 中加入 async，不把 Tokio、async-std 或任何 runtime 依赖放入 raw FFI crate。

### 判断依据

本次禁止读取头文件和文档，因此无法确认 obmm 是否具备：

- file descriptor / handle 可等待机制。
- poll/select/epoll/kqueue 集成点。
- multi/event API。
- 阻塞 IO 调用。
- 后台线程或回调驱动模型。

### 初始策略

- `obmm-sys`：只暴露 raw FFI，不包含 async。
- `obmm`：先提供同步 safe API，并在类型设计中避免阻止未来 async adapter。
- `obmm-async`：仅当确认 obmm 有可等待句柄、回调完成通知或长时间阻塞 IO 后再创建。

### 如果后续确认需要 async

| C 机制 | Rust 集成 |
|--------|-----------|
| fd/poll API | safe 层暴露 raw fd 或 readiness primitive；runtime adapter 放 `obmm-async` |
| 回调完成通知 | 用 channel/waker bridge，但 panic 和 lifetime 仍在 safe 层处理 |
| 阻塞调用 | 提供同步 API；async adapter 使用 `spawn_blocking`，不伪装成非阻塞 |
| C 自带 event loop | safe 层封装 loop handle；runtime 集成独立 feature 或 crate |

### 如果后续确认不需要 async

`ASYNC` 结论应改为 N/A，并说明 obmm 是纯计算、短同步调用或由调用方自行调度线程池。

## CHECKLIST

### 允许扫描后的调研

- [ ] 确认 obmm C 项目路径、主头文件和上游版本。
- [ ] 确认公共符号前缀、native library name 和 pkg-config package name。
- [ ] 识别 opaque handle、值结构体、ops 表、回调、union、bitfield、宏和 `static inline`。
- [ ] 列出所有 create/destroy、retain/release、borrow、global init/cleanup 对。
- [ ] 确认错误模型：返回码、`errno`、错误栈、out param、回调错误。
- [ ] 确认线程安全、阻塞调用、回调线程和全局状态语义。
- [ ] 确认构建系统、系统依赖、vendored 可行性、多后端和平台差异。
- [ ] 确认许可证、静态链接和源码再分发影响。

### 架构落地

- [ ] 创建 `obmm-sys` + `obmm` workspace，或根据扫描结果调整为纯 `-sys` / 三层 workspace。
- [ ] 在 `obmm-sys` 中设置准确的 `links` 值。
- [ ] 在 README 中记录支持的 obmm C library version。
- [ ] 明确 `obmm-sys` 与 `obmm` 的 semver 关系。
- [ ] 若发现已有 `obmm-sys`，评估复用和 `links` 冲突。

### 绑定与构建

- [ ] 选择最终绑定路径：手写、预生成 `bindgen`、构建时 `bindgen`、`wrapper.c` 或混合。
- [ ] 固定 bindgen 再生命令。
- [ ] 配置 allowlist/blocklist。
- [ ] 为宏、inline、bitfield、union 决定 wrapper 或手写替代。
- [ ] 实现 `build.rs` 探测顺序：env override、pkg-config、system path、optional vendored。
- [ ] 设计 docs.rs 无原生库构建路径。
- [ ] 设计 CI 缺少 dev package 时的降级路径。

### Safe API

- [ ] 为每个 owner 资源建立 RAII 类型。
- [ ] 为每个 borrowed pointer 建立生命周期约束或暂不封装。
- [ ] 建立真实错误模型到 `Result<T, Error>` 的映射。
- [ ] 为回调 API 建立 `user_data`、panic、生命周期和线程策略。
- [ ] 对每个公开 `unsafe fn` 写 `# Safety`。
- [ ] 根据证据决定是否实现 `Send`/`Sync`；无证据则保持不实现。
- [ ] 标出暂不封装的 C API 区域和原因。

### 测试与 CI

- [ ] 使用 `ctest2`、`systest` 或等价方式检查 layout、constant、signature。
- [ ] 加入绑定再生 diff 检查。
- [ ] 加入最小 init/destroy 或核心调用 smoke test。
- [ ] 为无原生库 CI 提供仅编译路径或外部环境标记。
- [ ] 为 safe 层不变量加入 doctest、unit test 或 compile-fail test。
- [ ] 对回调 panic 不跨 FFI 增加回归测试。

### 发布

- [ ] 确认 crate 名称可用性和 `links` 唯一性。
- [ ] 确认许可证表达和 vendored feature 默认值。
- [ ] 确认 docs.rs metadata。
- [ ] 确认 README 包含安装、feature、支持平台、原生依赖和安全边界。
- [ ] 确认上游版本升级流程：更新头文件、再生 bindings、运行 ABI 检查、更新 changelog。

## 最终自检

- 本文未读取 obmm 代码、头文件、README、构建脚本或文档。
- 本文未编译 C 项目、未安装依赖、未实现代码。
- 所有缺失事实均标为 `Unknown` 或“待确认”。
- 架构选择是保守默认，不冒充基于 obmm 真实 API 的结论。
- 输出只写入用户指定 outputs 目录中的 `final.md`。
