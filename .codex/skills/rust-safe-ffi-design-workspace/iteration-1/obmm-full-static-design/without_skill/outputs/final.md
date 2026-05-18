以下方案基于静态阅读 `obmm/src/libobmm` 头文件、CMake、实现和 `obmm/doc` 文档；未编译 C 项目、未安装依赖、未实现代码。

**目标分层**

设计为三层发布：

1. `obmm-sys`
   原始 FFI crate，只暴露 C ABI、常量、结构体、链接规则，不承诺 Rust 安全语义。

2. `obmm`
   安全封装 crate，提供 `MemId`、描述符构造器、导出/引入/预引入生命周期、错误处理和 mmap 辅助。

3. `obmm-tools` 或 examples
   只放示例和诊断工具，不进入核心库路径，避免把环境假设、sysfs 探测和业务流程混进 API。

**`obmm-sys` 设计**

`libobmm.h` 依赖 `<ub/obmm.h>`，而当前仓库没有该头文件。因此 `obmm-sys` 不应手写所有 `OBMM_*` 数值常量；发布版应通过系统安装的 OBMM/UB 头文件生成绑定。

推荐策略：

- 默认使用 `bindgen` 读取系统 `libobmm.h` 和 `ub/obmm.h`。
- `build.rs` 通过 `pkg-config` 优先发现 `libobmm`；没有 `.pc` 时回退到 `/usr/include`、`/usr/local/include`、`/usr/lib64`。
- 链接 `dylib=obmm`，因为 CMake 产物是 `libobmm.so`，SOVERSION 跟随主版本。
- 提供 `OBMM_LIB_DIR`、`OBMM_INCLUDE_DIR` 环境变量覆盖。
- CI 可保留一份最小 mock header 做 bindgen smoke test，但 crates.io 发布的真实构建必须使用目标系统头文件。

FFI 类型：

- `pub type mem_id = u64`
- `struct obmm_mem_desc` 和 `struct obmm_preimport_info` 需要保留 C 布局。
- flexible array `priv[]` 不应由安全层直接引用；`sys` 层只暴露 bindgen 结果，安全层负责分配 `sizeof(header) + priv_len` 的连续内存。
- `unsigned long` 在 Rust 侧使用 `libc::c_ulong`，不要假定等于 `u64`。
- `size_t` 使用 `libc::size_t`。
- `void *` 使用 `*mut libc::c_void`。

**安全 crate 核心类型**

建议 API 以这些类型为中心：

```text
MemId(NonZeroU64)
Eid([u8; 16])
Cna(u32)
NumaNode(i32)
PhysicalAddress(u64)
UserAddress(NonNull<c_void>)
Length(usize)
PrivateData(Vec<u8>)
```

`MemId` 用 `NonZeroU64` 表达 `OBMM_INVALID_MEMID == 0`，所有返回 0 的 C 调用转成 `Err(ObmmError)`。

描述符不要让用户直接写 C struct，提供 builder：

```text
MemDescBuilder
ExportDescBuilder
ImportDescBuilder
PreimportInfoBuilder
```

构造器负责：

- `priv_len <= OBMM_MAX_PRIV_LEN`
- `priv_len == 0` 时仍初始化为无私有数据
- EID 固定 16 字节
- export 只允许配置 `deid` 和 private data
- import 必须配置 `addr`、`length`、`seid`、`scna`，按需配置 `deid`、`dcna`、private data
- preimport 必须配置 `pa`、`length`、`seid`、`scna`、`base_dist`、`numa_id`

对齐校验建议分两级：

- 安全层提供 `validate_basic_granularity()`、`validate_page_aligned()` 等本地检查。
- 真正的 `OBMM_BASIC_GRANU`、`OBMM_MEMSEG_SIZE`、remote NUMA 粒度仍以 C/内核返回为准，因为文档说明粒度受内核页大小和平台影响。

**错误模型**

C API 的错误通道是返回值加 `errno`：

- `mem_id` 返回 0 表示失败。
- `int` 返回 -1 表示失败。
- 查询类返回负值表示失败。
- 详细错误在 `errno`。

Rust 侧设计：

```text
type Result<T> = std::result::Result<T, ObmmError>;

ObmmError {
    source: std::io::Error,
    operation: Operation,
}
```

`Operation` 可区分：

