# obmm/libobmm Rust FFI 发布架构设计

## README

本文为 `obmm/src/libobmm` 的 Rust FFI 发布方案设计。范围限定为静态阅读 `libobmm.h`、`CMakeLists.txt`、`obmm/doc` 与少量必要 C 片段；未编译 C 项目，未安装依赖，未实现代码。

一句话结论：建议发布 `obmm-sys` + `obmm` 双 crate，`obmm-sys` 采用手写 `extern` + 手写常量/布局测试的绑定路径，safe crate 提供以 `MemId` 为核心的 RAII、描述符 builder、设备映射与 ownership guard；async 不进入首版核心 crate。

阅读顺序：

1. `ARCHITECTURE`：库画像、crate 分层、发布目标和风险。
2. `BINDING`：绑定、链接、`build.rs`、ABI 守卫和再生策略。
3. `SAFE-API`：Rust safe API、所有权、错误、`unsafe` 边界和 `Send`/`Sync`。
4. `CHECKLIST`：从调研到发布的执行清单。
5. `ASYNC-ANALYZE`：async 与上层运行时是否需要。

当前状态：可评审、可开工的设计文档；需要在实现前补齐发行环境中的 `<ub/obmm.h>` UAPI 头文件、常量值和 ioctl 结构体布局。

## ARCHITECTURE

### 库画像

`libobmm.so` 是 OBMM 用户态库，配合内核模块 `obmm.ko` 和 `/dev/obmm` 控制设备完成远端内存 export/import/preimport、共享内存字符设备创建、ownership 变更和地址查询。公开 C API 很小，主头文件为 `obmm/src/libobmm/libobmm.h`，版本文件显示当前版本为 `1.0.1`。

公开 API 分为五组：

| 领域 | C API | Rust safe 目标 |
|---|---|---|
| 导出本地内存 | `obmm_export`, `obmm_export_useraddr`, `obmm_unexport` | `ExportedMemory` RAII owner |
| 引入远端内存 | `obmm_import`, `obmm_unimport` | `ImportedMemory` RAII owner |
| 预引入 NUMA | `obmm_preimport`, `obmm_unpreimport` | `PreimportReservation` RAII owner |
| 共享设备一致性 | `obmm_set_ownership` | `MappedObmmMemory`, `OwnershipGuard` |
| 维测查询 | `obmm_query_memid_by_pa`, `obmm_query_pa_by_memid` | `query_*` 调试 API |

API 不以 opaque 指针为主，而是以 `mem_id` 句柄、值结构体、可变长 trailing array 和 POSIX fd/mmap 为核心。资源释放由 `mem_id` 区分：export 得到的 id 必须用 `obmm_unexport`，import 得到的 id 必须用 `obmm_unimport`；preimport 用 `<pa, length>` 精确匹配释放。文档明确要求控制面流程为 provider export、consumer import、访问、consumer unimport、provider unexport。

核心 C 类型：

| C 类型 | 画像 | Rust 暴露策略 |
|---|---|---|
| `typedef uint64_t mem_id` | 0 为无效值 | safe 层用 `NonZeroU64` 包装；`-sys` 暴露 `u64` |
| `struct obmm_mem_desc` | 含 `uint8_t priv[]` flexible array member | `-sys` 手写 header 部分；safe 层用 owned buffer builder |
| `struct obmm_preimport_info` | 含 `uint8_t priv[]`，但 preimport 文档中 priv 被忽略 | 同上，safe 层首版可固定 `priv_len = 0` 或支持 owned buffer |
| fd/mmap pointer | 由 POSIX API 管理 | safe 层用 `OwnedFd`、`Mmap`/自有 mapping 类型封装 |

错误模型是标准 C 风格：返回 `OBMM_INVALID_MEMID` 或 `-1` 表示失败，详细错误在 `errno`。safe crate 统一映射为 `Result<T, ObmmError>`，保留 `errno` 与操作上下文。

