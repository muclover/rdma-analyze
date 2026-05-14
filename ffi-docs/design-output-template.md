# 新项目 Rust FFI 设计方案（输出模板）

将下列章节填完后，即形成可评审、可落地的 FFI 架构设计文档。可与 [methodology-any-c-project.md](methodology-any-c-project.md) 的检查表对照。

---

## 1. 目标 C 库画像

- **库名与版本范围**：
- **API 形态**（句柄 / 缓冲区 / 回调 / 对象 / 状态机 / I/O）：
- **ABI 稳定性与版本探测需求**：
- **头文件难点**（`static inline`、宏、变长、`union`、bitfield、函数指针表）：
- **资源所有权与销毁顺序**：
- **全局初始化、线程模型、allocator 约束**：
- **链接与分发**（系统 / vendored / 静态 / `dlopen` / 交叉编译）：
- **与运行时的关系**（阻塞 / readiness / completion / callback；是否与 Tokio 同进程）：

## 2. 约束与非目标

- **必须满足**：
- **明确不做**（非目标）：
- **风险等级接受度**（例如：是否接受终端依赖 libclang）：

## 3. crate 分层图

- 附 mermaid 或文字说明：`C` → `*-sys` → `safe-low` → `ergonomic` / `async-adapter`（删去不存在的层并说明理由）。

## 4. `-sys`：生成、链接与「数据路径闭合」

- **绑定生成**：bindgen / 手写 / 预生成（工具名）/ 混合；allowlist/blocklist 要点。
- **若存在 inline/宏/ops 热路径**：选用的闭合策略及**为何**排除其它策略。
- **`build.rs` 与 `links`**：
- **feature 与构建矩阵**（系统库、静态、vendor、平台）：

## 5. safe API 类型模型

- **哪些类型封装 C 句柄**；opaque / newtype / `ManuallyDrop` 使用点。
- **safe / `unsafe` / builder / token** 分界：引用「准入标准」一节结论。

## 6. 资源生命周期与 `Drop` 顺序

- 成对调用表；错误路径是否泄漏；多资源时的 drop 顺序。

## 7. 错误模型

- 主错误类型；C 码映射；是否需栈式错误（类 OpenSSL）。

## 8. 回调与 panic 策略

- C→Rust 回调列表；`catch_unwind` / abort；闭包 `Send + 'static` 要求；生命周期 pinning。

## 9. `Send` / `Sync` 策略

- 原始句柄、owned 句柄、borrowed 视图分别是否 `Send`/`Sync`；thread-affine 类型；worker/poll guard 是否建模。

## 10. async / 并发策略

- 阻塞 / readiness / completion / callback 选型；与 [async-ecosystem.md](async-ecosystem.md) 决策表对齐；backpressure；cancellation 与 C 侧可取消性。

## 11. 测试与验证

- layout / ABI / systest / ctest；多版本 C 库 CI；动静态链接 smoke；交叉编译；Sanitizer；Miri 范围；回调 panic 测试；Drop/错误路径泄漏测试。

## 12. 已知 `unsafe` 边界清单

- 逐项列出：调用方必须保证的前提、无法由类型系统表达的不变量、已知 UB 风险场景。

---

**评审人 / 日期**：