```text
Export
ExportUserAddr
Import
Unexport
Unimport
Preimport
Unpreimport
SetOwnership
QueryMemIdByPa
QueryPaByMemId
OpenShmDev
Mmap
Munmap
```

不要把 errno 过度枚举成业务错误；保留 `io::ErrorKind` 和 raw errno 更利于平台诊断。

**生命周期设计**

OBMM 的销毁操作有真实副作用，尤其 `unexport` 要求用户保证远端已停止使用。因此不建议在 `Drop` 中静默执行 `unexport`。

推荐：

```text
ExportedMemory::unexport(self) -> Result<()>
ImportedMemory::unimport(self) -> Result<()>
PreimportReservation::unpreimport(self) -> Result<()>
```

`Drop` 策略：

- `MappedRegion` 可以在 `Drop` 中 best-effort `munmap`，因为这是本进程资源。
- `ShmDevice` 可以在 `Drop` 中 close fd。
- `ImportedMemory` 可选 feature 支持 Drop 自动 `unimport`，默认只在 debug 日志中提示泄漏风险。
- `ExportedMemory` 默认不在 Drop 中 `unexport`，避免因 Rust 对象生命周期结束导致远端不可预测访问。
- 提供 `OwnedExportedMemory` 这种显式 RAII 类型给确实希望作用域释放的用户，但文档必须强调远端同步责任。

**导出 API**

建议提供两组接口：

```text
Obmm::export(lengths, ExportOptions, ExportDescriptor) -> Result<ExportedMemory>
Obmm::export_user_addr(pid, ptr, length, ExportUserAddrOptions, ExportDescriptor) -> Result<ExportedMemory>
```

`ExportOptions`：

- `fast`
- `allow_mmap`

`export_user_addr` 的 flags 当前必须为 0，因此安全 API 不暴露通用 flags，只暴露空 options，避免误用。

`ExportedMemory` 保存：

```text
mem_id: MemId
desc: MemoryDescriptor
allow_mmap: bool
```

`MemoryDescriptor` 是 Rust owned 表示，可序列化，用于跨机器传递 import 所需信息：

```text
addr
length
seid
deid
tokenid
scna
dcna
private_data
```

注意 export 返回后 `addr` 是 UBA，`tokenid` 有效，`scna/dcna` 被 C 实现置 0。

**引入 API**

建议拆分 mmap 模式和 NUMA remote 模式，避免 flags 组合错误：

```text
Obmm::import_mmap(desc) -> Result<ImportedMemory>
Obmm::import_numa(desc, NumaPolicy) -> Result<ImportedNumaMemory>
Obmm::import_preimported(desc) -> Result<ImportedNumaMemory>
```

文档要求 `OBMM_IMPORT_FLAG_ALLOW_MMAP` 和 `OBMM_IMPORT_FLAG_NUMA_REMOTE` 必须且只能指定一个；安全 API 应通过不同函数保证这一点。

`NumaPolicy`：

```text
Auto
Specific(NumaNode)
WithBaseDistance { numa: Option<NumaNode>, base_dist: BaseDistance }
```

`BaseDistance` 构造时校验：

- 允许 0
- 允许 11..=255
- 拒绝 1..=10 和 >255

`import_preimported` 不暴露 `base_dist` 和 `numa`，因为 C 文档说明 preimport 模式下这两个参数被忽略，实际 NUMA 节点由预引入地址段决定。

**预引入 API**

```text
Obmm::preimport(info) -> Result<PreimportReservation>
PreimportReservation::numa_id() -> NumaNode
PreimportReservation::unpreimport(self) -> Result<()>
```

`unpreimport` 只用 `pa` 和 `length` 精确匹配。安全类型可以只保留这两个字段用于释放，其他字段作为诊断信息保存。

约束要在文档中写清：

- 预引入地址段不能重叠。
- `obmm_import` 的地址段必须是预引入范围子集。
- 只有未实际上线时才能 `unpreimport`。
- `numa_id = -1` 表示系统分配。

**mmap 与 ownership**

单独设计 `ShmDevice` 和 `MappedRegion`：

```text
ShmDevice::open(mem_id, OpenMode, CacheMode) -> Result<ShmDevice>
ShmDevice::mmap(length, prot, offset, MapGranularity) -> Result<MappedRegion>
MappedRegion::set_ownership(range, Ownership) -> Result<()>
```