并发模型没有公开线程安全保证。`libobmm.c` 内部缓存 `/dev/obmm` fd 并用 `pthread_mutex_t` 保护初始化，但这不能推出每个 `mem_id`、mmap 区间或 ownership 操作天然跨线程安全。safe crate 对资源 owner 采用保守策略：不自动实现 `Sync`；`Send` 仅在字段天然可移动且 Drop 不依赖创建线程时实现，mapping 的可变访问必须受 Rust 借用或显式 guard 约束。

构建模型是 Linux-only 系统库：CMake 构建共享库 `libobmm.so`，安装到 `lib64`，安装头文件 `libobmm.h`。公开头文件依赖 `<ub/obmm.h>`，当前仓库未包含该 UAPI 头；常量如 `OBMM_MAX_LOCAL_NUMA_NODES`、`OBMM_MAX_PRIV_LEN`、`OBMM_EXPORT_FLAG_*`、`OBMM_IMPORT_FLAG_*`、ioctl 结构体和设备常量来自该外部头。发布 Rust 绑定时必须把它作为系统依赖、发行包依赖，或在 `-sys` 中维护最小兼容常量快照。

许可证方面，用户态库源码头和 `obmm/License/LICENSE` 标注 Mulan PSL v2；仓库 README 同时说明内核模块为 GPL-2.0、用户态 `Libobmm` 为 Mulan PSL v2。Rust crate 不应 vendored 内核模块；若 vendored 用户态源码或静态链接 `libobmm`，README 需要明确 Mulan PSL v2 许可证副本与下游声明义务。

### Q1-Q10 决策结论

| 问题 | 结论 | 设计影响 |
|---|---|---|
| Q1 API 类型 | `mem_id` 句柄 + 值结构体 + fd/mmap；无公开 callback/ops 表 | safe 层围绕 RAII owner 和 typed descriptor builder |
| Q2 宏/inline/union/bitfield | 公开 `libobmm.h` 无 inline/union/bitfield，但依赖缺失的 `<ub/obmm.h>` 宏和 ioctl 类型 | 不选构建时 bindgen；首版手写公开 API，常量需 ABI 检查 |
| Q3 资源模型 | export/unexport、import/unimport、preimport/unpreimport 成对；mmap/open/close 由 POSIX 管 | `Drop` 默认释放；提供 `into_raw_mem_id`/`from_raw_*` 处理所有权转移 |
| Q4 错误模型 | invalid id 或 -1 + `errno` | `ObmmError { operation, errno }`，可选分类 enum |
| Q5 构建模型 | 系统 `libobmm.so` + 外部 UAPI 头 + Linux 设备 | `build.rs` 用 pkg-config/环境变量探测，vendored 默认关闭 |
| Q6 CI 限制 | 需要 Linux、内核模块、设备、硬件/UB 环境；普通 CI 无法跑真实流程 | 分离 compile/layout tests 与外部环境 integration tests |
| Q7 发布 | 面向 crates.io 应拆 `obmm-sys` + `obmm` | `-sys` semver 跟 C ABI，safe crate 独立演进 |
| Q8 许可证 | 用户态 Mulan PSL v2，内核 GPL-2.0 不进入 crate | 不 vendored 内核；vendored 用户态需许可证说明 |
| Q9 上层协议 | 首版不需要协议 crate；OBMM 是内存管理控制面 | safe crate 已足够，后续可另建 NUMA/mmap 工具 crate |
| Q10 async | C API 同步、无 fd reactor API；数据面是 load/store | async 标记为 N/A；阻塞控制面由调用方线程池处理 |

### Crate 与 workspace 划分

建议 workspace：

```text
obmm-rs/
  obmm-sys/   # 原始 FFI、链接、常量、layout tests
  obmm/       # safe RAII、descriptor builder、mmap/ownership
```

`obmm-sys` 职责：

- 暴露 `extern "C"` 函数、`mem_id`、`obmm_mem_desc` header、`obmm_preimport_info` header、flag 常量。
- 只做链接与 ABI/layout 检查，不承诺 Rust safe 语义。
- `links = "obmm"`，避免同一进程中多 crate 重复链接不同 `libobmm`。
- 提供 `system` 默认路径；`vendored` 仅作为可选后续功能，不作为首版默认。

`obmm` 职责：

