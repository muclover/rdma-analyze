# libobmm — 实施检查清单

> 与 [ARCHITECTURE.md](./ARCHITECTURE.md)、[BINDING.md](./BINDING.md)、[SAFE-API.md](./SAFE-API.md) 一致。

## 调研

- [x] 头文件与 API 形态已归档（`libobmm.h`、`ub/obmm.h` 依赖）
- [x] 许可证已确认（用户态 Mulan PSL v2）
- [ ] 架构决策已与团队评审

## 脚手架

- [ ] 创建 `obmm-sys` + `obmm` crate
- [ ] `links = "obmm"` 与 LICENSE（Mulan PSL v2）
- [ ] `build.rs` 探测顺序已实现并注释
- [ ] README Prerequisites：内核模块、UB 头文件、硬件

## 绑定

- [ ] `wrapper.h` + 预生成 `obmm_bindings.rs`
- [ ] FAM 结构体手写布局 + blocklist 配置
- [ ] `regenerate_bindings.sh` 已文档化
- [ ] `systest` 已添加

## Safe 层

- [ ] `MemDescBuilder` / `PreimportInfoBuilder`
- [ ] `ExportGuard` / `ImportGuard` + `Drop`
- [ ] `ObmmError` 与 `Result` 映射
- [ ] `# Safety` 与契约表写入 rustdoc
- [ ] `mem_desc_builder` 无硬件示例

## 质量与发布

- [ ] CI：`cargo test -p obmm-sys`（layout）
- [ ] 集成测试 `#[ignore]` + 标签 `obmm-hardware`
- [ ] README：链接方式、OBMM 版本、feature
- [ ] semver 政策：`-sys` 随 C ABI；`obmm` 0.x

## Async（[ASYNC-ANALYZE.md](./ASYNC-ANALYZE.md) 为「不需要」）

- [x] 确认首版不引入 tokio 依赖
