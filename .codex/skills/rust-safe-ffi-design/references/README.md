# rust-safe-ffi-design Reference Router

本目录是 `rust-safe-ffi-design` 的内部参考包。它帮助 agent 做 FFI 架构判断，不是用户交付物的一部分。

## 分层原则

| 层级 | 角色 | 读取时机 |
|------|------|----------|
| `core/` | 干净执行手册，可转化为交付物结构和判断规则 | 每次执行必读 |
| `cases/`、`rdma/`、`comparison.md` | case study、横向材料、RDMA 专题等内部证据 | 仅在目标库画像匹配或决策不确定时选读 |

`core/` 不应包含样本编号、研究阶段术语或内部路径痕迹。`cases/`、`rdma/`、`comparison.md` 可以保留这些内部材料，但只能辅助判断，不能直接复制到用户交付物。

## 必读路径

每次执行按顺序读取：

1. [core/decision-questions.md](./core/decision-questions.md)
2. [core/decision-rules.md](./core/decision-rules.md)
3. [core/delivery-checklist.md](./core/delivery-checklist.md)

这三份文件覆盖 Q1-Q10、crate 分层、绑定路径、linking、safe 边界、async、测试、CI 与发布检查。

## 条件路由

按目标 C 库画像选择少量内部材料，通常最多 1-2 个最相似材料。

| 目标库画像 | 选读 reference |
|------------|----------------|
| 只需要裸 FFI，`build.rs`、系统库、vendored 切换复杂 | [cases/libz-sys.md](./cases/libz-sys.md) |
| 双 crate、回调、panic 不能跨 FFI、multi/event API | [cases/curl-rust.md](./cases/curl-rust.md) |
| 大 workspace、多后端、错误栈、手写 + bindgen 混合 | [cases/rust-openssl.md](./cases/rust-openssl.md) |
| 三层 `sys -> safe -> ergonomic API`、预生成 bindgen、流式 API | [cases/zstd-rs.md](./cases/zstd-rs.md) |
| verbs、rdma-core、ops 表、inline 导出、无硬件 CI | [rdma/README.md](./rdma/README.md) 与 [rdma/overview.md](./rdma/overview.md) |
| 无法在单一 case 中定位，需要横向模式核对 | [comparison.md](./comparison.md) |

## 决策到交付物映射

| 决策主题 | 主要写入 |
|----------|----------|
| crate 分层、workspace、发布目标 | `ARCHITECTURE.md` |
| 手写、bindgen、wrapper、mummy、`build.rs`、linking | `BINDING.md` |
| 类型、RAII、错误、`# Safety`、`Send`/`Sync` | `SAFE-API.md` |
| 实施顺序、测试、CI、发布前检查 | `CHECKLIST.md` |
| async、reactor、上层协议 crate | `ASYNC-ANALYZE.md` |
| 阅读顺序与摘要 | `README.md` |

## 快速规则

| 条件 | 优先考虑 |
|------|----------|
| 无 inline 难题，中大 API，要 docs.rs 友好 | 预生成 bindgen + ABI 测试 |
| API 少且稳定 | 手写 `extern` + ABI 测试 |
| 频繁追随系统头文件 | 构建时 bindgen，但用 feature 或文档限制成本 |
| inline、宏、bitfield、union 难题明显 | `wrapper.c`、blocklist + 手写替代，或专用导出层 |
| 硬件或 dev 包不适合 CI | 设计仅编译路径、mock、静态桥接或外部环境测试 |
| 只做上层框架 | 优先复用已有 `-sys`，检查许可证与 ABI 版本 |

## 维护建议

- `core/` 保持短、规范、可直接执行。
- `cases/`、`rdma/`、`comparison.md` 可以较长，但只能按需读取。
- 若移动 reference 文件，必须同步更新本 router 和 `SKILL.md` 的相对链接。