- 提供 `ExportedMemory`、`ImportedMemory`、`PreimportReservation`、`MemDevice`、`MappedObmmMemory`、`OwnershipGuard`。
- 统一错误处理、输入校验、RAII Drop、descriptor owned buffer。
- 封装 `open`/`mmap`/`munmap`/`close` 以及 ownership 转换。
- 不暴露 ioctl 结构体，不让用户手写 flexible array 内存布局。

不建议首版三层 workspace。当前 API 面小，业务协议和 runtime 依赖不明显；把第三层提前拆出会增加 semver 和文档成本。后续若出现 NUMA placement 策略、跨主机描述符交换、控制面服务或异步调度框架，再新增 `obmm-control` 或 `obmm-async`。

### 发布目标与 semver

`obmm-sys` 版本建议跟随 `libobmm` 主版本，crate 版本可从 `0.1.0` 起步并在 README 声明支持的 native ABI：`libobmm.so.1` / OBMM `1.0.1` 及兼容版本。`obmm` safe crate 以 Rust API semver 为准；新增 C 常量通常是 minor，改变资源 Drop 语义或 safe 类型不变量是 breaking change。

### 架构决策摘要

| 决策 | 选择 | 理由 |
|---|---|---|
| 分层 | `obmm-sys` + `obmm` | 对外发布需要隔离 raw FFI 与 safe RAII，避免下游直接误用 `mem_id` |
| 绑定 | 手写 `extern` + 手写类型/常量快照 + ABI 测试 | C API 小；缺失 `<ub/obmm.h>` 使 bindgen 不可稳定作为用户构建依赖 |
| 链接 | 默认系统动态链接 `libobmm` | native 库依赖内核模块和发行版环境，vendored 无法解决运行环境 |
| safe 边界 | RAII owner + builder + explicit unsafe escape hatch | 释放顺序和远端使用者状态有强外部约束，必须在类型和文档中表达 |
| async | 首版不提供 | 控制面同步，数据面不是 socket/fd readiness 模型 |

### 主要风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| `<ub/obmm.h>` 不在仓库 | 常量、ioctl layout、flag 值无法从当前源码完整确认 | 实现前要求提供目标发行版 UAPI 头；`obmm-sys` 加 layout/constant tests |
| flexible array member | Rust 不能直接安全构造 `struct obmm_mem_desc` 的完整对象 | safe 层使用 `Vec<u8>` owned allocation，header + priv 连续布局 |
| export/useraddr 外部安全约束强 | pin 用户地址、内核访问可能 panic、远端未停用时 unexport 后果严重 | 对应 API 标记 `unsafe` 或要求高层类型用显式 `unsafe` constructor |
| 无普通 CI 运行环境 | 真实 import/export 需要内核模块、设备、硬件/UB 环境 | compile/layout tests 常规跑，integration tests 标记外部环境 |
| ownership 一致性跨 host | Rust 类型无法单机证明全局互斥写者 | API 文档要求调用方协议保证，guard 只管理本进程权限切换 |

## BINDING

### 绑定路径选择

选择：`obmm-sys` 手写 `extern "C"` 绑定公开函数，手写最小 `#[repr(C)]` header 类型，手写或从 UAPI 头生成常量快照，并用 ABI/layout 测试守卫。

理由：

- 公开函数只有 9 个，人工维护成本低。
- `libobmm.h` 公开结构体简单，但包含 flexible array member，bindgen 生成结果也不能直接成为 safe 构造接口。
- 公开头依赖 `<ub/obmm.h>`，该文件当前不在仓库。构建时 bindgen 会把 libclang 和系统 UAPI 头变成所有用户的构建依赖，不适合作为默认 crates.io 路径。
- `libobmm` 依赖运行时 `/dev/obmm`、内核模块和硬件环境；vendored 绑定生成不能解决运行时可用性。

不选路径：

