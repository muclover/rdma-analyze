已按指定 `rust-safe-ffi-design` skill 做静态设计；未编译 C 项目、未安装依赖、未实现代码。

**README.md 正文**

本方案为 `obmm/src/libobmm` 设计可发布到 Rust 生态的 FFI 架构。结论：采用 `libobmm-sys` + `obmm` 双 crate；`-sys` 负责 `libobmm.so` 链接、原始 ABI、常量和布局校验，safe crate 负责 `mem_id` 资源 RAII、`errno` 错误映射、flexible array 描述符构造、设备映射与 ownership API。

阅读顺序：

1. `ARCHITECTURE.md`：库画像、crate 划分和发布策略。
2. `BINDING.md`：绑定生成、链接、`build.rs`、ABI 守卫。
3. `SAFE-API.md`：safe API 类型、所有权、错误和 `unsafe` 边界。
4. `ASYNC-ANALYZE.md`：async 需求判断。
5. `CHECKLIST.md`：从调研到发布的执行清单。

当前状态：设计阶段。静态阅读范围包括 `obmm/src/libobmm/libobmm.h`、`vendor_adaptor.h`、`CMakeLists.txt`、`libobmm.c` 的必要 ABI 片段，以及 `obmm/doc` 中 libobmm、export/import/preimport/shmdev/ownership/query 文档。

**ARCHITECTURE.md 正文**

库画像：

`libobmm` 是 OBMM 用户态库，暴露 `libobmm.so`，通过 `/dev/obmm` ioctl 与内核模块 `obmm.ko` 交互。公共 API 很小，核心函数族为 export/unexport、import/unimport、preimport/unpreimport、ownership 设置和调试查询。资源句柄是 `typedef uint64_t mem_id`，`OBMM_INVALID_MEMID == 0` 表示失败或无效 ID。

API 以值结构体和句柄为主，没有 opaque 指针、ops 表、回调或异步完成队列。主要 ABI 难点是：

- `struct obmm_mem_desc` 和 `struct obmm_preimport_info` 使用 flexible array member `priv[]`。
- `libobmm.h` 依赖外部 `<ub/obmm.h>`，其中包含 flags、ioctl 命令、`OBMM_MAX_LOCAL_NUMA_NODES`、`OBMM_MAX_PRIV_LEN` 等常量。
- CMake 只构建共享库 `libobmm.so`，安装 `libobmm.h` 到 include，未看到 pkg-config 文件。
- 运行依赖特定内核模块、硬件/平台、`/dev/obmm`、`/dev/obmm_shmdev{id}` 和 sysfs/procfs。

Crate 分层：

采用 workspace：

- `libobmm-sys`：发布原始 FFI。包含 `extern "C"` 函数、`repr(C)` 类型、常量、linking/build.rs、ABI/layout 测试。公开 raw API，不提供所有权语义。
- `obmm`：safe crate。封装 `ExportedMemory`、`ImportedMemory`、`PreimportedRange`、`MemoryDescriptor`、`MappedMemory`、`Ownership`、`Error` 等类型。
- 暂不新增第三层协议 crate。OBMM 的“协议”主要是远端内存生命周期、NUMA 上线和 ownership 协调，不是一个独立 wire protocol；可先放在 safe crate 的 builder/API 层。

发布目标：

按 crates.io 发布设计。`libobmm-sys` 使用 `links = "obmm"`，避免多个 crate 重复链接不同版本 `libobmm.so`。`obmm` 依赖同版本范围的 `libobmm-sys`。版本策略建议：

- `libobmm-sys` 的 minor/patch 跟随上游 libobmm API 兼容更新。
- C ABI 破坏性变化时提升 Rust major。
- safe crate 可在不破坏 raw ABI 的情况下增加更高层 builder，但不得改变已有 RAII 释放语义。

许可证：

用户态库声明为 Mulan PSL v2；内核态驱动为 GPL-2.0。Rust crate 若只动态链接系统 `libobmm.so`，crate 自身可选 Apache-2.0/MIT/Mulan PSL v2，但 README 必须说明运行依赖的用户态库许可证和内核模块许可证。默认不 vendored C 源码，避免把内核/平台依赖和许可证边界混入 Rust crate。

