# obmm Rust FFI 草案设计级修订方案

## 结论摘要

现有草案不应按“单 crate + 用户构建时全量 bindgen + `u64`/`i32` 直出”的方向推进。`libobmm` 是一个面向 OBMM 内核模块、字符设备、NUMA 热插和共享内存一致性控制的 C 用户态库；它的 API 面不大，但句柄生命周期、可变长描述符、系统库链接、硬件/内核环境和 ABI 漂移风险都很明确。推荐改为：

1. 建立 `obmm-sys` + `obmm` 双 crate workspace。
2. `obmm-sys` 负责原始 FFI、`build.rs`、`links = "obmm"`、系统库探测、预生成或受控再生的 bindgen 绑定、ABI/layout 检查。
3. `obmm` safe crate 负责 `MemId` newtype、RAII guard、描述符 builder、错误类型、ownership/NUMA 语义和保守线程安全策略。
4. 不在默认路径引入 async/runtime crate；OBMM 的公开 C API 是同步控制面，数据访问走 mmap/load/store 或系统调用，runtime 适配应留在后续上层 crate 或示例。

这份修订方案面向 crates.io 发布和 Linux/OBMM 专用环境使用；CI 与 docs.rs 必须有无原生库、无 OBMM 内核模块时的降级路径。

## C API 画像

`libobmm.h` 暴露的主类型和函数如下：

| 类别 | C API | Rust 设计影响 |
|---|---|---|
| 内存 ID | `typedef uint64_t mem_id`，`OBMM_INVALID_MEMID = 0` | safe 层必须使用 `MemId(NonZeroU64)`，避免把无效 ID 当成功值传播 |
| 导出 | `obmm_export`、`obmm_export_useraddr`、`obmm_unexport` | 需要 `ExportedMemory` RAII 类型，在 `Drop` 中调用 `obmm_unexport` 或提供显式关闭 |
| 引入 | `obmm_import`、`obmm_unimport` | 需要 `ImportedMemory` RAII 类型，保存 import 后的 `MemId` 和 NUMA out param |
| 预上线 | `obmm_preimport`、`obmm_unpreimport` | 需要 `Preimport` guard，处理 `numa_id` 入参/出参 |
| 描述符 | `struct obmm_mem_desc`、`struct obmm_preimport_info` 均含 `uint8_t priv[]` | 不能用普通 Rust struct + `Vec<u8>` 表示 C ABI；需要拥有型 buffer + raw view |
| 一致性 | `obmm_set_ownership(int fd, void *start, void *end, int prot)` | safe 层应暴露 range/prot 封装，要求调用者证明 fd 和映射区间有效 |
| 维测查询 | `obmm_query_memid_by_pa`、`obmm_query_pa_by_memid` | 可放入 `debug` 或 `diagnostics` 模块，返回 typed result |

文档显示 OBMM 内存 ID 中 `0` 是错误/预留 ID；export/import 使用同一 ID 空间。内存描述符中的 `priv` 是紧随结构体尾部的连续内存，`priv_len` 当前上限由上游头文件常量定义，文档建议应用自用部分不超过 128 字节以保留兼容性。OBMM 操作受 PMD_SIZE、memory block、page size、cache home agent 等粒度约束，且共享 cacheable 内存需要调用者维护跨 host 读写权限状态。

构建侧，`obmm/src/libobmm/CMakeLists.txt` 生成共享库 `libobmm.so`，安装头文件 `libobmm.h` 到 include，库安装到 `lib64`，SOVERSION 来自 `obmm/VERSION` 的 major version。未看到 pkg-config 文件；Rust 侧不能假设 `pkg-config --libs obmm` 一定可用。

许可证为 Mulan PSL v2。Rust crate README 与 `Cargo.toml` license 字段需要明确该许可证，vendored/静态链接策略若后续加入，必须重新评估下游分发影响。

## 对原草案的主要问题