- 不选构建时 `bindgen` 作为默认：用户需要 libclang、目标发行版 UAPI 头和一致的 include path，docs.rs 也难稳定通过。
- 不选预生成完整 bindgen：API 面太小，生成大量 ioctl 内部类型会扩大 `-sys` semver 面；缺失 UAPI 头时也无法可靠再生。
- 不选 `wrapper.c`：公开 API 没有需要 C 编译器解释的 inline/bitfield；增加 C shim 只会增加构建复杂度。
- 不选 mummy/static bridge：当前难点不是链接期缺系统 dev 包，而是运行时内核模块和硬件环境；mummy 对首版价值有限。

### `obmm-sys` 原始 API 面

`obmm-sys` 暴露：

```rust
pub type mem_id = u64;

pub const OBMM_INVALID_MEMID: mem_id = 0;
pub const MAX_NUMA_NODES: usize = 16;

#[repr(C)]
pub struct obmm_mem_desc {
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
pub struct obmm_preimport_info {
    pub pa: u64,
    pub length: u64,
    pub base_dist: i32,
    pub numa_id: i32,
    pub seid: [u8; 16],
    pub deid: [u8; 16],
    pub scna: u32,
    pub dcna: u32,
    pub priv_len: u16,
}
```

注意：上述 Rust struct 只表示 flexible array member 之前的 header。传给 C 时必须指向一段连续 allocation，header 后紧跟 `priv_len` 字节。`-sys` 应把构造完整对象留给 safe crate 或提供明确 `unsafe` helper。

函数绑定：

```rust
unsafe extern "C" {
    pub fn obmm_export(
        length: *const usize,
        flags: c_ulong,
        desc: *mut obmm_mem_desc,
    ) -> mem_id;

    pub fn obmm_export_useraddr(
        pid: c_int,
        va: *mut c_void,
        length: usize,
        flags: c_ulong,
        desc: *mut obmm_mem_desc,
    ) -> mem_id;

    pub fn obmm_unexport(id: mem_id, flags: c_ulong) -> c_int;
    pub fn obmm_import(
        desc: *const obmm_mem_desc,
        flags: c_ulong,
        base_dist: c_int,
        numa: *mut c_int,
    ) -> mem_id;
    pub fn obmm_unimport(id: mem_id, flags: c_ulong) -> c_int;
    pub fn obmm_preimport(info: *mut obmm_preimport_info, flags: c_ulong) -> c_int;
    pub fn obmm_unpreimport(info: *const obmm_preimport_info, flags: c_ulong) -> c_int;
    pub fn obmm_set_ownership(fd: c_int, start: *mut c_void, end: *mut c_void, prot: c_int) -> c_int;
    pub fn obmm_query_memid_by_pa(pa: c_ulong, id: *mut mem_id, offset: *mut c_ulong) -> c_int;
    pub fn obmm_query_pa_by_memid(id: mem_id, offset: c_ulong, pa: *mut c_ulong) -> c_int;
}
```

常量策略：

- `OBMM_MAX_LOCAL_NUMA_NODES`：不要假设等于 `MAX_NUMA_NODES`，实现时从目标 `<ub/obmm.h>` 确认。safe API 中 `ExportLengths` 应以该常量为长度。
- `OBMM_MAX_PRIV_LEN`：文档当前为 512；实现时通过常量检查确认。
- flags：`OBMM_EXPORT_FLAG_FAST`、`OBMM_EXPORT_FLAG_ALLOW_MMAP`、`OBMM_IMPORT_FLAG_NUMA_REMOTE`、`OBMM_IMPORT_FLAG_ALLOW_MMAP`、`OBMM_IMPORT_FLAG_PREIMPORT`、`OBMM_MMAP_FLAG_HUGETLB_PMD` 必须来自 UAPI 头或经测试确认。
- POSIX constants：`PROT_NONE`、`PROT_READ`、`PROT_WRITE`、`O_SYNC`、`MAP_SHARED` 来自 `libc`。

### `build.rs` 与链接

`obmm-sys` `Cargo.toml`：

```toml
[package]
links = "obmm"

[features]
default = ["system"]
system = []
vendored = []
bindgen = []
```

首版建议只实现 `system`，保留 `vendored` feature 名称但不发布未完成路径，或不暴露该 feature。

探测顺序：

