# obmm Rust FFI 草案设计级修订方案

## 结论

当前草案不宜直接落地。`libobmm` 的公开 C API 包含不透明系统资源、柔性数组成员、出参结构、错误哨兵值、NUMA/ownership 状态和动态库 ABI 约束。把所有内容塞进一个 crate、在用户构建时全量 bindgen、把 `mem_id` 暴露成裸 `u64`、用普通 Rust struct 表示 `struct obmm_mem_desc { ... priv[] }`、并给所有 handle 无条件 `Send + Sync`，会把 C ABI、生命周期、线程安全和资源状态错误直接泄漏给安全 Rust 用户。

建议改为两层 crate：

- `obmm-sys`：最小、可审计、ABI 稳定的原始绑定和链接层。
- `obmm`：安全封装层，负责类型语义、资源生命周期、可变长描述符、错误建模、线程安全边界和文档降级。

## C API 事实约束

从 `obmm/src/libobmm/libobmm.h` 和 `obmm/doc/libobmm.md` 可见：

- `mem_id` 是 `uint64_t`，`OBMM_INVALID_MEMID == 0` 是错误/无效 ID。导出和引入在同一个 ID 空间。
- `obmm_export`、`obmm_export_useraddr`、`obmm_import` 返回 `mem_id`；失败不能只按负 errno 判断，必须处理 `0`。
- `obmm_unexport`、`obmm_unimport`、`obmm_preimport`、`obmm_unpreimport`、`obmm_set_ownership`、查询函数返回 `int`。
- `struct obmm_mem_desc` 和 `struct obmm_preimport_info` 都以 `uint8_t priv[]` 结尾，是 C 柔性数组成员，不是固定大小 Rust struct。
- `priv_len` 是 `uint16_t`，文档说明当前上限由 `OBMM_MAX_PRIV_LEN` 给出，且建议应用自用部分不超过 128 字节。该常量来自外部头 `ub/obmm.h`，本次可读材料没有给出具体值。
- `obmm_preimport_info.numa_id` 同时是入参和出参，`-1` 表示系统分配。
- `obmm_set_ownership` 操作文件描述符和虚拟地址范围，权限语义与 `PROT_NONE`、`PROT_READ`、`PROT_WRITE` 对齐。
- CMake 构建共享库 `libobmm.so`，输出名为 `obmm`，安装头文件到 `include`，库到 `lib64`，SOVERSION 取主版本。

## 主要问题与修订

### 1. crate 结构

草案问题：只做一个 `obmm` crate 会把 bindgen 产物、链接策略、ABI 检查、安全 API 和用户文档混在一起。后续任何 C 头变动都会影响安全层 semver。

修订方案：

- 新增 `obmm-sys` crate，暴露 `extern "C"` 函数、C 常量、C 布局类型和链接元数据。
- `obmm` crate 依赖 `obmm-sys`，只暴露安全或显式 `unsafe` 的领域 API。
- `obmm-sys` 版本跟随 C ABI 主版本和 Rust crate 修订；`obmm` 可以按安全 API 独立演进。

### 2. 绑定生成与发布策略

草案问题：在用户构建时全量 bindgen 会要求用户安装 clang、完整 C 头依赖和正确 include 路径。`libobmm.h` 还包含 `<ub/obmm.h>`，下游环境缺少该头会直接构建失败。全量绑定也会把不希望承诺的外部符号纳入 Rust API。

修订方案：

- `obmm-sys` 默认发布预生成绑定，仅覆盖 `libobmm.h` 中公开函数、`mem_id`、必要常量和必要布局。
- 提供可选 feature，例如 `bindgen`，供发行版维护者或开发者重新生成绑定。
- bindgen 必须 allowlist：
  - 函数：`obmm_export`、`obmm_unexport`、`obmm_export_useraddr`、`obmm_import`、`obmm_unimport`、`obmm_preimport`、`obmm_unpreimport`、`obmm_set_ownership`、`obmm_query_memid_by_pa`、`obmm_query_pa_by_memid`
  - 类型：`mem_id`、`obmm_mem_desc`、`obmm_preimport_info`
  - 常量：`OBMM_INVALID_MEMID`、`MAX_NUMA_NODES`，以及来自可用头文件的 `OBMM_MAX_*` 常量