| 草案点 | 风险 | 修订 |
|---|---|---|
| 只做一个 `obmm` crate，不拆 `obmm-sys` | safe API semver 会直接暴露 C ABI 面；其他上层 crate 无法复用 raw binding；链接策略难以集中治理 | 拆为 `obmm-sys` 和 `obmm`，workspace 发布 |
| `build.rs` 直接链接 `-lobmm` | 无探测、无 include/lib 路径覆盖、无 docs.rs 降级；lib64 安装路径和交叉编译不可控 | `obmm-sys` 使用 `links = "obmm"`，支持 env override、可选 pkg-config、system path fallback 和 docs.rs stub |
| 用户构建时 bindgen 生成全部绑定 | 要求用户安装 libclang；docs.rs/CI 容易失败；全量头文件可能把 `<ub/obmm.h>` 和平台细节泄漏为 semver 面 | 默认预生成受控绑定；再生 feature/xtask 仅给维护者使用；allowlist 只覆盖 `obmm_*`、`mem_id`、必要常量和结构体 |
| safe 层把 `mem_id` 暴露为 `u64` | `0` 无效 ID 会混入成功路径；调用者可伪造任意 ID | safe 层使用 `MemId(NonZeroU64)`，raw 转换集中校验；需要 raw escape 时用显式 `unsafe` |
| export/import 直接返回 `u64` | 无法表达错误、资源归属和释放责任 | 返回 `Result<ExportedMemory>`、`Result<ImportedMemory>` 或显式 `MemId` guard |
| `obmm_mem_desc` 用普通 Rust struct 且包含 `Vec<u8> priv` | 与 C flexible array member ABI 不兼容；传给 C 会产生错误布局 | 使用 `ObmmMemDescBuf` 拥有连续 buffer，内部按 `repr(C)` header + tail bytes 组织 |
| 所有 handle 都 `unsafe impl Send + Sync` | C 文档未声明线程安全；资源关联内核对象、fd、mmap 和跨 host ownership 状态 | 默认不实现 `Send`/`Sync`，或仅在类型内部不含借用且上游明确线程安全后逐项实现 |
| 错误只用 `i32` | 无法区分无效 ID、负 errno、正返回码、out param 失败、前置条件错误 | `Error` enum 封装 `errno`/return code/invalid id/null output/invalid input，并保留 raw code |
| 暂不做 ABI 检查和 docs.rs 降级 | 发布后极易因系统头/库版本变化、无 libobmm 环境导致构建失败 | 必须加入 layout/constant/signature 检查、绑定再生检查和 docs.rs no-link/no-runtime 降级 |

## 推荐 workspace 与 crate 边界

推荐结构：

```text
obmm-rs/
  Cargo.toml
  obmm-sys/
    Cargo.toml
    build.rs
    src/lib.rs
    src/bindings.rs
    wrapper.h
    tests/abi.rs
  obmm/
    Cargo.toml
    src/lib.rs
    src/desc.rs
    src/error.rs
    src/mem.rs
    src/ownership.rs
    tests/
```

`obmm-sys` 职责：

- 暴露原始 C ABI：`extern "C"` 函数、`mem_id` raw alias、`obmm_mem_desc` header、`obmm_preimport_info` header、必要常量。
- `Cargo.toml` 设置 `links = "obmm"`，防止同一依赖图中多份不同链接策略互相覆盖。
- `build.rs` 输出 `cargo:rustc-link-lib=obmm`，并按明确顺序探测路径。
- 保持 raw API 不做所有权、错误和线程语义承诺。

`obmm` safe crate 职责：

- 提供 `MemId`、`ExportedMemory`、`ImportedMemory`、`Preimport`、`MemDesc`/`MemDescBuf`、`PreimportInfoBuf` 等 Rust 类型。
- 将 C 的返回值和无效 ID 转成 `Result<T, Error>`。
- 用 RAII 管理 `unexport`、`unimport`、`unpreimport`，并允许 `close`/`into_raw` 处理需要显式交接的场景。
- 文档化 OBMM 粒度、NUMA、ownership 和 mmap/fd 安全前提。

不建议第三层 crate 作为初始方案。当前 C API 是控制面绑定，不包含协议编解码、网络会话或 runtime 事件循环；等 safe crate 稳定后，如需 Tokio/NUMA 调度或 mmap ergonomic API，可另建 `obmm-async` 或 `obmm-util`。

## 绑定生成与 ABI 策略

### 选择：预生成 bindgen + allowlist + layout 检查

API 面很小，理论上可手写 `extern`。但 `struct obmm_mem_desc` 和 `struct obmm_preimport_info` 依赖 C flexible array member、常量来自上游头文件，后续也可能扩展字段。因此推荐：