核心决策摘要：

| 主题 | 决策 | 理由 |
|---|---|---|
| 分层 | `libobmm-sys` + `obmm` | raw ABI 与 safe 生命周期分离，适合发布 |
| 绑定 | 小 API 手写 extern + 预生成/校验常量布局 | 函数少，手写更稳定；外部 UAPI 常量需校验 |
| 链接 | 默认 system shared lib，`links = "obmm"` | CMake 产物是 `libobmm.so`，运行需真实平台 |
| vendored | 非默认，先不支持 | 依赖外部 `<ub/obmm.h>`、内核模块和硬件环境 |
| safe 所有权 | `mem_id` RAII 类型区分 export/import/preimport | `unexport` 与 `unimport` 不可混用 |
| async | N/A | C API 是同步控制面，数据访问走 mmap/load/store |

主要风险：

- 仓库中未包含 `<ub/obmm.h>`，发布前必须确认 dev 包安装路径或随 libobmm 安装该 UAPI 头。
- `OBMM_MAX_LOCAL_NUMA_NODES` 来自外部头，不能在 Rust 中硬编码为 `MAX_NUMA_NODES`，除非 ABI 测试确认一致。
- `obmm_export_useraddr` 有高风险语义：pin 用户地址，且文档说明 unexport 前内核态访问目标内存可能导致宿主机 panic。safe API 必须把它设计成显式 `unsafe` 或放在独立 `unsafe` builder 中。
- `obmm_unexport` 要求用户保证远端使用者已停止访问；Rust RAII 只能释放本机资源，不能证明跨 host 协议安全。

**BINDING.md 正文**

绑定路径：

选择“手写 public extern + ABI/layout 守卫 + 可选 bindgen 再生辅助”。

原因：

- 公共函数只有 9 个，函数签名稳定，手写 `extern "C"` 可读性和审查成本优于整头 bindgen。
- 结构体只有两个 public flexible array member 类型，bindgen 会生成不适合 safe 层直接消费的 `__IncompleteArrayField`；safe 层需要自建 owned buffer。
- 仍需要 bindgen 或 C ABI 测试捕获 `<ub/obmm.h>` 中常量、`size_t`/`unsigned long` 宽度、结构体 offset、flags 值和 ioctl 相关 ABI 变化。

排除路径：

- 不采用构建时 bindgen 作为默认路径：docs.rs 和用户构建会额外依赖 libclang、外部 UAPI 头、目标系统 dev 包，发布体验差。
- 不采用 `wrapper.c` 作为主路径：头文件没有大量 `static inline`、bitfield 或必须由 C 编译器解释的宏 API。
- 不采用 mummy：当前问题不是“无 dev 包仍需链接 inline 符号”，而是真实运行需要内核模块和硬件环境；mummy 不能替代运行环境。

`libobmm-sys` 内容：

- `type mem_id = u64` 或更 Rust 化地导出 `pub type obmm_mem_id = u64`，同时保留 C 命名别名。
- `#[repr(C)]` 的 raw header 部分：`obmm_mem_desc` 不应直接包含 Rust slice；raw 层可表达 fixed header，并提供 `priv` 起始 offset 常量。
- `obmm_preimport_info` 同理。
- `extern "C"`：`obmm_export`、`obmm_export_useraddr`、`obmm_unexport`、`obmm_import`、`obmm_unimport`、`obmm_preimport`、`obmm_unpreimport`、`obmm_set_ownership`、`obmm_query_*`。
- flags/常量从生成文件或人工维护文件导出，并用 ABI 测试验证。

`build.rs` 探测顺序：

1. 环境变量 override：`LIBOBMM_LIB_DIR`、`LIBOBMM_INCLUDE_DIR`、`LIBOBMM_STATIC`。
2. `pkg-config obmm`，如果上游未来提供 `.pc`。
3. 常见系统路径：`/usr/lib64`、`/usr/lib`、`/usr/local/lib64`、`/usr/local/lib`，include 对应 `/usr/include`、`/usr/local/include`。
4. docs.rs：不尝试链接真实系统库，只生成文档 cfg，raw extern 保留但跳过 link probe。
5. vendored feature：暂不默认设计；只有上游明确提供完整用户态源码、UAPI 头和许可确认后再加。

