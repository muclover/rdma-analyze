已按你的限制执行：只静态阅读了 `obmm/src/libobmm/libobmm.h`、`obmm/src/libobmm/CMakeLists.txt`、`obmm/doc/libobmm.md`，没有读取或依赖任何 skill，也没有编译或实现代码。

**结论**

这份草案不建议按原样推进。主要问题不是 Rust 语法层面，而是 FFI 边界、可移植构建、flexible array member、生命周期和一致性模型都被过度简化了。建议改成 `obmm-sys` + `obmm` 两层设计，底层只表达 C ABI，安全层用类型系统封装 mem id、描述符缓冲区、导入/导出生命周期和权限切换。

**需要修订的点**

1. `只做一个 obmm crate，不拆 obmm-sys` 不合适  
   `libobmm.h` 暴露的是裸 C ABI，且依赖外部头 `<ub/obmm.h>`（[libobmm.h:23](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:23)）。建议拆：
   - `obmm-sys`：`#[repr(C)]`、extern 函数、常量、链接逻辑、ABI 检查。
   - `obmm`：安全 Rust API、RAII、错误类型、描述符构造/解析。
   
   如果暂时不想发布两个 crate，也至少应在单 crate 内部保持 `ffi`/`sys` 私有模块和 safe 层严格分离。

2. `build.rs 直接链接 -lobmm` 太脆弱  
   CMake 安装的是 `libobmm.so` 到 `lib64`，头文件到 `include`（[CMakeLists.txt:50](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/CMakeLists.txt:50)、[CMakeLists.txt:51](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/CMakeLists.txt:51)）。`build.rs` 应支持：
   - `OBMM_LIB_DIR` / `OBMM_INCLUDE_DIR`
   - 可选 `pkg-config`，如果未来项目补 `.pc`
   - `cargo:rustc-link-search=native=...`
   - `cargo:rustc-link-lib=dylib=obmm`
   - docs.rs 或无系统库环境下的降级路径

3. `用户构建时 bindgen 生成全部绑定` 风险高  
   当前头依赖 `<ub/obmm.h>`，其中包含 `OBMM_MAX_LOCAL_NUMA_NODES`、`OBMM_MAX_PRIV_LEN` 等关键常量，但不在 `libobmm.h` 本文件中定义（[libobmm.h:61](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:61)、文档提到上限在 [libobmm.md:120](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:120)）。用户构建时 bindgen 会要求本机完整 C 头环境、clang、架构匹配，docs.rs 也容易失败。

   建议：
   - `obmm-sys` 默认提供预生成或手写的最小 ABI 绑定。
   - 提供 `bindgen` feature 供维护者再生成，不作为普通用户默认路径。
   - 只绑定公开 API，不导出全量 bindgen 噪音。

4. `mem_id 暴露为 u64，导出/引入直接返回 u64` 不合适  
   文档明确 `OBMM_INVALID_MEMID == 0` 是错误/无效 ID（[libobmm.h:30](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:30)、[libobmm.md:46](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:46)）。safe 层应使用：
   - `struct MemId(NonZeroU64)`
   - `Result<ExportedMemory, Error>`
   - `Result<ImportedMemory, Error>`
   
   裸 `u64` 只应留在 `obmm-sys`。

5. `struct obmm_mem_desc 用普通 Rust struct + Vec<u8> priv` 是 ABI 错误  
   C 结构体使用 flexible array member：`uint8_t priv[]`（[libobmm.h:44](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:44)）。文档要求用 `sizeof(struct obmm_mem_desc) + priv_len` 分配连续内存，`priv` 紧随结构体尾部（[libobmm.md:118](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:118)、[libobmm.md:120](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:120)）。

   Rust safe 层应设计成：
   - `MemDescBuf { buf: Vec<MaybeUninit<u8>> }` 或 boxed byte buffer
   - 内部按 C layout 写 header + trailing priv
   - 对外提供 `MemDesc`/`MemDescOwned` 访问器
   - 不把 `priv` 放进 `#[repr(C)] struct` 的 `Vec<u8>` 字段

   `obmm_preimport_info` 同理，也有 `priv[]`（[libobmm.h:58](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:58)）。

