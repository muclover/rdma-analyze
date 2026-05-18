# obmm/src/libobmm Rust FFI 发布架构设计

## 1. 目标与边界

本设计面向当前仓库 `obmm/src/libobmm` 暴露的 C API，目标是形成可发布、可维护、尽量安全的 Rust FFI 绑定与高级封装。静态依据来自：

- `obmm/src/libobmm/libobmm.h`
- `obmm/src/libobmm/CMakeLists.txt`
- `obmm/src/libobmm/libobmm.c`
- `obmm/doc/libobmm.md` 以及 export/import/preimport/unimport/unexport/query/set_ownership/shmdev 文档

本仓库未包含 `libobmm.h` 所依赖的 `<ub/obmm.h>`，因此 `OBMM_MAX_LOCAL_NUMA_NODES`、`OBMM_EXPORT_FLAG_*`、`OBMM_IMPORT_FLAG_*`、`OBMM_MAX_PRIV_LEN`、ioctl 结构等 ABI 细节必须从目标系统安装的 OBMM/UB 头文件取得，Rust crate 不应凭文档推测硬编码这些数值。文档中能确认的业务约束，例如 `OBMM_INVALID_MEMID == 0`、`mem_id == uint64_t`、私有数据当前上限 512、典型基础粒度 2 MiB，可用于校验策略说明，但发布实现仍应以系统头文件或运行时返回为准。

不建议直接绑定内核 ioctl 结构。`libobmm.so` 已经封装 `/dev/obmm` 打开、vendor adaptor、sysfs 查询、ioctl 调用和错误返回，Rust 首版应绑定公开的 `libobmm.h` 函数，而不是绕过用户态库。

## 2. C API 与风险摘要

公开函数：

- `obmm_export(length, flags, desc) -> mem_id`
- `obmm_export_useraddr(pid, va, length, flags, desc) -> mem_id`
- `obmm_unexport(id, flags) -> int`
- `obmm_import(desc, flags, base_dist, numa) -> mem_id`
- `obmm_unimport(id, flags) -> int`
- `obmm_preimport(preimport_info, flags) -> int`
- `obmm_unpreimport(preimport_info, flags) -> int`
- `obmm_set_ownership(fd, start, end, prot) -> int`
- `obmm_query_memid_by_pa(pa, id, offset) -> int`
- `obmm_query_pa_by_memid(id, offset, pa) -> int`

关键数据结构：

- `struct obmm_mem_desc` 以 flexible array member `priv[]` 结尾。`priv_len` 和 `priv` 总是输入字段；export 成功后 `addr`、`length`、`tokenid` 回填；import 时 `addr`、`length`、`seid`、`deid`、`scna`、`dcna`、`priv_len`、`priv` 为输入。
- `struct obmm_preimport_info` 也以 flexible array member `priv[]` 结尾。文档说明 preimport 当前忽略 `priv_len/priv`，但 C 实现仍把它们传入 ioctl 命令，因此 Rust 侧仍按 FFI 内存布局正确构造。
- EID 是 16 字节数组，并按文档以 little-endian 顺序存放。
- memid 的 0 值是错误/无效 ID。export/import 返回 0 时要读取 `errno`。

主要安全风险：

- C API 使用裸指针和 flexible array，需要 Rust 确保描述符分配大小至少为 `sizeof(header) + priv_len`，且调用期间指针有效。
- export/unexport 与 import/unimport 有跨主机生命周期顺序要求。提供方无法知道远端是否仍在使用导出内存，`Drop` 自动 unexport 可能造成远端崩溃或硬件错误。
- `obmm_export_useraddr` 会 pin 目标地址范围，要求 PMD 对齐、可读写、大页/THP/hugetlb 等约束，且 unexport 前内核态访问目标内存可能导致 panic。该接口必须设计为显式 unsafe。
- `obmm_set_ownership` 只适用于 cacheable 字符设备映射，不适用于 `O_SYNC` non-cacheable 映射和 PMD 映射。错误使用可能导致一致性风险或 bus error。
- mmap 后 load/store 的权限必须与 OBMM ownership 状态匹配。Rust 类型系统无法跨主机证明全局唯一写者，只能在本进程/本 crate 范围内收窄误用。

## 3. Crate 分层

建议发布为 workspace 下两个 crate：

1. `obmm-sys`

   低层 FFI crate，只负责 ABI 绑定、链接、常量暴露和 errno 保留，不提供资源安全语义。