- 使用 bindgen 生成 `obmm-sys/src/bindings.rs`，提交到仓库。
- 维护 `wrapper.h`，仅 include `libobmm.h`。
- 使用 allowlist 限制符号：
  - functions: `obmm_.*`
  - types: `mem_id`、`obmm_mem_desc`、`obmm_preimport_info`
  - vars/constants: `OBMM_.*`、`MAX_NUMA_NODES`
- 对 flexible array member 的 Rust 表示保持 raw 只读，safe 层不直接公开该 struct 作为可构造类型。
- 记录再生命令，例如 `bindgen wrapper.h --allowlist-function 'obmm_.*' ...`，并在 CI 中检查生成文件是否有 diff。

排除构建时全量 bindgen作为默认路径，原因是它把 libclang 和系统头版本转嫁给最终用户，也会把当前不需要的上游头文件细节纳入 Rust semver 表面。排除纯手写 extern 作为首选路径，原因是可维护性弱于受控 bindgen；但如果上游明确承诺 API 极长期稳定，也可以用手写 extern 替换预生成 bindgen，并保留同等 ABI 检查。

### ABI/layout 检查

`obmm-sys` 应包含以下检查：

- `mem_id` 宽度等于 `uint64_t`。
- `OBMM_INVALID_MEMID` 值等于 0。
- `struct obmm_mem_desc` header 中 `addr`、`length`、`seid`、`deid`、`tokenid`、`scna`、`dcna`、`priv_len` 的 offset 与 C 一致。
- `struct obmm_preimport_info` header 中 `pa`、`length`、`base_dist`、`numa_id`、`seid`、`deid`、`scna`、`dcna`、`priv_len` 的 offset 与 C 一致。
- 函数签名检查覆盖 export/import/unexport/unimport/preimport/unpreimport/set_ownership/query。

真实系统测试需要 OBMM 内核模块和硬件/平台环境，不应作为普通 CI 的默认必跑项。CI 默认跑 binding 生成检查、layout 编译检查、safe 层单元测试；集成 smoke test 用 feature 或环境变量显式开启。

## build.rs 与链接方案

`obmm-sys/build.rs` 的探测顺序建议为：

1. 如果设置 `OBMM_LIB_DIR`，输出 `cargo:rustc-link-search=native=$OBMM_LIB_DIR`。
2. 如果设置 `OBMM_INCLUDE_DIR`，供 bindgen 再生或 ABI 检查使用。
3. 可选尝试 `pkg-config` 查找 `obmm`，但不能作为唯一机制，因为 CMake 片段未显示安装 `.pc` 文件。
4. fallback 到系统默认搜索路径，输出 `cargo:rustc-link-lib=dylib=obmm`。
5. `DOCS_RS` 或 `OBMM_SYS_NO_LINK=1` 时不强制链接真实库，只生成文档可用的类型和函数声明，并在 crate docs 中说明这些构建不能运行调用。

建议 feature：

| feature | 默认 | 用途 |
|---|---:|---|
| `system` | yes | 链接系统 `libobmm.so` |
| `bindgen` | no | 维护者再生绑定，不要求普通用户安装 libclang |
| `abi-check` | no 或 CI only | 开启 layout/signature 检查 |
| `vendored` | no | 暂不建议实现；若未来加入，需完整处理 Mulan PSL v2、内核头和静态链接影响 |

因为上游安装路径是 `lib64`，README 需要写明在非标准路径上使用 `LD_LIBRARY_PATH`、`OBMM_LIB_DIR` 或系统 linker 配置。交叉编译时，`OBMM_LIB_DIR` 和 `OBMM_INCLUDE_DIR` 必须指向 target sysroot 中的库和头。

## Safe API 修订方案

### 核心类型

```rust
pub struct MemId(NonZeroU64);

pub struct ExportedMemory {
    id: MemId,
    desc: MemDescBuf,
}

pub struct ImportedMemory {
    id: MemId,
    numa: i32,
}

pub struct Preimport {
    info: PreimportInfoBuf,
    active: bool,
}

pub struct MemDescBuf {
    // owned contiguous allocation: C header + priv tail
}

pub struct PreimportInfoBuf {
    // owned contiguous allocation: C header + priv tail
}
```

`MemId` 不应提供 `From<u64>` 的安全实现。建议提供：

- `MemId::new(raw: u64) -> Option<MemId>`，拒绝 0。
- `MemId::as_raw(self) -> u64` 或 `raw(&self) -> u64`。
- `unsafe fn MemId::from_raw_unchecked(raw: u64)`，只给已经确认来自 OBMM 的调用使用。

