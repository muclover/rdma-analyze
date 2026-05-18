# 参考文档副本说明

本目录内容为仓库分析产出的**只读副本**，供 `rust-safe-ffi-design` 技能自包含引用。

| 副本路径 | 权威源（仓库根） |
|----------|------------------|
| `guide/*` | `docs/guide/*` |
| `comparison/*` | `docs/comparison/*` |
| `rdma/rdma-overview.md` | `docs/rdma/rdma-overview.md` |
| `rdma/README.md` | `docs/rdma/README.md` |
| `rdma/category-*/*` | `docs/rdma/category-*/*` |
| `libz-sys/` … `zstd-rs/` | `docs/libz-sys/` … `docs/zstd-rs/` |

**未收录**：仓库计划元数据与总索引文档。

## 运行时读取策略

- `guide/*` 是运行时核心参考。
- `comparison/*` 和各项目 `FFI-ANALYSIS.md` 是按画像选读的 case study。
- 不要在用户交付物中写入本目录的源路径、样本编号或研究阶段表述。

## 同步策略

当 `docs/guide`、`docs/comparison` 或 case study 有重大更新时，应重新复制到本目录，并检查：

- `references/README.md` 的路由表是否仍准确。
- `references/rdma/README.md` 是否有失效链接。
- 复制来的文件是否引入了对技能外元数据的运行时依赖。

**最近同步**：2026-05-18