1. `OBMM_LIB_DIR` 和 `OBMM_INCLUDE_DIR` 环境变量：显式用户覆盖。
2. `pkg-config --libs --cflags obmm`：如果发行版提供 `obmm.pc`。
3. 常见系统路径：`/usr/lib64`, `/usr/lib`, `/usr/local/lib64`, `/usr/local/lib`，并只输出 `cargo:rustc-link-lib=obmm`。
4. docs.rs：不要求真实 native lib，使用 `#[cfg(docsrs)]` 跳过 link 探测，仅生成文档；所有会链接 native 符号的 doctest 禁用。

`build.rs` 输出：

```text
cargo:rustc-link-lib=obmm
cargo:rustc-link-search=native=<dir>   # 仅当明确找到或用户指定
cargo:rerun-if-env-changed=OBMM_LIB_DIR
cargo:rerun-if-env-changed=OBMM_INCLUDE_DIR
```

若缺少系统库：

- 普通构建应失败并给出安装 `libobmm`/开发包和设置 `OBMM_LIB_DIR` 的提示。
- `cargo check --features docsrs` 或 docs.rs 构建可降级，但不能声明 runtime 可用。

### ABI 与再生流程

即使手写绑定，也需要再生/校验机制：

- 增加 `tests/abi.rs` 或 `ctest2`/`systest`：检查 `sizeof`, `alignof`, field offsets、常量值、函数签名可链接。
- 增加 `tests/constants.rs`：由小 C 程序打印或编译期断言 UAPI 常量；CI 中在有开发包环境下运行。
- 上游版本升级流程：更新支持矩阵、运行 header diff、更新常量快照、运行 layout tests、更新 README。
- 不把 ioctl 内部结构体作为公开 Rust API；仅在 `obmm-sys` 测试中使用它们确认 native header 版本。

### CI 策略

常规 CI：

- Linux x86_64/aarch64 `cargo check`、`cargo test --no-run`。
- `cargo test -p obmm-sys --features abi-tests` 仅在安装 native dev 包的 job 运行。
- docs.rs 配置 `DOCS_RS=1`，跳过 native link 探测。

外部环境 CI：

- 需要 openEuler/目标内核、`obmm.ko`、`/dev/obmm`、UB/CXL 或可替代测试环境。
- 标记 `OBMM_RUN_INTEGRATION=1` 才运行真实 export/import/preimport/mmap 流程。
- 真实测试结束必须检查 `/dev/obmm_shmdev{id}` 和 sysfs 状态已清理。

## SAFE-API

### 模块树

```text
obmm
  error        # ObmmError, Result
  desc         # MemDesc, MemDescBuilder, PreimportInfoBuilder, Eid
  flags        # ExportFlags, ImportFlags, MmapMode, Protection
  memory       # ExportedMemory, ImportedMemory, MemId
  preimport    # PreimportReservation
  device       # MemDevice, MappedObmmMemory, OwnershipGuard
  query        # query_memid_by_pa, query_pa_by_memid
  raw          # controlled re-export of obmm_sys
```

### 核心类型

| Rust 类型 | C 对应 | 所有权 |
|---|---|---|
| `MemId(NonZeroU64)` | `mem_id` | 非 0 id，避免把 invalid id 放进 safe owner |
| `Eid([u8; 16])` | `uint8_t eid[16]` | 值类型，little-endian 语义写入文档 |
| `MemDesc` | `struct obmm_mem_desc + priv[]` | owned contiguous buffer，可安全传给 C |
| `PreimportInfo` | `struct obmm_preimport_info + priv[]` | owned contiguous buffer，首版可限制 `priv_len = 0` |
| `ExportedMemory` | `mem_id` from export | `Drop` 调 `obmm_unexport(id, 0)` |
| `ImportedMemory` | `mem_id` from import | `Drop` 调 `obmm_unimport(id, 0)` |
| `PreimportReservation` | `<pa, length>` | `Drop` 调 `obmm_unpreimport` |
| `MemDevice` | `/dev/obmm_shmdev{id}` fd | owns `OwnedFd` |
| `MappedObmmMemory` | `mmap` range | owns mapping; Drop `munmap` |
| `OwnershipGuard` | ownership state range | Drop 可恢复 `PROT_NONE`，但需显式处理失败策略 |