`ExportedMemory`、`ImportedMemory`、`Preimport` 的 `Drop` 应调用对应 C 释放函数。由于 `Drop` 无法返回错误，类型还应提供 `close(self) -> Result<()>`，并在 `Drop` 中记录或忽略清理错误；文档必须说明需要处理失败时应显式调用 `close`。

### flexible array member 处理

`obmm_mem_desc` 和 `obmm_preimport_info` 不能建模为：

```rust
struct ObmmMemDesc {
    ...
    priv: Vec<u8>,
}
```

这与 C ABI 完全不同。正确方案是 safe 层拥有一块连续分配的 `Vec<MaybeUninit<u8>>` 或 `Box<[MaybeUninit<u8>]>`，长度为 `size_of::<Header>() + priv_len`，并提供：

- 构造时校验 `priv_len <= OBMM_MAX_PRIV_LEN`，若该常量不可公开则至少按文档保守限制并在运行时允许用户显式覆盖。
- 初始化 header 字段和 tail bytes。
- `as_mut_ptr() -> *mut sys::obmm_mem_desc` 仅在 FFI 调用边界内部使用。
- FFI 返回后，将 header 字段复制到安全的 Rust view，tail bytes 通过 slice 暴露。

对于 `obmm_export`，描述符部分字段既有入参也有出参：`priv_len`/`priv`、`deid` 可能需要由调用者设置，`addr`、`length`、`tokenid` 等由 C 填充。builder 应区分 provider-side export 配置和 consumer-side import 描述，避免一个可变 struct 混淆方向。

### 错误模型

建议错误类型：

```rust
pub enum Error {
    InvalidMemId,
    ReturnCode { function: &'static str, code: i32, errno: Option<i32> },
    InvalidInput { field: &'static str, reason: &'static str },
    NullOutput { function: &'static str, parameter: &'static str },
    Unsupported { feature: &'static str },
}
```

映射规则：

- 返回 `mem_id` 的 C 函数：`0` 转为 `Error::InvalidMemId`；非 0 转为 `MemId`。
- 返回 `int` 的 C 函数：`0` 表示成功；非 0 转为 `ReturnCode`，必要时采集 `errno`。
- out param：safe 层自行分配并初始化，不让用户传 null。
- 输入约束：长度、priv_len、NUMA、地址对齐、ownership range 等前置条件尽量在 Rust 层提前校验；无法静态确认时写入 `# Safety`。

### unsafe 边界

safe crate 对普通 export/import/unexport/unimport/preimport 流程应提供安全函数，但这些函数的文档必须说明运行环境要求：已安装并加载 OBMM 内核模块、libobmm 版本匹配、调用者遵守 OBMM 粒度和控制面时序。

以下入口应保留 `unsafe` 或要求 unsafe 参数：

| API | unsafe 原因 | 设计 |
|---|---|---|
| `export_useraddr(pid, va, length, flags, desc)` | 传入进程 VA 范围必须有效、对齐并满足 OBMM 硬件限制；错误可能影响目标进程内存 | `unsafe fn export_useraddr(...) -> Result<ExportedMemory>`，`# Safety` 详细写明 VA、pid、lifetime、pinning 和权限要求 |
| `set_ownership(fd, start, end, prot)` | fd 必须是 OBMM memory device，range 必须是有效映射且与 OBMM 粒度/共享模型一致 | `unsafe fn set_ownership_raw(...)`；另可提供基于 safe mmap guard 的安全封装 |
| `from_raw_mem_id` / `into_raw` | 可能破坏 RAII，导致 double unexport/unimport 或泄漏 | 显式 unsafe 或消费 self 的安全 `into_raw`，并文档化释放责任 |
| debug query by physical address | 物理地址权限和平台语义不适合普通 safe API | 放入 `diagnostics`，根据环境决定是否 unsafe |

### `Send`/`Sync`

默认不要给所有 handle blanket `unsafe impl Send + Sync`。C 文档未声明 libobmm 全局线程安全；OBMM 资源还牵涉字符设备、mmap、NUMA、跨 host ownership 状态和释放时序。保守方案：