2. `obmm`

   安全/半安全高级封装 crate，负责 builder、RAII handle、mmap 封装、ownership token、错误类型和文档化 unsafe 边界。

可以将文档示例和集成测试放入第三个非发布包 `obmm-test-fixtures`，但首版不必发布。

## 4. `obmm-sys` 设计

### 4.1 绑定生成策略

首选 `bindgen` 生成绑定：

- 输入 wrapper header：

  ```c
  #include <libobmm.h>
  ```

- 只 allowlist 公开 API、公开结构和 OBMM 常量：

  ```text
  obmm_.*
  mem_id
  OBMM_.*
  ```

- blocklist 内核私有 ioctl 结构，除非 `<ub/obmm.h>` 公开且确实属于应用 ABI。

因为 `struct obmm_mem_desc` 和 `struct obmm_preimport_info` 使用 flexible array member，bindgen 往往会生成零长度占位字段或省略尾部数组。`obmm-sys` 应额外提供 `#[repr(C)]` 的 header-only 镜像类型，供高级 crate 计算布局和分配：

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

#[repr(C)]
pub struct obmm_preimport_info_header {
    pub pa: u64,
    pub length: u64,
    pub base_dist: libc::c_int,
    pub numa_id: libc::c_int,
    pub seid: [u8; 16],
    pub deid: [u8; 16],
    pub scna: u32,
    pub dcna: u32,
    pub priv_len: u16,
}
```

这些镜像必须通过编译期 layout 测试与 bindgen 输出或 C helper 断言一致。若 bindgen 可直接生成可用的 header 字段类型，则以生成结果为准，手写镜像只作为兼容层。

### 4.2 链接与构建

`build.rs` 职责：

- 通过 `pkg-config` 查找 `libobmm`，如果上游未提供 `.pc` 文件，则支持环境变量：
  - `OBMM_INCLUDE_DIR`
  - `OBMM_LIB_DIR`
  - `OBMM_STATIC` 可选
- 输出 `cargo:rustc-link-lib=obmm`。
- 设置 bindgen include path，确保能找到 `libobmm.h` 和 `<ub/obmm.h>`。
- 不 vendoring、不编译仓库内 C 项目。发布 crate 假定目标系统已安装 libobmm 开发包。

`obmm-sys` 的 crate metadata 应明确：

- 需要 Linux。
- 需要 OBMM 内核模块、`/dev/obmm`、`/dev/obmm_shmdev{id}` 和 UB 控制器 sysfs。
- `libobmm.so` CMake 安装路径是 `lib64`，头文件安装到 `include`。
- `libobmm.so` 当前 SOVERSION 来自 `obmm/VERSION` 的 major，静态读取为 `1.0.1`。

### 4.3 低层 API 约定

`obmm-sys` 不隐藏 C 返回值：

- 返回 `mem_id` 的函数保留 `u64`，调用者判断 0。
- 返回 `int` 的函数保留 `c_int`，调用者判断 `-1` 或 `<0`。
- 不在 sys 层自动读取 errno，但高级层应在失败后立即调用 `std::io::Error::last_os_error()`。

## 5. 高级 crate `obmm` 设计

### 5.1 基础类型

```rust
pub struct MemId(NonZeroU64);
pub struct Eid([u8; 16]);
pub struct Cna(u32);
pub struct NumaNode(i32);
pub struct BaseDistance(u8);
pub struct PhysicalAddress(u64);
pub struct Uba(u64);
```

`MemId::new(raw)` 在 raw 为 0 时返回 `None`，避免把无效 ID 放入已成功资源对象。

错误类型：

```rust
pub enum Error {
    Os(std::io::Error),
    InvalidInput(&'static str),
    Unsupported(&'static str),
}
```

首版可只使用 `thiserror` 派生。所有 C 调用失败都转换为 `Error::Os(last_os_error())`。

### 5.2 Flexible descriptor 封装

提供内部拥有型 buffer：

```rust
struct MemDescBuf {
    buf: Box<[MaybeUninit<u8>]>,
}
```

能力：

- 按 `Layout::new::<obmm_mem_desc_header>()` 加 `priv.len()` 分配。
- 校验 `priv.len() <= max_priv_len()`。若常量可由 bindgen 暴露则使用系统常量；否则高级 API 设置保守上限 512 并在文档说明来自当前 libobmm 文档。
- 初始化 header 全字段，未用字段清零，复制 priv 到 header 后的尾部。
- 暴露 `as_mut_ptr() -> *mut obmm_mem_desc` 或 bindgen 对应类型。
- C 调用返回后读取 header 字段生成 `MemoryDescriptor`。

公开不可变描述符：

```rust
pub struct MemoryDescriptor {
    pub addr: Uba,
    pub length: u64,
    pub seid: Eid,
    pub deid: Eid,
    pub tokenid: u32,
    pub scna: Cna,
    pub dcna: Cna,
    priv_data: Vec<u8>,
}
```

该类型用于跨进程/跨主机控制面传输时，需要额外提供显式序列化。不要直接承诺 `MemoryDescriptor` 的 Rust 序列化格式等同 C ABI。建议开启可选 feature `serde`，以稳定字段名序列化为 JSON/TOML/bincode 由应用自选。

`PreimportInfoBuf` 同理处理 `struct obmm_preimport_info`。

### 5.3 Flag 类型

使用 `bitflags` 包装公开 flag：

```rust
bitflags! {
    pub struct ExportFlags: libc::c_ulong {
        const FAST = sys::OBMM_EXPORT_FLAG_FAST as _;
        const ALLOW_MMAP = sys::OBMM_EXPORT_FLAG_ALLOW_MMAP as _;
    }
}

bitflags! {
    pub struct ImportFlags: libc::c_ulong {
        const NUMA_REMOTE = sys::OBMM_IMPORT_FLAG_NUMA_REMOTE as _;
        const ALLOW_MMAP = sys::OBMM_IMPORT_FLAG_ALLOW_MMAP as _;
        const PREIMPORT = sys::OBMM_IMPORT_FLAG_PREIMPORT as _;
    }
}
```

如果 bindgen 未能生成 flag 常量，构建应失败并提示安装匹配版本的 OBMM 开发头，而不是在 Rust 中复制数值。

高级层应额外校验：

- import 必须且只能指定 `NUMA_REMOTE` 或 `ALLOW_MMAP` 之一。
- `PREIMPORT` 必须和 `NUMA_REMOTE` 同时使用。
- `base_dist` 只允许 `0` 或 `11..=255`，文档还指出 C 实现只先筛掉负数和大于 `UINT8_MAX`，内核会继续验证；Rust 应在本地给出更早错误。
- export_useraddr 的 flags 必须为 0。
- unexport/unimport/unpreimport flags 首版只允许 0。

### 5.4 资源对象

#### ExportedMemory

```rust
pub struct ExportedMemory {
    id: MemId,
    desc: MemoryDescriptor,
    close_on_drop: CloseOnDrop,
}
```

创建：

```rust
pub struct ExportBuilder { ... }
impl ExportBuilder {
    pub fn lengths(self, lengths: &[usize]) -> Result<Self>;
    pub fn deid(self, deid: Eid) -> Self;
    pub fn private_data(self, data: impl AsRef<[u8]>) -> Result<Self>;
    pub fn flags(self, flags: ExportFlags) -> Self;
    pub fn export(self) -> Result<ExportedMemory>;
}
```

`lengths` 参数长度必须等于 `max_local_numa_nodes()`，或提供 `NumaLengths` 类型确保长度正确。因为该常量来自 `<ub/obmm.h>`，推荐：

```rust
pub struct NumaLengths(Vec<usize>);
```

构造时根据 sys 常量校验长度，非零长度之和必须大于 0。对齐校验可以提供可选 helper，但真实粒度可能依赖内核页大小、UMMU 和 memseg；Rust 不应只靠 2 MiB 断言作为唯一真相。

销毁：

- `pub fn unexport(self) -> Result<()>` 显式消费对象。
- `Drop` 行为默认可以尝试 unexport，但必须文档化风险：如果远端仍在使用，C 文档指出可能导致崩溃、数据不一致、硬件故障或 kernel panic。
- 更保守的发布 API 可采用 `CloseOnDrop::Disabled` 默认，要求用户显式调用 `unexport()`；另提供 `enable_drop_unexport()` 适合单进程测试。考虑到 Rust 用户对 RAII 的期待，建议默认 Drop 尝试释放，但在类型文档中用醒目的 `Safety and distributed lifetime` 章节说明它不是跨主机协调机制。

#### ExportedUserAddr

```rust
pub struct ExportedUserAddr {
    id: MemId,
    desc: MemoryDescriptor,
    ptr: NonNull<c_void>,
    len: usize,
}
```

构造必须是 unsafe：

```rust
pub unsafe fn export_useraddr(
    pid: libc::pid_t,
    va: NonNull<c_void>,
    len: usize,
    deid: Eid,
    priv_data: &[u8],
) -> Result<ExportedUserAddr>;
```

调用者必须保证：

- `va..va+len` 在目标进程中有效、可读写。
- 地址和长度满足 PMD_SIZE、UMMU 和大页/THP/hugetlb 约束。
- 目标内存在 unexport 前不会被以违反 OBMM 文档的方式访问或释放。
- pid 为 0 时表示当前进程。

#### ImportedMemory

```rust
pub struct ImportedMemory {
    id: MemId,
    numa: Option<NumaNode>,
    mode: ImportMode,
}

pub enum ImportMode {
    MmapAllowed,
    NumaRemote { preimported: bool },
}
```

创建：

```rust
pub struct ImportBuilder { ... }
impl ImportBuilder {
    pub fn descriptor(self, desc: MemoryDescriptor) -> Self;
    pub fn allow_mmap(self) -> Self;
    pub fn numa_remote(self, numa: Option<NumaNode>, base_dist: BaseDistance) -> Self;
    pub fn preimported(self) -> Self;
    pub fn import(self) -> Result<ImportedMemory>;
}
```

销毁 `unimport(self) -> Result<()>`。`Drop` 同样可尝试 cleanup，但 EBUSY 时无法阻止析构；因此显式关闭方法必须存在，且返回错误给调用方处理。

#### PreimportReservation

```rust
pub struct PreimportReservation {
    pa: PhysicalAddress,
    length: u64,
    numa: NumaNode,
    route: Route,
}
```

`preimport()` 成功后更新 `numa_id`，如果输入为 -1 则记录系统分配的 remote NUMA ID。`unpreimport(self)` 必须使用原始 `pa` 和 `length` 精确释放，其他字段按文档对 unpreimport 无实质影响，但 Rust 保留原始 route 便于审计和日志。

### 5.5 字符设备与 mmap API

OBMM 内存设备路径固定为 `/dev/obmm_shmdev{id}`。高级 crate 应提供：

```rust
pub struct ShmDevice {
    file: std::fs::File,
    id: MemId,
    cache_mode: CacheMode,
}

pub enum CacheMode {
    Cacheable,
    NonCacheable,
}
```

打开：

- `open_readonly(id)`
- `open_readwrite(id)`
- `open_readwrite_noncacheable(id)` 使用 `O_SYNC`

映射：

```rust
pub struct Mapping {
    ptr: NonNull<u8>,
    len: usize,
    fd: BorrowedFd<'static>, // 或内部持有 Arc<File>
    cache_mode: CacheMode,
    granularity: MappingGranularity,
}

pub enum MappingGranularity {
    Page,
    Pmd,
}
```

`mmap` 参数约束：

- 必须使用 `MAP_SHARED`。
- offset 和 length 对页大小对齐；PMD 映射时 length 按 PMD_SIZE 对齐，offset 高位置入 `OBMM_MMAP_FLAG_HUGETLB_PMD`。该 flag 文档定义为 `1UL << 63`，但推荐仍从上游头文件取得；若头文件无定义，可在高级层作为 Linux `off_t` 约定常量集中封装，并附测试。
- 同一 obmm_shmdev 不允许混合映射粒度。Rust 可在单个 `ShmDevice` 实例内记录首次映射粒度，但无法覆盖其他进程或其他 fd。

Ownership：

```rust
pub enum Protection {
    None,
    Read,
    ReadWrite,
}

impl Mapping {
    pub fn set_ownership(&self, range: Range<usize>, prot: Protection) -> Result<()>;
}
```

本方法仅当 `cache_mode == Cacheable` 且 `granularity == Page` 时可调用，否则返回 `Error::Unsupported`。范围必须非空且页对齐。`Protection::ReadWrite` 调用 C API 时使用 `PROT_WRITE` 或 `PROT_READ | PROT_WRITE` 均可；为贴近文档，Rust 可传 `PROT_READ | PROT_WRITE`。

裸内存访问不应直接通过 `Deref<[u8]>` 暴露为安全可写切片，因为跨主机 ownership 无法由 Rust 类型系统证明。建议：

- `unsafe fn as_slice(&self) -> &[u8]`
- `unsafe fn as_mut_slice(&mut self) -> &mut [u8]`
- 或提供作用域 API：

  ```rust
  pub unsafe fn with_read<T>(&self, range: Range<usize>, f: impl FnOnce(&[u8]) -> T) -> Result<T>;
  pub unsafe fn with_write<T>(&mut self, range: Range<usize>, f: impl FnOnce(&mut [u8]) -> T) -> Result<T>;
  ```

这些函数可以在本进程内先调用 `set_ownership`，但仍必须 unsafe，因为无法协调其他 host。

### 5.6 查询 API

调试/维测接口：

```rust
pub fn query_memid_by_pa(pa: PhysicalAddress) -> Result<(MemId, u64)>;
pub fn query_pa_by_memid(id: MemId, offset: u64) -> Result<PhysicalAddress>;
pub fn contains_pa(pa: PhysicalAddress) -> Result<bool>;
```

文档说明该组接口不应用在性能敏感业务面，高级 crate 文档应重复该限制。若只检查有效性，可传空出参；Rust 可直接调用并忽略结果。

## 6. Safety 契约

高级 crate 应在每个 unsafe API 文档中写清楚调用者责任。

需要 unsafe 的能力：

- `export_useraddr`
- 从原始 memid 构造 `ExportedMemory`/`ImportedMemory`
- 将 mmap 区域暴露为 Rust slice
- 声明跨主机 ownership 已经满足的读写访问
- 反序列化来自不可信来源的 `MemoryDescriptor` 后直接 import，可设计为 safe 但必须完整校验长度、地址非零、私有数据长度、flags 组合；物理地址真实性仍由调用者/内核负责

可以 safe 的能力：

- `obmm_export` 从 OBMM 内存池导出，前提是参数 builder 完整校验，失败返回 errno。
- `obmm_import` 控制面调用本身可以 safe，但对 import 后内存的访问仍受 unsafe/ownership 控制。
- `preimport/unpreimport`
- `query_*`
- 打开设备和 mmap 建立映射可以 safe；读写映射内容不 safe。

`Send`/`Sync`：

- `MemId`、`MemoryDescriptor` 可以 `Send + Sync`。
- `ExportedMemory`、`ImportedMemory` 包含跨主机资源句柄，是否 `Send` 可允许，`Sync` 不建议自动实现，除非内部状态完全不可变且 cleanup 受 Mutex 保护。
- `Mapping` 不应无条件 `Send + Sync`。如果持有 `NonNull<u8>` 且可改变 ownership，建议默认 `!Send + !Sync`，后续根据用例通过 feature 和严格不变量放开。

## 7. 生命周期与故障处理

推荐显式关闭模式：

```rust
impl ExportedMemory {
    pub fn unexport(self) -> Result<()>;
    pub fn into_raw_memid(self) -> MemId;
}
```

`Drop` 中若执行 cleanup：

- 忽略错误但可通过 `log` feature 记录。
- 不 panic。
- 防止 double free：内部使用 `Option<MemId>` 或状态枚举，显式关闭成功后清空。

对 EBUSY 的处理：

- unexport/unimport 可能因设备仍被 open/mmap 返回 EBUSY。
- Rust API 应鼓励先 drop `Mapping`，再 drop/close `ShmDevice`，最后 unimport/unexport。
- 如果 `Drop` cleanup 遇到 EBUSY，只能记录；用户需要显式调用关闭方法获取错误。

对 errno 的处理：

- C 调用失败后立即读取 `last_os_error()`，避免后续 Rust/系统调用覆盖 errno。
- export/import 返回 0 视为失败，即使 errno 为 0，也返回 `Error::Os` 或 `Error::InvalidInput("libobmm returned OBMM_INVALID_MEMID")`，并保留诊断信息。

## 8. 版本、feature 与发布

建议 crate 版本策略：

- `obmm-sys` 主版本跟随可兼容的 C ABI major。当前 libobmm 版本静态读到 `1.0.1`，SOVERSION 使用 major `1`。
- `obmm` 高级 crate 使用独立 semver。C ABI 新增函数可 minor；改变 safe API 语义需 major。

features：

- `bindgen` 默认开启，用目标系统头文件生成绑定。
- `pregenerated-bindings` 可选，但只适合针对明确发行版/架构发布。由于 `<ub/obmm.h>` 缺失且硬件相关，不建议首版默认使用。
- `serde` 给 `MemoryDescriptor`、`Eid` 等控制面类型提供序列化。
- `log` 在 Drop cleanup 失败、参数自动修正等场景记录。

平台：

- `cfg(target_os = "linux")`。
- 非 Linux 直接 compile_error 或让高级 crate API 返回 unsupported，sys crate 更适合 compile_error。

许可证：

- libobmm 用户态库为 Mulan PSL v2。Rust crate 需要选择与项目发布策略兼容的许可证，并在 README 中说明运行时链接的 libobmm 许可证。

## 9. 测试计划

不依赖硬件的测试：

- `obmm-sys` bindgen smoke test：确认公开函数存在，常量存在。
- layout 测试：`size_of`、`align_of`、字段 offset 与 C 编译 helper 输出一致。该测试只编译极小 helper，不编译 OBMM C 项目。
- descriptor buffer 单元测试：不同 priv 长度、0 长度、超限、字段回读、尾部数据复制。
- flag builder 校验：import flag 互斥、preimport 依赖、base_dist 范围。
- Drop 状态机测试：显式关闭后 Drop 不重复调用。底层 FFI 可用 trait/mock 抽象替代真实 libobmm。
- mmap 参数计算测试：page/PMD offset、范围对齐、cacheable/NC ownership 禁止规则。

需要 OBMM 环境的集成测试，默认 ignored：

- `/dev/obmm` 存在时 query 失败路径。
- export 2 MiB allow_mmap，然后 open `/dev/obmm_shmdev{id}`，mmap `PROT_NONE`，set ownership write/none，munmap/close/unexport。
- import allow_mmap 需要真实远端描述符，放入手动测试，不在 CI 默认跑。
- preimport/unpreimport 需要 remote NUMA 和 UB 控制器，放入硬件实验室 CI。

运行时验证不可只停留在编译通过。对 mmap/ownership 的发布验收至少要在支持硬件上执行一次真实 open/mmap/set_ownership/munmap/close/unexport 流程。

## 10. 文档与示例

README 应包含：

- 安装前提：libobmm.so、libobmm.h、`<ub/obmm.h>`、内核模块、硬件平台。
- 最小 export 示例。
- import 示例，强调 MemoryDescriptor 需要由提供方控制面安全传输。
- mmap 示例，明确 cacheable 映射需要 ownership 切换。
- `export_useraddr` unsafe 示例和风险说明。
- 销毁顺序：远端停止访问 -> 使用方 unimport -> 提供方 unexport。

示例代码必须避免暗示 Rust crate 能自动解决跨主机一致性。所有权模型的全局协调仍由应用协议负责。

## 11. 建议首版 API 清单

首版优先实现：

- `MemId`、`Eid`、`MemoryDescriptor`
- `ExportBuilder` + `ExportedMemory::unexport`
- `ImportBuilder` + `ImportedMemory::unimport`
- `PreimportBuilder` + `PreimportReservation::unpreimport`
- `ShmDevice::open_*`
- `Mapping::map_page`、`Mapping::unmap/drop`
- `Mapping::set_ownership`
- `query_memid_by_pa`、`query_pa_by_memid`

延后：

- async API。libobmm 接口是同步 ioctl/文件操作，且控制面调用不适合伪 async；需要时由用户放到 blocking 线程池。
- 直接 ioctl 绑定。
- 自动跨主机 lease/lock 协议。这属于分布式协调层，不属于 libobmm FFI 首版。
- PMD 映射的安全读写封装。先提供低层映射能力和明确限制。

## 12. 结论

可发布 Rust FFI 应以 `libobmm.so` 的公开 C ABI 为边界，采用 `obmm-sys` + `obmm` 双层结构。`obmm-sys` 从目标系统头文件生成 ABI，避免复制未在仓库内出现的 `<ub/obmm.h>` 常量；`obmm` 用 builder、拥有型 flexible descriptor buffer、显式资源关闭、mmap/ownership 状态约束和清晰 unsafe 契约封装主要误用点。

这个设计不能也不应隐藏 OBMM 的核心事实：跨主机生命周期和 cache ownership 是应用协议的一部分。Rust 封装的价值在于减少本进程内的 FFI 内存布局错误、flag 组合错误、无效 memid、重复释放和错误 errno 处理，同时把无法静态保证的分布式一致性要求明确留在 unsafe 文档和显式 API 边界上。
