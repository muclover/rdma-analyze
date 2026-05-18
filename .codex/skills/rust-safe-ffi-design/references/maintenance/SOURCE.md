# 参考文档副本说明

本目录内容为仓库分析产出的只读副本，供 `rust-safe-ffi-design` 技能自包含引用。

| 副本路径 | 权威源（仓库根） |
|----------|------------------|
| `references/core/*` | 从 `docs/guide/*` 提炼出的干净执行手册 |
| `references/evidence/comparison/*` | `docs/comparison/*` |
| `references/evidence/rdma/rdma-overview.md` | `docs/rdma/rdma-overview.md` |
| `references/evidence/rdma/README.md` | `docs/rdma/README.md` |
| `references/evidence/rdma/category-*/*` | `docs/rdma/category-*/*` |
| `references/evidence/cases/*.md` | `docs/libz-sys/`、`docs/curl-rust/`、`docs/rust-openssl/`、`docs/zstd-rs/` 的分析文件 |

未收录：仓库计划元数据与总索引文档。

## 运行时读取策略

- `core/*` 是每次执行必读的干净参考。
- `evidence/*` 是按画像选读的内部材料。
- 不要在用户交付物中写入本目录的源路径、样本编号或研究阶段表述。

## 同步策略

当 `docs/guide`、`docs/comparison` 或 case study 有重大更新时，应重新同步到本目录，并检查：

- `references/README.md` 的路由表是否仍准确。
- `references/evidence/rdma/README.md` 是否有失效链接。
- `references/core/` 是否仍保持无内部样本编号、研究阶段措辞和旧路径。
- 复制来的 evidence 是否引入了对技能外元数据的运行时依赖。

最近同步：2026-05-18

