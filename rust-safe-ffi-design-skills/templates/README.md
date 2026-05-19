# [库名] Rust FFI 架构设计 — 文档索引

> **对象 C 库**：[名称与版本]  
> **设计范围**：`-sys` 绑定 · safe API · `build.rs`/链接 · 测试/CI · async/上层协议  
> **状态**：草案 / 评审中 / 已定稿  
> **日期**：[YYYY-MM-DD]

## 一句话摘要

[例如：采用 `libfoo-sys` + `libfoo` 双 crate；预生成 bindgen + systest；pkg-config 优先；无 crate 内 async。]

## 阅读顺序

| 顺序 | 文档 | 内容 |
|------|------|------|
| 1 | [ARCHITECTURE.md](./ARCHITECTURE.md) | 库画像、crate 划分、总决策 |
| 2 | [BINDING.md](./BINDING.md) | 绑定方式、`build.rs`、链接 |
| 3 | [SAFE-API.md](./SAFE-API.md) | safe 层 API 与契约 |
| 4 | [ASYNC-ANALYZE.md](./ASYNC-ANALYZE.md) | async 与上层协议必要性 |
| 5 | [CHECKLIST.md](./CHECKLIST.md) | 实施勾选清单 |

## 决策摘要表

| 维度 | 决定 |
|------|------|
| Crate 分层 | |
| 绑定方式 | 手写 / 预生成 bindgen / 构建时 bindgen / wrapper.c / 其他 |
| 链接策略 | |
| 错误模型 | |
| Async | 需要 / 不需要 |
| 上层协议 crate | 需要 / 不需要 |