6. `所有 handle unsafe impl Send + Sync` 过度承诺  
   文档强调共享 cacheable 内存需要跨 host 权限一致性，同一时刻只能全读或单写（[libobmm.md:208](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:208)-[libobmm.md:211](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:211)）。这不是普通 Rust 内存共享语义，不能 blanket `Send + Sync`。

   建议：
   - `MemId` 作为纯 ID 可以 `Copy + Send + Sync`。
   - 拥有生命周期的 `ExportedMemory` / `ImportedMemory` 默认不要 `Sync`。
   - `Send` 也要基于内部是否只持有 ID、fd、描述符缓冲区来判断。
   - mmap 后的可访问区域需要单独类型表达权限状态，如 `MappedNone`、`MappedRead`、`MappedWrite`，不要让多个线程无约束共享写权限。

7. `错误只用 i32` 信息不足  
   C API 有两种错误形态：
   - 返回 `mem_id` 的函数用 `0` 表示错误：`obmm_export`、`obmm_export_useraddr`、`obmm_import`（[libobmm.h:61](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:61)、[libobmm.h:76](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:76)、[libobmm.h:78](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/src/libobmm/libobmm.h:78)）。
   - 返回 `int` 的函数用整数状态：`unexport`、`preimport`、`set_ownership` 等。

   safe 层建议：
   ```rust
   enum Error {
       InvalidMemId,
       ObmmCall { function: &'static str, code: i32 },
       Io(std::io::Error), // 仅在确认 errno 语义后使用
       InvalidArgument(&'static str),
       UnsupportedOnDocsRs,
   }
   ```
   是否读取 `errno` 需要进一步看函数级文档或 C 实现；仅凭这三份文件不能断言。

8. `暂时不做 ABI 检查和 docs.rs 降级` 不建议  
   ABI 风险集中在：
   - `unsigned long` 宽度
   - `size_t`
   - `int`
   - flexible array header 大小/对齐
   - `OBMM_MAX_LOCAL_NUMA_NODES`
   - `OBMM_MAX_PRIV_LEN`

   至少应在 `obmm-sys` 里用 `ctest` 或构建期小 C 程序检查 layout；safe crate 在 docs.rs 下应跳过链接系统库，只生成文档和类型说明，否则用户看到的 crate 文档不可用。

**建议的新设计**

底层：

```text
obmm-sys
  - ffi::mem_id = u64
  - ffi::obmm_mem_desc_header
  - ffi::obmm_preimport_info_header
  - extern "C" functions
  - link libobmm.so
  - layout/constant checks
```

安全层：

```text
obmm
  - MemId(NonZeroU64)
  - MemDescBuf / PreimportInfoBuf
  - ExportRequest / ImportRequest / PreimportRequest
  - ExportedMemory { id, desc }
  - ImportedMemory { id, numa_id }
  - PreimportedMemory { info }
  - Ownership APIs with explicit Prot
  - Drop-based unexport/unimport/unpreimport, plus explicit close methods
```

生命周期应体现文档时序：先 export，再 import，访问后先 unimport，再 unexport（[libobmm.md:243](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:243)-[libobmm.md:251](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/obmm/doc/libobmm.md:251)）。safe API 不能完全阻止跨进程/跨 host 误用，但至少不能把错误时序包装成“天然安全”。

**最终建议**

草案应改为：拆 sys/safe 层；不要把 FAM 映射成普通 Rust struct；不要让 safe API 返回裸 `u64`；不要 blanket `Send + Sync`；默认不要用户构建时全量 bindgen；补 ABI 检查和 docs.rs 降级。这样设计才配得上 OBMM 这种涉及内核模块、NUMA、mmap 权限和跨 host 一致性的 FFI。