- 对 docs.rs 使用预生成绑定和 `doc_cfg`，避免因缺少 libobmm/内核头/clang 导致文档构建失败。

### 3. 链接与发现

草案问题：`build.rs` 直接输出 `-lobmm` 太脆弱，不能处理 `lib64`、自定义安装前缀、交叉编译、pkg-config 缺失和 docs.rs。

修订方案：

- `obmm-sys/build.rs` 按顺序查找：
  1. `OBMM_LIB_DIR` / `OBMM_INCLUDE_DIR` 环境变量。
  2. `pkg-config`，如果项目后续提供 `.pc` 文件。
  3. 常见系统路径，包括 `/usr/lib64`、`/usr/local/lib64`、`/usr/lib`、`/usr/local/lib`。
- 链接输出使用 `cargo:rustc-link-lib=dylib=obmm`，必要时输出 `cargo:rustc-link-search=native=...`。
- docs.rs 或 `DOCS_RS=1` 时不强制探测本机库，只构建文档。
- 明确不在 crate 中静态 vendoring C 库，除非上游提供稳定源码包和许可证/构建流程。

### 4. `mem_id` 类型

草案问题：safe 层把 `mem_id` 暴露为 `u64`，并让 export/import 直接返回 `u64`，会丢失 `0` 是无效值、ID 绑定资源生命周期、导出 ID 与引入 ID 角色不同等语义。

修订方案：

- `obmm-sys` 中保留 C 等价类型，例如 `pub type mem_id = u64`。
- `obmm` 中定义非零 ID：

```rust
#[repr(transparent)]
pub struct MemId(NonZeroU64);
```

- 安全层函数返回 `Result<MemId, Error>` 或资源句柄，而不是裸整数。
- 区分角色：
  - `ExportedMemory`：由 `obmm_export` 或 `obmm_export_useraddr` 创建，`Drop` 调用 `obmm_unexport`。
  - `ImportedMemory`：由 `obmm_import` 创建，`Drop` 调用 `obmm_unimport`。
- 如需暴露 ID，提供 `as_raw_mem_id()`；从裸 ID 构造资源句柄必须是 `unsafe` 或受限 API，因为调用方必须保证所有权和释放责任。

### 5. 柔性数组结构

草案问题：用普通 Rust struct 表示 `obmm_mem_desc` 并包含 `Vec<u8> priv` 不符合 C ABI。C 期望 `priv` 紧随结构头连续存放，`Vec` 只会在 Rust struct 中存储指针、长度、容量，传给 C 会导致布局错误。

修订方案：

- `obmm-sys` 只定义头部布局，不把 `priv[]` 误建模为 `Vec<u8>`。可使用 `bindgen` 对柔性数组的表示，或手写 `#[repr(C)]` header：

```rust
#[repr(C)]
pub struct obmm_mem_desc_header {
    pub addr: u64,
    pub length: u64,
    pub seid: [u8; 16],
    pub deid: [u8; 16],
    pub tokenid: u32,
    pub scna: u32,
    pub dcna: u32,
    pub priv_len: u16,
}
```

- `obmm` 安全层提供拥有型 buffer，例如 `MemDescBuf`，内部是 `Box<[MaybeUninit<u8>]>` 或 `Vec<u8>` 原始字节区，布局为 `header + priv_len`。
- 只通过方法读写字段和 `priv()` / `priv_mut()`，不暴露可被用户随意移动字段的 repr-Rust 结构。
- 创建时验证 `priv_len <= u16::MAX`，并在可获得 `OBMM_MAX_PRIV_LEN` 时继续验证上限。
- `PreimportInfoBuf` 使用同样策略，处理 `numa_id` 入参/出参。

### 6. 错误模型

草案问题：错误只用 `i32` 会混淆两类失败：返回 `int` 的函数可能返回负 errno 或其他错误码；返回 `mem_id` 的函数用 `OBMM_INVALID_MEMID` 表示失败。