### Descriptor builder

`MemDescBuilder` 负责构造 flexible array member：

- 校验 `priv.len() <= OBMM_MAX_PRIV_LEN`。
- allocation 大小为 `size_of::<obmm_mem_desc>() + priv.len()`，并按 C header alignment 对齐。
- 对 export：只允许设置 `deid`、`priv`；其他出参字段初始化为 0。
- 对 import：设置 `addr`、`length`、`seid`、`deid`、`scna`、`dcna`、`priv`。
- C 调用返回后，从同一 buffer 读取出参形成 immutable `MemDesc`。

`ExportLengths` 应是固定长度数组包装：

```rust
pub struct ExportLengths([usize; OBMM_MAX_LOCAL_NUMA_NODES]);
```

提供 `set_numa(node, bytes)`，并做非零总长度、对齐辅助检查。对齐值依赖内核配置，safe 层只做已知常量的基础检查；无法确定时让 C 返回 `EINVAL`。

### RAII 与 Drop

`ExportedMemory::export(lengths, ExportOptions, ExportDescriptorInput) -> Result<ExportedMemory>`：

- 成功时持有 `MemId` 和返回 `MemDesc`。
- Drop 调用 `obmm_unexport(id, 0)`。
- `try_unexport(self) -> Result<()>` 提供可观察错误的显式释放。
- Drop 中错误不能 panic；建议记录到 `tracing` 或忽略，并在文档中要求生产代码使用显式释放。

`ImportedMemory::import(desc, ImportOptions, NumaPlacement) -> Result<ImportedMemory>`：

- `ImportOptions` 类型层面保证 `AllowMmap` 与 `NumaRemote` 必选且互斥。
- `Preimport` 只能与 `NumaRemote` 组合。
- 成功时保存可选 `numa_id`。

`PreimportReservation::preimport(info) -> Result<PreimportReservation>`：

- 保存 `pa`、`length` 和返回的 `numa_id`。
- Drop 只在未被明确 `disarm` 时调用 `obmm_unpreimport`。
- 文档说明：若该预引入区段已实际上线，unpreimport 可能返回 `EBUSY`；建议用户先 unimport。

`export_useraddr`：

- 建议 safe crate 暴露为 `unsafe fn export_useraddr(...)`。
- 原因：调用方必须保证虚拟地址、长度、映射粒度、权限、生命周期、内核态访问约束和 pin 住期间不违反 C 文档。Rust 无法从类型系统证明这些条件。

### mmap 与 ownership

`MemDevice::open(mem_id, AccessMode, CacheMode) -> Result<MemDevice>`：

- 构造 `/dev/obmm_shmdev{id}`。
- `CacheMode::Cacheable` 不带 `O_SYNC`，允许 ownership 变更。
- `CacheMode::NonCacheable` 带 `O_SYNC`，禁止 `set_ownership`。

`MappedObmmMemory::map(device, length, prot, MapGranularity) -> Result<Self>`：

- 使用 `MAP_SHARED`。
- `MapGranularity::Page` offset 为 page-aligned。
- `MapGranularity::Pmd` 使用 `OBMM_MMAP_FLAG_HUGETLB_PMD | offset`，并在类型中标记不支持 ownership。

`OwnershipGuard`：

- `mapped.set_ownership(range, Protection::Read/Write/None) -> Result<OwnershipGuard>`。
- guard 可在 Drop 时尝试恢复 `PROT_NONE`，但 Drop 失败不可返回；因此应提供 `release(self) -> Result<()>`。
- 不承诺跨 host 互斥；只表达本进程对当前 fd/mapping 的权限切换。

### 错误类型

```rust
pub struct ObmmError {
    pub operation: &'static str,
    pub errno: std::io::Error,
}
```

提供分类 helper：

- `is_invalid_argument()`
- `is_busy()`
- `is_no_device()`
- `is_no_space()`
- `is_not_found()`

不要把文档列出的所有 errno 固化为 exhaustive enum。上游内核和 vendor adaptor 可能返回新 errno；Rust API 应保留原始 `errno`。

