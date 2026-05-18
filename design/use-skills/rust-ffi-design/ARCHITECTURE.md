# libobmm — 架构设计

## 1. 库画像（静态分析）

| 项 | 结论 |
|----|------|
| **C 库名称与角色** | `libobmm`：超节点/UB 场景下 export/import 远端内存；配合内核 `obmm.ko` 与 `/dev/obmm_shmdev*` |
| **主要头文件** | `obmm/src/libobmm/libobmm.h`（安装为 `<libobmm.h>`）；**依赖** `<ub/obmm.h>`（UB 协议常量、`OBMM_MAX_*`、`obmm_cmd_*`、flags） |
| **API 形态** | 过程式 C API；`mem_id`（`uint64_t`）；**值结构体 + flexible array member**（`obmm_mem_desc.priv[]`、`obmm_preimport_info.priv[]`） |
| **inline / 宏 / union** | 公开 API 头 **无** 大量 inline；`libobmm_log.h` 含 `static inline`（实现细节，**不纳入** `-sys` 绑定面） |
| **资源模型** | `mem_id` 标识 export/import 会话；`obmm_unexport` / `obmm_unimport` 释放；描述符由调用方分配（含 `priv_len` 变长尾部） |
| **线程安全** | 文档未明确；按内核/设备语义，safe 层默认 **不** 标记 `Sync`，文档要求调用方串行或自行同步 |
| **分发与依赖** | CMake 产出 `libobmm.so` → `lib64`；需系统安装 **UB 头文件**、已加载 **obmm 内核模块**、匹配硬件/平台 |
| **许可证** | 用户态 **Mulan PSL v2**；内核 **GPL-2.0**（Rust 绑定仅链接用户态库） |

## 2. 设计目标与约束

- **发布范围**：crates.io / 团队内部生态均可；默认按**可发布双 crate** 设计。
- **MSRV**：1.70+（`build.rs`、edition 2021）。
- **非目标**：不绑定内核模块 ioctl 面（仅 `libobmm`）；不封装 `mmap` 全流程（提供 `mem_id` + 文档指向设备路径）；不做性能调优与 OBMM 业务语义教程。

## 3. Crate 与工作区划分

```text
obmm-rs/                    # 可选 workspace 根
├── obmm-sys/               # FFI + build.rs
│   ├── src/lib.rs
│   ├── src/bindings/       # 预生成 bindgen 输出
│   ├── wrapper.h           # #include <libobmm.h> + <ub/obmm.h>
│   └── build.rs
└── obmm/                   # safe API
    ├── src/lib.rs
    ├── src/mem_desc.rs
    ├── src/export.rs
    ├── src/import.rs
    ├── src/ownership.rs
    └── src/error.rs
```

| Crate | 职责 | 对外发布 |
|-------|------|----------|
| `obmm-sys` | `extern "C"`、常量、原始结构体布局 | 是 |
| `obmm` | `MemId`、`MemDescBuilder`、export/import/preimport safe API | 是 |

**不** 单独拆 `obmm-errors`（API 面小）；若后续增长再拆。

## 4. 架构决策摘要

| 决策点 | 选择 | 理由 |
|--------|------|------|
| 是否独立 `-sys` | **是** | 生态发布、semver、下游仅需 FFI 时可依赖 `-sys` |
| safe 层厚度 | **中等** | 处理 FAM、对齐校验、`MemId` RAII；不隐藏 mmap 设备操作 |
| 绑定主路径 | **预生成 bindgen** + FAM 手写 | API 约 12 个函数但结构体含 FAM；`ub/obmm.h` 常量需一并生成 |
| `links` 键 | **`links = "obmm"`** | 与 CMake `OUTPUT_NAME obmm` 一致，避免重复链接 |
| 错误映射 | `-sys` 保持 C；safe 统一 `ObmmError` | `mem_id==0` 表失败；`int<0` 映射 `io::Error` |
| 变长 `priv` | safe **`MemDescBuilder`** 拥有 `Vec<u8>`，调用前拷贝到 C 布局缓冲区 | 避免 Rust 侧误用 FAM |
| NUMA 数组 | safe 提供 `[usize; OBMM_MAX_LOCAL_NUMA_NODES]` 或 `ExportLengths` newtype | 对齐文档中的 per-node length 约束 |

## 5. 依赖与许可证

- **传递原生依赖**：`libobmm.so`、运行时 **UB/内核 OBMM** 环境。
- **构建依赖**：系统或 `OBMM_INCLUDE_DIR` 提供 `libobmm.h`、`ub/obmm.h`。
- **Rust 依赖**：`obmm` 依赖 `obmm-sys`、`thiserror`（可选 `bitflags` 封装 flags）。
- **下游许可**：crate 采用 **Mulan PSL v2** 或与用户态库一致；README 声明**不** 链接 GPL 内核模块到 Rust 产物。

## 6. 风险与未决项

| 项 | 说明 | 计划 |
|----|------|------|
| `ub/obmm.h` 未随 obmm 仓库分发 | 绑定 CI 需 UB SDK 或文档化路径 | `build.rs` 检测 + README Prerequisites |
| 硬件/内核强依赖 | 通用 CI 仅能 `cargo build` + layout 测试 | systest + 可选 `#[ignore]` 集成测试 |
| `OBMM_MAX_*` 随内核演进 | 预生成绑定需版本 pin 说明 | 文档声明 tested obmm 版本（读 `obmm/VERSION`） |
| `vendor_adaptor` 内部符号 | 不导出；`-fvisibility=hidden` 已用于实现 | bindgen allowlist 仅公开 API |