修订方案：

- 定义 `Error` 枚举：
  - `InvalidMemId`：返回 `OBMM_INVALID_MEMID`。
  - `Libc { code: i32 }` 或 `Os(std::io::Error)`：返回 `int` 的错误码。
  - `InvalidArgument`：Rust 层前置校验失败，例如长度、对齐、priv 长度、空切片。
  - `Unsupported`：当前平台、docs.rs 或缺少运行时库。
- 所有安全层 API 返回 `Result<T, Error>`。
- 对 `int` 返回值的解释应在实现前确认 libobmm 约定：若为 `0` 成功、负数失败，则封装为 errno；若失败时设置 `errno`，则读取 `errno`。当前材料没有明确说明，不应臆造。

### 7. 线程安全与资源所有权

草案问题：所有 handle 都 `unsafe impl Send + Sync` 没有依据。OBMM 涉及内核状态、文件描述符、内存映射、ownership 切换和导入/导出时序。无条件跨线程共享可能制造释放竞态和权限切换竞态。

修订方案：

- 默认不为资源句柄手写 `Send` / `Sync`。
- 如果句柄只包含 `MemId` 且 drop 调用只依赖进程级 libobmm 状态，可以考虑 `Send`，但必须先由上游文档或测试确认 libobmm 函数线程安全。
- 不给可变描述符 buffer、preimport buffer 或映射/ownership guard 实现 `Sync`。
- `set_ownership` 相关 API 应绑定借用的 fd 和地址范围，不应允许在 Rust 类型系统外自由并发切换。
- Drop 失败不能 panic；记录失败能力可通过显式 `close()` / `unexport()` / `unimport()` 返回 `Result`，Drop 作为兜底 best-effort。

### 8. API 形状

建议 safe 层提供以下方向的 API：

```rust
pub struct ExportOptions { flags: Flags, priv_data: Vec<u8>, deid: [u8; 16], /* ... */ }
pub struct ImportOptions { flags: Flags, base_dist: i32, /* ... */ }

pub struct MemDescBuf { /* header + priv bytes */ }
pub struct ExportedMemory { id: MemId, desc: MemDescBuf, flags: Flags }
pub struct ImportedMemory { id: MemId, numa: Option<i32>, flags: Flags }
pub struct Preimport { info: PreimportInfoBuf, flags: Flags }

pub fn export(length_by_numa: &[usize], options: ExportOptions) -> Result<ExportedMemory, Error>;
pub unsafe fn export_useraddr(pid: i32, va: NonNull<c_void>, len: usize, options: ExportOptions) -> Result<ExportedMemory, Error>;
pub fn import(desc: &MemDescBuf, options: ImportOptions) -> Result<ImportedMemory, Error>;
```

`export_useraddr` 应保持 `unsafe`，因为调用方必须保证虚拟地址范围有效、权限合适、生命周期满足 pin/export 过程要求。

`query_memid_by_pa`、`query_pa_by_memid` 属于维测接口，建议放在 `debug` 或 `diagnostics` feature 下。

### 9. flags、对齐和长度

草案没有处理 flags 和粒度约束。文档说明 OBMM 操作受基础粒度、PMD_SIZE、memory block size、页大小等约束，且地址/长度通常都需要对齐。

修订方案：

- flags 不使用裸 `u64`/`usize` 直接散落在 API 中，应定义 `Flags` newtype。具体 bit 值必须来自公开头文件，缺失时仅在 `obmm-sys` 暴露原始 `c_ulong`。
- 长度参数使用 `usize`，但调用 C 前转换为 `size_t` 并检查数组长度。
- `obmm_export` 的参数是 `const size_t length[OBMM_MAX_LOCAL_NUMA_NODES]`。safe API 应接收固定长度数组或 builder，并在运行/编译时与 `OBMM_MAX_LOCAL_NUMA_NODES` 对齐。
- 对 import/preimport/export_useraddr 的地址和长度提供前置校验接口；无法在用户态可靠获得的平台粒度时，应文档化由 libobmm 返回错误。

### 10. ABI 检查