### `unsafe` 边界与 Safety 契约

| 入口 | safe/unsafe | 契约 |
|---|---|---|
| `obmm::raw::*` | unsafe | 与 C API 完全一致，调用方保证指针、buffer、flags、生命周期有效 |
| `MemDesc::from_raw_parts` | unsafe | header 指针后必须有 `priv_len` 字节连续有效内存 |
| `ExportedMemory::from_raw_exported` | unsafe | id 必须来自成功 `obmm_export`/`obmm_export_useraddr`，且当前 Rust owner 独占释放责任 |
| `ImportedMemory::from_raw_imported` | unsafe | id 必须来自成功 `obmm_import`，且当前 Rust owner 独占释放责任 |
| `export_useraddr` | unsafe | va/length 对齐且映射满足 PMD/hugetlb/THP、读写权限、生命周期和内核访问约束 |
| `MappedObmmMemory::as_slice` | unsafe 或受 prot 限制 | 只有在当前权限允许读时才能创建 shared slice，写 slice 必须独占且权限为 write |
| `MappedObmmMemory::as_mut_ptr` | safe 返回 raw ptr | 解引用由调用方负责 |

### `Send`/`Sync` 策略

- `MemId`、`Eid`、descriptor value 类型可以 `Send + Sync`。
- `ExportedMemory`、`ImportedMemory` 可以考虑 `Send`，但默认不实现 `Sync`。释放操作作用于全局内核状态，且文档没有多线程并发保证。
- `MemDevice` 基于 `OwnedFd`，可 `Send`；是否 `Sync` 取决于是否允许同一 fd 并发 ownership ioctl。首版不实现 `Sync`。
- `MappedObmmMemory` 不实现 `Sync`；可在 `Send` 上保守，除非 API 明确禁止跨线程移动。
- `OwnershipGuard` 不实现 `Send`/`Sync`，除非设计能保证 Drop 在任意线程恢复权限是正确的。

### 暂不封装区域

首版不封装：

- vendor adaptor 内部接口。
- `/sys/devices/obmm` 和 `/proc/obmm/preimport_info` 维测文件的结构化解析。
- 自动 NUMA placement 策略。
- 跨主机 descriptor 交换协议。
- 日志 syslog 配置；只在文档中说明 native 库会记录 syslog。

## CHECKLIST

### 调研

- [x] 确认 C 项目路径：`obmm/src/libobmm`。
- [x] 确认主头文件：`libobmm.h`。
- [x] 确认版本：`obmm/VERSION` 为 `1.0.1`。
- [x] 静态阅读 CMake、README、`obmm/doc/libobmm.md` 和各 API 文档。
- [x] 记录外部 UAPI 头 `<ub/obmm.h>` 当前仓库缺失。
- [x] 记录用户态 Mulan PSL v2、内核态 GPL-2.0 的许可证边界。

### 架构

- [ ] 创建 workspace：`obmm-sys`、`obmm`。
- [ ] `obmm-sys` 设置 `links = "obmm"`。
- [ ] `obmm-sys` README 写明支持 native `libobmm` 版本范围。
- [ ] `obmm` README 写明需要内核模块、`/dev/obmm`、硬件/目标内核环境。
- [ ] 明确 safe crate 不保证跨 host ownership 协议，只封装本机 API。

### 绑定与构建

- [ ] 从目标发行版获取 `<ub/obmm.h>`。
- [ ] 确认 `OBMM_MAX_LOCAL_NUMA_NODES`、`OBMM_MAX_PRIV_LEN`、flag、PMD mmap flag 的值。
- [ ] 手写 9 个 `extern "C"` 函数。
- [ ] 手写 flexible array header 类型，避免把 trailing array 错误建模为固定数组。
- [ ] 实现 `build.rs` 环境变量覆盖。
- [ ] 支持 pkg-config 探测；若上游没有 `obmm.pc`，在 README 给出替代环境变量。
- [ ] docs.rs 路径跳过 native link 探测。
- [ ] 增加 ABI/layout/constant 测试。
- [ ] 增加上游版本升级时的 header diff 和测试流程。