链接策略：

- `links = "obmm"`。
- 默认动态链接：`cargo:rustc-link-lib=dylib=obmm`。
- 静态链接只允许用户显式启用，并在文档中提示 Mulan PSL v2 分发义务和目标系统 ABI 风险。
- 不在 safe crate 直接链接 C 库，所有链接集中在 `libobmm-sys`。

绑定再生与 ABI 守卫：

- 维护一个 `ffi/obmm_abi.h` 只 include `<libobmm.h>` 和必要系统头。
- CI 中有“有 dev 包”作业运行 `bindgen --allowlist-function obmm_.* --allowlist-type obmm_.* --allowlist-var OBMM_.*` 生成临时文件并 diff 常量/签名。
- 用 `ctest2` 或等价 C 小程序校验 `sizeof`、`alignof`、字段 offset、常量值。
- 无硬件 CI 只做编译和 ABI/layout；真实 ioctl 成功路径放到外部硬件集成测试。

**SAFE-API.md 正文**

模块树建议：

- `obmm::desc`：`MemoryDescriptor`、`PreimportInfo`、`Eid`、`PrivateData`。
- `obmm::export`：`ExportBuilder`、`ExportedMemory`、`ExportedUserAddrMemory`。
- `obmm::import`：`ImportBuilder`、`ImportedMemory`、`PreimportedRange`。
- `obmm::device`：`MemoryDevice`、`MappedMemory`、`MapOptions`。
- `obmm::ownership`：`Ownership::{None, Read, ReadWrite}`。
- `obmm::query`：地址查询。
- `obmm::error`：`Error`、`Errno`、`Result<T>`。

核心类型：

- `MemId(NonZeroU64)`：禁止构造 0。
- `ExportedMemory`：持有 `MemId`，`Drop` 调用 `obmm_unexport(id, 0)`。
- `ImportedMemory`：持有 `MemId`，`Drop` 调用 `obmm_unimport(id, 0)`。
- `PreimportedRange`：持有 `pa`、`length`，`Drop` 调用 `obmm_unpreimport`。必须记录精确匹配字段，因为 C API 要求 pa/length 精确匹配。
- `MemoryDescriptor`：owned buffer，内部布局为 fixed header + `priv` bytes，避免用户手写 flexible array。
- `MemoryDevice`：打开 `/dev/obmm_shmdev{id}` 的 fd owner。
- `MappedMemory`：mmap 后的 owner，`Drop` 先 `munmap` 再释放 fd owner 引用。

RAII 规则：

- `ExportedMemory` 与 `ImportedMemory` 类型分开，防止把 import id 传给 `unexport` 或反向传错。
- Drop 失败不能 panic；记录错误的策略应是 `close()`/`unexport()` 显式方法返回 `Result`，Drop 仅 best-effort。
- 设备映射存在时不允许销毁内存设备。safe 层通过所有权关系让 `MappedMemory` 借用或持有 `Arc<MemoryDevice>`，并让 `MemoryDevice` 关联父 `ExportedMemory`/`ImportedMemory`，保证 drop 顺序为 mapping -> fd -> memid。
- `obmm_export_useraddr` 设计为 `unsafe fn export_user_addr(...)`，因为 Rust 无法证明目标地址 PMD 对齐、映射粒度、权限、pin 期间内核访问风险和跨进程生命周期。

错误模型：

- C API 失败统一读取 `errno`。
- 返回 `mem_id` 的函数：`0` 映射为 `Err(Error::last_os_error(Operation::Export/Import/...))`。
- 返回 `int` 的函数：`-1` 映射为 `Err`，`0` 为 `Ok(())`。
- 错误类型保留原始 `errno`，同时携带操作名和关键上下文，如 `mem_id`、`pa/length`、flags。

`Send`/`Sync` 策略：

- `MemId` 可以是值类型，但资源 owner 默认不实现或不显式承诺 `Sync`，除非确认 libobmm 和内核接口对同一 memid 的并发操作语义。
- `MappedMemory` 默认不实现 `Sync`；是否 `Send` 取决于 fd/mmap 生命周期和所有权模型，建议先保守，不手写 `unsafe impl Send/Sync`。
- ownership 变更涉及跨 host 一致性，safe API 只能保证本进程调用形式正确，不能保证分布式互斥；文档必须要求调用方实现跨 host 协调。

