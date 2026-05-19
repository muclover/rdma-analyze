# FFI 设计问卷

这份问卷用于把目标 C 库转换成 Rust FFI 架构画像。先从头文件、构建脚本、README、许可证和已有 Rust 草稿中推断答案；只有会改变架构方向的未知项才询问用户。

## 使用方式

- 每个问题都要有结论；信息不足时写 `Unknown`，并说明对设计的影响。
- 不要把问卷当成逐题访谈。先静态扫描，再一次性询问阻塞项。
- 问卷结论应进入交付物：库画像、决策摘要、风险与未决项。

## Q1-Q10

| # | 问题 | 影响 |
|---|------|------|
| Q1 | API 以 opaque 指针、ops 表、回调、还是值结构体为主？ | safe 类型、所有权、`repr(C)` 暴露策略 |
| Q2 | 是否存在大量 `static inline`、宏、union、bitfield 或平台条件类型？ | `bindgen`、`wrapper.c`、手写替代、allowlist/blocklist |
| Q3 | 资源是否有明确 create/destroy、引用计数、borrow、全局 init/cleanup？ | RAII、`Drop`、`Clone`、生命周期、父子资源顺序 |
| Q4 | 错误是 `errno`、返回码、错误栈、out param、还是回调错误？ | `Error` 类型、`Result` 映射、错误上下文 |
| Q5 | 分发依赖是系统库、vendored、多后端、还是生成头文件？ | `build.rs`、feature、`links`、docs.rs 和 CI 策略 |
| Q6 | CI 是否缺少 dev 包、专用硬件、内核模块或特定平台？ | ABI 测试、mock/smoke test、feature gate、降级路径 |
| Q7 | 是否面向 crates.io 生态发布并需要独立 `-sys`？ | crate 分层、semver、公开 ABI 面、重复链接风险 |
| Q8 | 上游许可证、vendored 源码或静态链接是否影响下游？ | feature 默认值、README 警示、发布范围 |
| Q9 | safe 层之上是否需要协议、运行时或 ergonomic API 层？ | 三层 workspace、协议 crate、依赖隔离 |
| Q10 | 是否需要 async 或 reactor 集成？ | `ASYNC-ANALYZE.md`、Tokio/async-std 位置、独立 async crate |

## 扫描重点

| 来源 | 提取内容 |
|------|----------|
| `*.h` / `include/` | opaque 类型、值结构体、ops 表、回调、union、bitfield、宏 API、`static inline` |
| README / INSTALL / docs | 链接方式、线程安全、版本支持、平台限制、许可证 |
| `CMakeLists.txt` / `Makefile` / `meson.build` / `*.pc.in` | 库名、pkg-config、依赖、feature、多后端、生成头 |
| 目录结构 | 单库/多库、vendored 可能性、平台分支、测试入口 |
| 已有 Rust 文件 | crate 命名、`links`、feature、unsafe 边界、测试形态 |

## 输出到交付物

| 问卷主题 | 写入位置 |
|----------|----------|
| API 与资源画像 | `ARCHITECTURE.md`、`SAFE-API.md` |
| 绑定难点 | `BINDING.md` |
| 构建与链接 | `ARCHITECTURE.md`、`BINDING.md`、`CHECKLIST.md` |
| 许可证与发布 | `ARCHITECTURE.md`、`README.md` |
| async / 上层协议 | `ASYNC-ANALYZE.md` |

