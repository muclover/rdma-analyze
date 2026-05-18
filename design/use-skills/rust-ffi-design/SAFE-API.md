# libobmm — Safe API 设计

## 1. 设计原则（本库）

- `unsafe` 仅出现在 `obmm-sys` 调用与 `MemDesc` → C 缓冲区的拷贝边界。
- 公开 API 稳定性：`obmm` crate 0.x 阶段允许调整对齐校验；`-sys` 随 C ABI 严格 semver。
- **不** 向生态 re-export 原始 `obmm_mem_desc` 指针类型。

## 2. 模块结构（草案）

```text
src/
├── lib.rs           # 重导出、文档入口
├── error.rs         # ObmmError
├── mem_id.rs        # MemId, ExportHandle, ImportHandle
├── mem_desc.rs      # MemDescBuilder, PreimportInfoBuilder
├── export.rs        # export, export_useraddr, unexport
├── import.rs        # import, unimport, preimport, unpreimport
├── ownership.rs     # set_ownership（需 RawFd）
└── query.rs         # query_memid_by_pa, query_pa_by_memid（调试）
```

| 模块 | 职责 |
|------|------|
| `mem_id` | `MemId` newtype；`ExportGuard`/`ImportGuard` 在 `Drop` 中 unexport/unimport |
| `mem_desc` | 构建变长 `priv`、校验 `priv_len ≤ OBMM_MAX_PRIV_LEN` |
| `export` / `import` | 业务入口，封装 NUMA length 数组与 flags |
| `ownership` | 对 `/dev/obmm_shmdev{N}` 的 fd 调用 `obmm_set_ownership` |

## 3. 核心类型

| Rust 类型 | 对应 C | 所有权 / `Drop` | `Send`/`Sync` |
|-----------|--------|-----------------|---------------|
| `MemId(NonZeroU64)` | `mem_id` | 由 `ExportGuard`/`ImportGuard` 持有 | 不实现 `Sync`（默认同线程） |
| `ExportGuard` | export 返回的 id | `Drop` → `obmm_unexport` | — |
| `ImportGuard` | import 返回的 id | `Drop` → `obmm_unimport` | — |
| `MemDescBuilder` | `obmm_mem_desc` + priv 缓冲 | owned `Vec<u8>` 布局 | — |
| `ExportLengths` | `size_t length[]` | 栈/owned 数组 | — |
| `PreimportInfoBuilder` | `obmm_preimport_info` | 同 MemDesc | — |

## 4. 错误模型

| 层 | 类型 | 映射规则 |
|----|------|----------|
| `-sys` | 原始 `mem_id`、`c_int` | 保持 C 语义 |
| `obmm` | `Result<_, ObmmError>` | `mem_id == 0` → `InvalidMemId`；`ret < 0` → `Io(io::Error::last_os_error())`；对齐/参数 → `InvalidInput` |

```rust
// 草案
pub enum ObmmError {
    InvalidMemId,
    InvalidInput(&'static str),
    Io(io::Error),
}
```

## 5. `unsafe` 边界与 `# Safety`

### 5.1 集中 `unsafe` 的函数/模块

- `mem_desc.rs`：`as_c_desc(&self) -> &ObmmMemDescHeader` 内部 `unsafe` 仅当缓冲区长 verified。
- `export.rs` / `import.rs`：调用 `-sys` 的单行 `unsafe { obmm_export(...) }`。

### 5.2 C 契约表（草案）

| C API / 场景 | 前置条件 | 后置条件 | 不得违反 |
|--------------|----------|----------|----------|
| `obmm_export` | `length` 对齐、和 >0；`desc` 由 builder 构建，`priv_len` 合法 | 成功时 `MemId != 0`，`desc` 出参字段有效 | 不得在未 unexport 时重复 export 同一逻辑资源 |
| `obmm_import` | `desc` 入参完整；对齐满足 doc | 成功返回非 0 id | 必须 unimport |
| `obmm_export_useraddr` | `va`/`length` 有效、进程可访问 | 同 export | pid=0 表示当前进程 |
| `obmm_set_ownership` | `fd` 为对应 shmdev；`[start,end)` 已 mmap | `prot` 符合内核要求 | `PROT_WRITE` 隐含 READ |
| FAM `priv` | `priv_len` 与缓冲区一致 | — | 禁止 `priv_len > 实际分配` |

## 6. 回调（若有）

- 无 C 回调注册 API；**不适用**。

## 7. 暂不封装范围

| C API / 区域 | 原因 | 后续 |
|--------------|------|------|
| 直接 `mmap` shmdev | 属 OS 交互；用 `std::fs::OpenOptions` + `mmap2` 在应用层 | 可选 `obmm-mmap` 辅助 crate |
| `vendor_*` | 内部实现 | 不绑定 |
| 日志宏 `OBMM_LOG*` | 实现细节 | 不绑定 |

## 8. 示例入口（计划）

| 示例 | 演示 |
|------|------|
| `export_import` | 最小 export → 填 desc → import（需 OBMM 环境，`#[ignore]`） |
| `mem_desc_builder` | 仅 safe 构建描述符 + layout 测试（无硬件） |
| `ownership` | fd + `set_ownership`（需设备） |