草案问题：暂时不做 ABI 检查会让柔性数组前缀、字段对齐、`unsigned long` 宽度、`size_t` 宽度和 `SOVERSION` 变动风险直接进入 release。

修订方案：

- `obmm-sys` 增加布局测试：
  - `mem_id` 大小为 8。
  - `obmm_mem_desc` header 字段 offset 与 C 一致。
  - `obmm_preimport_info` header 字段 offset 与 C 一致。
  - `c_ulong`、`size_t` 映射符合目标平台。
- 对预生成绑定保存生成时的 header 版本、bindgen 版本和目标三元组说明。
- build.rs 尝试读取动态库 SOVERSION 或通过链接路径约束主版本；至少在文档中声明支持的 libobmm ABI 主版本。
- CI 中增加“重新生成绑定后 diff 为空”的检查，防止头文件变动未同步。

### 11. docs.rs 与不可用环境降级

草案问题：不做 docs.rs 降级会导致公开 crate 文档不可构建，尤其是 libobmm 依赖内核头和系统库。

修订方案：

- `obmm-sys` 在 `DOCS_RS=1` 时跳过库探测和 bindgen，仅使用预生成绑定。
- 文档示例使用 `no_run`，避免在 docs.rs 执行。
- 对需要真实 OBMM 内核模块、设备节点或 libobmm.so 的 API 明确标注运行时前提。

## 修订后的分层职责

`obmm-sys`：

- 提供 C ABI 原始绑定。
- 处理动态链接搜索。
- 保留 C 类型名和常量。
- 提供 ABI/layout 测试。
- 不承诺安全性和资源生命周期。

`obmm`：

- 使用 `MemId(NonZeroU64)` 表达有效 ID。
- 使用 `ExportedMemory` / `ImportedMemory` / `Preimport` 表达所有权。
- 使用拥有型可变长 buffer 表达 `obmm_mem_desc` 和 `obmm_preimport_info`。
- 把 C 返回值转换为 `Result`。
- 将 `export_useraddr`、从 raw id 接管所有权、裸指针/裸 fd 操作保持为 `unsafe` 或受限 API。
- 不无条件实现 `Send + Sync`。

## 最小验收标准

- 不存在将 `priv[]` 建模为 `Vec<u8>` 的 FFI struct。
- safe API 不返回裸 `u64` 作为成功结果。
- `OBMM_INVALID_MEMID` 被转换为错误。
- `ExportedMemory` 和 `ImportedMemory` 的释放路径分别调用正确的 unexport/unimport。
- docs.rs 不依赖本机安装 `libobmm.so`、clang 或内核头即可构建文档。
- ABI/layout 测试覆盖两个柔性数组结构的 header 前缀。
- `Send` / `Sync` 只有在有明确线程安全依据时才实现，并在文档中说明依据和限制。

## 对原草案的逐项替换

| 原草案 | 修订方案 |
| --- | --- |
| 只做一个 `obmm` crate | 拆为 `obmm-sys` 和 `obmm` |
| `build.rs` 直接链接 `-lobmm` | 环境变量、pkg-config、系统路径分层探测，docs.rs 跳过探测 |
| 用户构建时全量 bindgen | 默认预生成 allowlist 绑定，可选 bindgen feature |
| safe 层暴露 `mem_id` 为 `u64` | `MemId(NonZeroU64)`，资源句柄区分 export/import |
| export/import 直接返回 `u64` | 返回 `Result<ExportedMemory/ImportedMemory, Error>` |
| `obmm_mem_desc` 用普通 Rust struct + `Vec<u8>` | 使用 `repr(C)` header + 连续拥有型 byte buffer |
| 所有 handle `Send + Sync` | 默认不手写；逐类型、逐依据开放 |
| 错误只用 `i32` | 区分 invalid memid、int 错误码、Rust 参数错误和环境不支持 |
| 暂不做 ABI 检查 | 增加 size/offset/layout/SOVERSION/绑定 diff 检查 |
| 暂不做 docs.rs 降级 | docs.rs 使用预生成绑定并跳过本机库探测 |