`OpenMode`：

```text
ReadOnly
ReadWrite
```

不建议暴露 `WriteOnly`，文档说明当前无法基于 `O_WRONLY` 做映射。

`CacheMode`：

```text
Cacheable
NonCacheableSync
```

`Ownership`：

```text
None
Read
ReadWrite
```

约束：

- `set_ownership` 只允许 cacheable 映射。
- PMD 映射不支持 `set_ownership`。
- `start/end` 必须 page aligned，且 `[start, end)` 落在映射范围内。
- `MappedRegion` 内部保存 fd、base pointer、length、cache mode、granularity，用于本地校验。

PMD 映射：

```text
MapGranularity::Page
MapGranularity::Pmd
```

PMD 模式 offset 需要 OR `OBMM_MMAP_FLAG_HUGETLB_PMD = 1 << 63`。该常量可在 `obmm` 层定义，因为文档明确给出数值，但最好仍在 `sys` 层 bindgen 到时做一致性测试。

**查询 API**

查询是诊断路径，不应鼓励热路径使用：

```text
Obmm::query_memid_by_pa(pa) -> Result<(MemId, u64)>
Obmm::query_pa_by_memid(mem_id, offset) -> Result<u64>
```

支持可选输出的 C 语义在 Rust 中没必要暴露；Rust 直接返回完整结果。

**线程与进程安全**

C 库内部缓存 `/dev/obmm` fd，并用 pthread mutex 初始化，接口可并发调用。但 Rust 类型仍应保守：

- `Obmm` client 可 `Clone + Send + Sync`，因为它无状态。
- `ExportedMemory`、`ImportedMemory` 可 `Send`，是否 `Sync` 取决于是否暴露内部可变状态；建议先不实现手写 `Sync`。
- `MappedRegion` 不应默认 `Sync`，除非提供明确的 typed slice API 和 ownership 协议。
- mmap 后的裸内存访问本质 unsafe；安全 crate 可以只提供 `as_ptr()` / `as_mut_ptr()`，把数据结构解释交给调用方 unsafe 块。

**发布边界**

crate feature 建议：

```text
default = ["dynamic"]
dynamic: link libobmm.so
bindgen: build-time generate bindings
vendored-headers: 仅用于测试 mock，不用于生产
mmap: 启用 ShmDevice/MappedRegion
serde: MemoryDescriptor 序列化
raii-drop: 为 ImportedMemory/ExportedMemory 启用 Drop 清理
```

许可证：

- `libobmm` 是 Mulan PSL v2。
- Rust wrapper 可选择同许可证或双许可证，但 `obmm-sys` 需要在 README 明确运行时链接 `libobmm.so`，且目标系统需安装 OBMM 用户态库和内核模块。

**最小可发布 API 面**

第一版建议只发布这些稳定接口：

```text
export
export_user_addr
unexport
import_mmap
import_numa
import_preimported
unimport
preimport
unpreimport
open shmdev
mmap/munmap
set_ownership
query
```

不要第一版就封装 sysfs、日志、mempool 调优和 vendor adaptor 细节。`vendor_adaptor` 是 `libobmm` 内部实现细节，会读取 `/sys/devices/ub_bus_controller*` 并校验 EID/CNA；Rust 层只需要把 EID、CNA 作为参数传给 C 库，并把 `ENODEV/EINVAL` 原样返回。

**关键安全文档必须写明**

- `MemoryDescriptor` 可以跨机器传输，但其中地址、EID、CNA、token 等只有在对应 OBMM/UB 环境下有效。
- provider 必须先 `export`，consumer 再 `import`；结束时 consumer 先 `unimport`，provider 再 `unexport`。
- `unexport` 前必须保证远端停止访问，否则可能导致进程崩溃、数据不一致、硬件故障或 kernel panic。
- cacheable shared mmap 必须由用户在多 host 间协调 ownership；Rust 类型只能维护本进程调用约束，不能证明跨机器互斥。
- `export_user_addr` 会 pin 目标地址区间，直到 `unexport` 前内核态访问目标内存有严重风险；该 API 应标为 `unsafe` 或要求传入 `PinnedUserRange` 这类显式不变量类型。
- 所有长度、地址、offset 对齐要求必须暴露为文档和本地校验，但最终以内核返回为准。