# FFI 设计交付检查清单

这份清单是生成 `CHECKLIST.md` 和最终自检的基线。它描述应覆盖的工作，不要求在设计阶段实现代码。

## 调研

- [ ] 目标 C 项目路径、主头文件、版本范围和输出目录已确认。
- [ ] API 画像已覆盖 opaque、值结构体、ops 表、回调、宏、inline、union、bitfield。
- [ ] 资源模型已覆盖 create/destroy、引用计数、borrow、全局 init/cleanup。
- [ ] 错误模型已覆盖返回码、`errno`、错误栈、out param 和回调错误。
- [ ] 构建模型已覆盖系统库、pkg-config、vendored、多后端和平台差异。
- [ ] 许可证、静态链接和下游发布风险已记录。

## 架构

- [ ] 明确选择纯 `-sys`、双 crate、三层 workspace 或复用已有 `-sys`。
- [ ] 明确每个 crate 的职责、发布范围和 semver 策略。
- [ ] 明确 safe 层厚度：薄封装、领域 API、IO trait、协议层或 async 层。
- [ ] Unknown 项已进入风险表，并给出确认后的影响。

## 绑定与构建

- [ ] 绑定路径已选定：手写、预生成 `bindgen`、构建时 `bindgen`、`wrapper.c`、mummy 或组合。
- [ ] 至少一个未选绑定路径已被明确排除并说明原因。
- [ ] 入口头文件、allowlist、blocklist、手写替代类型和再生流程已说明。
- [ ] `build.rs` 探测顺序已说明：pkg-config、环境变量、系统路径、vendored 或其他。
- [ ] `links` 是否需要已明确，并说明重复链接或冲突影响。
- [ ] docs.rs 和 CI 缺少原生依赖时的降级路径已说明。

## Safe API

- [ ] 核心 Rust 类型、C 对应类型、所有权和 `Drop` 关系已列出。
- [ ] 错误类型与 C 错误模型的映射已列出。
- [ ] 每个公开 `unsafe` 入口都有 `# Safety` 契约草案或封装计划。
- [ ] 回调、panic、`user_data` 生命周期和线程要求已说明。
- [ ] `Send`/`Sync` 采用保守默认；如果实现，写出依据。
- [ ] 暂不封装的 C API 区域已列出，并说明原因。

## Async 与上层协议

- [ ] `ASYNC-ANALYZE.md` 明确 async 需要或不需要；N/A 也有理由。
- [ ] 若需要 async，已说明集成层级、运行时依赖位置和 C 等待机制。
- [ ] 若需要上层协议 crate，已说明 FFI 止于哪一层。

## 发布与维护

- [ ] 6 个交付文件全部存在，名称固定。
- [ ] README 有阅读顺序、一句话摘要、状态和决策摘要。
- [ ] CHECKLIST 覆盖绑定、safe、测试、CI、文档和发布。
- [ ] 绑定再生、ABI 检查、上游版本升级流程已写清。
- [ ] 最终交付物不含内部 reference 路径、样本编号或研究材料标记。

