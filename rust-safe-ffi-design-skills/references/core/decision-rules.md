# FFI 架构决策规则

这份文件给出默认决策。目标库的头文件、构建脚本、许可证和用户发布目标优先；默认规则只能在信息不足时提供保守起点。

## 总体默认

- 面向 Rust 生态发布时，优先设计 `foo-sys` + `foo` 双 crate；API 很大或上层协议复杂时再拆第三层 crate。
- `-sys` 负责 FFI、`build.rs`、链接、原始常量和原始类型，不在其中伪造 safe 语义。
- safe crate 负责 RAII、错误映射、生命周期、回调约束和文档化的安全边界。
- async 和运行时依赖不进入 `-sys`；优先放在 safe 之上或独立适配 crate。
- 每个核心选择都要写出放弃其他路径的理由，避免只有偏好没有判断。

## Crate 分层

| 选择 | 适用条件 | 风险 |
|------|----------|------|
| 纯 `-sys` | 目标只是暴露原始 FFI，已有上层 crate 或用户明确只要裸绑定 | 下游容易误用裸指针；semver 面直接等于 C ABI 面 |
| `-sys` + safe | 对外发布、希望 Rust 用户直接消费 safe API | 需要维护双 crate 版本、错误模型和文档 |
| 三层 workspace | safe 层仍偏薄，还需要 ergonomic API、IO trait、协议或运行时层 | 依赖边界必须清楚，避免循环依赖 |
| 复用已有 `-sys` | 只做上层框架或协议封装 | 要确认许可证、ABI 版本、feature 和链接策略 |

## 绑定路径

| 路径 | 优先条件 | 必备配套 |
|------|----------|----------|
| 手写 `extern` | API 小、符号稳定、维护者愿意人工跟踪 ABI | ABI / layout 测试，升级时人工 diff |
| 预生成 `bindgen` | API 中大型、头文件版本可控、希望 docs.rs 和用户构建简单 | 再生命令、allowlist/blocklist、生成文件检查 |
| 构建时 `bindgen` | 需要频繁跟随系统头文件或平台差异 | libclang 要求、CI 说明、feature 或 fallback |
| `wrapper.c` | `static inline`、宏表达式、bitfield 或复杂 C 类型必须通过 C 编译器解释 | `cc` 构建、最小导出面、wrapper 测试 |
| 手写替代类型 | union、bitfield 或宏 API 无法可靠生成 Rust 形态 | 清晰的 C 契约表、layout 检查 |
| mummy 或类似静态桥接 | CI 缺少系统 dev 包但需要编译通过特定符号面 | 运行时依赖说明、真实环境 smoke test |

默认判断：

- 无 inline 难题且 API 中大型：预生成 `bindgen` + ABI 测试。
- API 极小且多年稳定：手写 `extern` + ABI 测试。
- inline、宏、bitfield、union 难题明显：`wrapper.c`、blocklist + 手写替代，或专用导出层。
- 不要把复杂 C 表达式直接泄漏到 safe 层。

## 构建与链接

- 对原生库使用 `links = "native-name"` 时，要说明它如何避免重复链接以及是否会与其他 crate 冲突。
- `build.rs` 应写清探测顺序，例如 pkg-config、环境变量、系统路径、vendored feature。
- vendored feature 要说明默认值、许可证、静态链接影响和 docs.rs 策略。
- CI 不具备原生库或硬件时，提供最小编译路径；真实集成测试可以标记为需要外部环境。
- 不要在同一进程中无意混用两套同名原生库或 ABI 不兼容版本。

## Safe API 边界

- 每个 create/destroy 对都应有 Rust 所有权类型或明确保持在 raw 层。
- 引用计数资源可以用 `Clone`、`Arc` 或专用 owner 类型，但必须对齐 C 语义。
- 父子资源要写清 Drop 顺序，必要时使用 detach guard 或显式 close。
- `-sys` 保持 C 返回值；safe 层统一映射为 `Result<_, Error>` 或领域错误类型。
- 回调边界必须处理 panic 策略，避免 unwinding 跨越 C ABI。
- 没有 C 文档或足够不变量时，不实现 `Send`/`Sync`；需要跨线程时写出依据。
- 所有公开 `unsafe fn` 都要有 `# Safety` 契约草案或封装计划。

## 测试与发布守卫

| 目标 | 推荐做法 |
|------|----------|
| ABI 不漂移 | `systest`、`ctest2`、layout/constant/signature 检查 |
| 绑定可再生 | 记录再生命令，必要时 CI 检查生成文件是否变化 |
| safe 层可用 | 至少一个 init/destroy 或核心工作流 smoke test |
| 无原生库 CI | feature gate、mock、仅编译路径或外部环境标记 |
| API 不变量 | 文档测试、compile-fail 或 trybuild 测试 |
| 发布可用 | README、feature、docs.rs、许可证、semver、`links` 策略齐全 |

## Async 与上层协议

| 画像 | 决策 |
|------|------|
| 纯计算或同步 IO | `ASYNC-ANALYZE.md` 写 N/A，并说明调用方线程池或同步 API 足够 |
| C 库已有 fd / poll / multi API | safe 层暴露可集成的等待原语，运行时适配放上层 |
| crate 定位就是 async 框架 | 独立 async crate 或 safe 之上的运行时模块 |
| gRPC / QUIC / 协议封装 | 放在 safe 之上，不新增 `extern "C"` 绑定面 |

