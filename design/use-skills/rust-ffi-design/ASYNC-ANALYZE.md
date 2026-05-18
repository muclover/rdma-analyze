# libobmm — Async 与上层协议分析

## 1. 结论

| 项 | 决定 |
|----|------|
| **是否需要 crate 内 async API** | **不需要** |
| **是否需要独立 async 适配 crate** | **不需要**（首版） |
| **是否需要上层协议 crate**（gRPC/QUIC/自定义传输） | **不需要** |

## 2. 分析依据

### 2.1 C 库并发模型

- libobmm API 为 **同步阻塞** 调用（内部 ioctl/UB 交互）；完成 export/import 后，应用通过 **`mmap` 字符设备** 做 load/store。
- 无 completion queue、无注册回调、无 event fd 暴露在 `libobmm.h`。
- 所有权变更 `obmm_set_ownership` 为同步 ioctl 风格。

### 2.2 产品需求（Q10）

- Rust 生态消费者典型用法：在 **同步** 路径配置内存，再在 **同步或自有 runtime** 中访问 mmap 区域。
- 若需 async，属于 **应用层** 对 mmap 缓冲区的访问模式，与 OBMM 配置 API 正交。

## 3. 若不需要 async（选定）

- **理由**：C API 全同步；内存访问发生在 mmap 之后，与 tokio 无天然耦合点。
- **推荐替代**：
  - 在文档中说明：配置阶段用 `obmm` 同步 API；
  - mmap 后可使用 `memmap2` + 普通 Rust 访问；
  - 若未来需要，可增 **`obmm-async`**（仅包装「在 `spawn_blocking` 中调用 export/import」），**首版不做**。

## 4. 若需要 async（未采用）

（保留模板供日后复审：仅当 OBMM 提供异步完成事件 fd 时再开独立 crate。）

## 5. 上层协议 crate（Q9）

| 项 | 内容 |
|----|------|
| 是否需要 | **否** |
| 边界 | OBMM 提供**内存资源**；分布式协议不在 libobmm 内 |
| 计划 | 无 |

## 6. 未决项

| 项 | 计划 |
|----|------|
| 是否在 README 推荐 `tokio::fs::File::from_std` 打开 shmdev | 文档示例即可，非 crate API |