### Safe API

- [ ] 实现 `MemId(NonZeroU64)`。
- [ ] 实现 `Eid([u8; 16])`。
- [ ] 实现 `MemDescBuilder` 和 contiguous allocation。
- [ ] 实现 export/import/preimport RAII owner。
- [ ] 为 `Drop` 失败设计文档策略，并提供显式 `try_*` 释放方法。
- [ ] 用类型表达 import flags 的互斥和依赖关系。
- [ ] 将 `export_useraddr` 设计为 `unsafe fn` 并写完整 `# Safety`。
- [ ] 封装 `/dev/obmm_shmdev{id}` open/mmap/munmap/close。
- [ ] 实现 ownership guard，并提供显式恢复方法。
- [ ] 对所有公开 unsafe API 写 `# Safety`。
- [ ] 对 `Send`/`Sync` 保守实现并在文档列出依据。

### 测试

- [ ] 无设备环境：`cargo check`、unit tests、descriptor buffer layout tests。
- [ ] 有 native dev 包环境：ABI/layout/constant tests。
- [ ] 有 `/dev/obmm` 环境：query 错误路径、invalid id、flags 参数错误 smoke tests。
- [ ] 有完整硬件/内核环境：export/unexport、import/unimport、preimport/unpreimport、mmap ownership 流程。
- [ ] Drop 清理测试必须检查资源释放，避免残留 `/dev/obmm_shmdev{id}`。
- [ ] 对 `unsafe` 不变量增加 doctest 或 compile-fail 测试。

### CI 与发布

- [ ] Linux-only cfg；非 Linux 构建给出清晰错误。
- [ ] docs.rs metadata 设置。
- [ ] README 包含安装 native library、设置环境变量、运行 integration tests 的说明。
- [ ] LICENSE 文件包含 Rust crate 许可证和 native 依赖许可证说明。
- [ ] 发布前检查 crate package 不包含内核模块源码或不必要 vendored 文件。
- [ ] 发布前在目标系统执行 package contents 检查和 smoke test。

## ASYNC-ANALYZE

结论：首版 `obmm-sys` 和 `obmm` 不提供 async API，也不引入 Tokio、async-std 或 reactor 依赖。

理由：

- `libobmm` 公开 API 是同步控制面调用，失败通过 `errno` 返回，没有公开 pollable completion fd、callback 或 async request handle。
- 数据面访问是应用对 mmap 或 NUMA 远端内存执行普通 load/store，不是 socket/readiness 模型。
- `obmm_set_ownership` 是同步 ioctl；把它包装成 async 只会把阻塞移到 runtime worker，不能提供真正的 readiness 语义。
- export/import/preimport 是低频控制面操作，调用方可以按需使用 `spawn_blocking` 或自己的线程池。

首版 safe crate 应提供的是阻塞 API，并在文档中说明：

- 如果在 async 服务中调用 export/import/preimport/unimport/unexport，使用运行时提供的 blocking pool。
- mmap 后的数据访问不应在 async API 层伪装为 `AsyncRead`/`AsyncWrite`，因为远端内存一致性由 ownership 协议而非字节流 backpressure 控制。
- ownership guard 可能触发 cache flush/invalidate 和 ioctl，调用方应避免在 latency-sensitive async executor core thread 上直接调用。

后续可以新增独立 crate，但不纳入首版：

| 可能 crate | 触发条件 | 依赖位置 |
|---|---|---|
| `obmm-tokio` | 出现控制面服务、descriptor 交换、批量异步 orchestration | 依赖 `obmm`，不依赖 `obmm-sys` internals |
| `obmm-control` | 需要跨主机发布/接收 `MemDesc`、NUMA policy、生命周期协议 | 依赖 `obmm`，可选择 async runtime |
| `obmm-mmap-io` | 需要把 shared memory 抽象成高层 buffer pool | 依赖 `obmm`，但不承诺通用 `AsyncRead/Write` |

因此，当前 async 设计为 N/A；真正需要上层异步时，应作为 safe crate 之上的运行时适配层，而不是进入 `-sys` 或核心 RAII crate。