- `MemId` 作为纯值可以 `Copy + Send + Sync`，但只代表 ID，不代表释放权限。
- RAII owner 类型默认不手写 `Send`/`Sync`；让内部字段自然推导，必要时用 `PhantomData<*mut ()>` 阻止自动 `Send`/`Sync`。
- 如果后续上游明确声明 `obmm_unexport`/`obmm_unimport` 可跨线程调用，再逐类型实现 `Send`，并说明同一个 owner 不支持并发 mutable 操作。
- `Sync` 比 `Send` 更严格；除非所有方法都能并发调用且 C 库线程安全，否则不实现。

## async 与上层协议判断

初始版本不需要 async crate。`libobmm` 当前公开的是同步控制面：export/import/preimport/unimport、ownership 设置和查询。远端内存的数据面访问由普通 load/store、mmap、NUMA 迁移或系统调用完成，不是 fd-based readiness API，也没有回调或 event loop。

如果未来要提供异步能力，应放在 safe crate 之上，而不是 `obmm-sys`：

- 长耗时 export/import 可以由调用方放入 blocking 线程池。
- mmap/ownership 的高层 guard 可做成同步 RAII。
- 与 Tokio 的集成只有在出现可 poll 的 fd、netlink、uevent 或专用通知机制时才有必要。

## 发布与 CI 清单

发布前必须完成：

- `obmm-sys`：确定最低支持的 libobmm major version，写入 README 和 crate docs。
- `obmm-sys`：实现 `links = "obmm"`、`OBMM_LIB_DIR`、`OBMM_INCLUDE_DIR`、`OBMM_SYS_NO_LINK`、`DOCS_RS` 策略。
- `obmm-sys`：提交预生成绑定，并提供维护者再生命令。
- `obmm-sys`：添加 layout/constant/signature 检查，至少覆盖 flexible array header offset。
- `obmm`：实现 `MemId(NonZeroU64)`，拒绝无效 ID。
- `obmm`：实现 `MemDescBuf` 和 `PreimportInfoBuf` 的连续内存布局，禁止把 `Vec<u8>` 当成 C struct 字段。
- `obmm`：为 export/import/preimport 提供 RAII guard 和显式 `close`。
- `obmm`：为所有公开 `unsafe fn` 写 `# Safety`。
- `obmm`：错误类型覆盖 invalid ID、非零返回码、输入约束和 out param。
- CI：默认不要求 OBMM 内核模块和真实硬件；真实集成测试用 feature/env opt-in。
- docs.rs：确保无 `libobmm.so` 时可以生成文档，并清楚标注运行时需要系统库和 OBMM 环境。
- 文档：写明 OBMM 控制面时序：provider export、consumer import、数据访问、consumer unimport、provider unexport。
- 文档：写明共享 cacheable 模型中的 ownership 一致性责任，避免 safe API 暗示自动跨 host 协调。

## 未决项

| 未决项 | 当前保守处理 | 确认后影响 |
|---|---|---|
| `OBMM_MAX_LOCAL_NUMA_NODES` 和 `OBMM_MAX_PRIV_LEN` 来自 `<ub/obmm.h>`，本次未读取该头 | safe 层不硬编码或只用绑定常量；文档限制以运行时/编译期检查为准 | 可完善 builder 的最大长度和数组维度约束 |
| libobmm 是否提供 pkg-config 文件 | 不依赖 pkg-config，只作为可选探测 | 若上游补充 `.pc`，可把 pkg-config 设为第一探测路径 |
| libobmm 线程安全保证 | 不 blanket 实现 `Send`/`Sync` | 上游若给出保证，可逐 owner 类型放宽 |
| errno 语义 | 非零返回码保留 raw code，并可选采集 errno | 若上游明确所有失败都设置 errno，可增强 `Error` 诊断 |
| vendored 源码发布 | 初始不做 vendored | 若需要 vendored，需审查 Mulan PSL v2、内核头依赖、静态链接和平台矩阵 |

## 最终建议

把草案改成“`obmm-sys` 管 ABI 和链接，`obmm` 管所有权和安全语义”的双 crate 设计。`mem_id`、可变长描述符、ownership range、`export_useraddr` 和释放时序是 safe 层的核心风险点，必须在类型系统和文档中显式建模。构建时全量 bindgen、`u64` 句柄直出、`Vec<u8>` 替代 flexible array member、无条件 `Send`/`Sync` 和缺失 docs.rs/ABI 策略都应作为发布阻断项修正。
