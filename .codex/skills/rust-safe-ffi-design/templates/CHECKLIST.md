# [库名] — 实施检查清单

> 与 [ARCHITECTURE.md](./ARCHITECTURE.md)、[BINDING.md](./BINDING.md)、[SAFE-API.md](./SAFE-API.md) 一致。

## 调研

- [ ] 头文件与 API 形态已归档
- [ ] 许可证与传递依赖已确认
- [ ] Q1-Q10 已填写，Unknown 已进入风险与未决项
- [ ] 架构决策已与团队评审

## 脚手架

- [ ] `*-sys` + safe crate 已创建
- [ ] `links` / LICENSE 已配置
- [ ] `build.rs` 探测顺序已实现并注释

## 绑定

- [ ] 绑定路径（手写/bindgen/wrapper）已落地
- [ ] 至少一个未选绑定路径已明确排除并说明原因
- [ ] blocklist / 手写类型已完成
- [ ] systest 或等效 ABI 测试已添加
- [ ] 再生命令已文档化（若 bindgen）

## Safe 层

- [ ] 核心类型 + `Drop` 已实现
- [ ] 错误类型与映射已实现
- [ ] `# Safety` 与契约表已写入文档
- [ ] `Send`/`Sync` 已基于 C 线程安全依据决定，未知时保持保守
- [ ] 至少一个 safe example

## 质量与发布

- [ ] CI：stable + 主要 feature
- [ ] 无硬件 / 弱依赖 CI 路径（若需要）
- [ ] README：链接方式、feature、维护说明
- [ ] semver / breaking 政策已声明
- [ ] docs.rs、弱依赖路径、许可证与 vendored 影响已说明

## Async（若 [ASYNC-ANALYZE.md](./ASYNC-ANALYZE.md) 为「需要」）

- [ ] async 集成层已实现或独立 crate 已规划
- [ ] 线程与 `Send`/`Sync` 已文档化