公开 `unsafe` 契约：

- `export_user_addr`：调用方保证地址、长度、权限、hugetlb/THP、pid 生命周期和 unexport 前访问约束。
- 从 raw descriptor 构造 safe descriptor：调用方保证 buffer 至少为 fixed header + `priv_len`，字段符合 C ABI。
- 从 raw fd 或 raw pointer 构造 `MemoryDevice`/`MappedMemory`：调用方保证唯一所有权或明确借用关系，避免 double close/munmap。

暂不封装区域：

- `vendor_adaptor.h` 是 libobmm 内部适配接口，不进入 Rust public API。
- `/dev/obmm` ioctl 原始命令不直接暴露；Rust 用户应经 libobmm public API。
- sysfs 维测文件可后续增加只读 helper，但不作为第一版核心 safe API。

**ASYNC-ANALYZE.md 正文**

结论：第一版不设计 async crate。

理由：

- `libobmm` public API 是同步控制面调用，成功后数据面通过 mmap 后的普通 load/store 完成。
- 没有 fd readiness、completion queue、callback 或 poll/multi API 需要 runtime 集成。
- 阻塞点主要是 ioctl、NUMA 上线、内存分配、preimport/import/export，这些更适合由调用方放入线程池或启动阶段执行。
- async runtime 依赖不应进入 `libobmm-sys`。safe crate 可以提供同步 API；如果业务后续需要 Tokio 封装，应在独立 `obmm-tokio` 或应用层用 `spawn_blocking` 包装。

例外：

- 若后续上游增加事件 fd、异步 import/export 状态查询、ownership 变更完成通知，再新增 `obmm-async` 或 `obmm-tokio`。
- sysfs/procfs 监控可做普通阻塞读取，不构成 FFI async 需求。

**CHECKLIST.md 正文**

调研：

- 确认上游 dev 包是否安装 `libobmm.h` 和 `<ub/obmm.h>`。
- 确认 `OBMM_MAX_LOCAL_NUMA_NODES`、`OBMM_MAX_PRIV_LEN`、flags、`OBMM_MMAP_FLAG_HUGETLB_PMD` 的定义来源。
- 确认 `libobmm.so` 是否有稳定 SONAME、是否会发布 pkg-config 文件。
- 确认目标平台：Linux only、架构、openEuler/kernel 版本、硬件依赖。

绑定：

- 建立 `libobmm-sys`，声明 `links = "obmm"`。
- 手写 extern 函数签名。
- 为 flexible array member 设计 raw header + offset 校验。
- 添加 `build.rs` 的 env/pkg-config/system path/docs.rs 分支。
- 添加 ABI/layout/constant 校验，至少覆盖两个 descriptor、`mem_id`、flags 和函数签名。

Safe API：

- 实现 `MemId(NonZeroU64)`。
- 分离 `ExportedMemory`、`ImportedMemory`、`PreimportedRange`。
- 为 `MemoryDescriptor` 和 `PreimportInfo` 提供 builder，自动处理 `priv_len` 和 buffer 大小。
- 显式 `close/unexport/unimport/unpreimport` 返回 `Result`，Drop best-effort。
- `export_user_addr` 保持 `unsafe` 并写完整 `# Safety`。
- 保守处理 `Send`/`Sync`，没有依据不手写 unsafe impl。

测试与 CI：

- 无硬件 CI：`cargo check`、layout/constant 生成检查、docs.rs。
- 有 dev 包 CI：link smoke test 仅确认可链接，不要求 `/dev/obmm` 存在。
- 外部硬件 CI：export/import/preimport/unimport/unexport、mmap、ownership、query 的真实工作流。
- negative tests：无效 memid、非法 flags、超长 priv、未对齐长度、Drop 顺序。

发布：

- README 说明系统依赖、内核模块、硬件平台、许可证。
- crate feature 明确：默认 dynamic system；`static`/`vendored` 暂不默认。
- 记录上游版本 `1.0.1` 的绑定基线。
- 每次上游升级先跑 ABI diff，再决定 Rust semver。