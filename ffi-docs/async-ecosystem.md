# 异步与生态封装

Rust 的 `async/await` **不是** C 库的一部分；样本仓库内的绑定也几乎从不把 Tokio 写进 `-sys`。正确做法是：**在 FFI 边界保持阻塞或「就绪驱动」语义**，在 **单独一层** 接入 executor。

## 1. 分层：crate 内 vs 生态

```mermaid
flowchart LR
    subgraph inCrate [样本 crate 内常见能力]
        A[阻塞 Easy.perform]
        B[Multi + perform + wait]
        C[同步 compress/decompress]
    end
    subgraph ecosystem [生态适配层]
        D[tokio-openssl 等]
        E[spawn_blocking 包装]
        F[async-compression 等]
    end
    inCrate --> ecosystem
```

| 样本 | crate 内提供的「并发相关」能力 | 典型生态下一步 |
|------|----------------------------------|----------------|
| curl-rust | `Multi`：多路复用、socket 就绪、`wait`；测试见 `tests/multi.rs` | 与 `mio`/`polling` 或上层 HTTP 客户端（如 `isahc` 历史方案、`reqwest` 可选 curl 后端等）组合；**非**内置 async。 |
| rust-openssl | `SslStream` 等阻塞 IO | **`tokio-openssl`**：在 Tokio 上把 TLS 作为 `AsyncRead`/`AsyncWrite` 的一部分；FFI 仍在 `openssl-sys`。 |
| zstd-rs / libz-sys | 同步 CPU/缓冲 API | **`tokio::task::spawn_blocking`** 或 **`async-compression`**（内部仍阻塞或 blocking 池）；不在 `-sys` 引 async runtime。 |

### 1.1 异步适配决策表（操作流选型）

先判定 C 库暴露的**进度模型**，再选 Rust 侧承载方式（**不要**在 `-sys` 内绑 Tokio）。

| C 侧进度模型 | 特征 | Rust 侧常见承载 | cancellation 注意 |
|--------------|------|------------------|---------------------|
| **阻塞调用** | 单次调用直到返回 | 默认同步；在 async 里用 **`spawn_blocking`** 或专用线程 | `Future` drop 时 C 调用可能已不可撤销，需文档或分离「取消令牌」 |
| **readiness-driven** | `poll`/fd/「可写可读」再推进 | **`mio`/`polling`**、`Multi`+socket、与 **`Waker`** 对接 | 取消 = 撤注册 fd 或丢弃 `Easy`；对齐 lib 文档 |
| **completion-driven** | post 与完成分离（CQ、io_uring 完成环等） | **专用线程** `poll_cq` + **`channel`/`Notify`/`oneshot`** 桥到 `Future` | 未完成 WR 与 `Future` drop 对齐策略；见 RDMA §5 |
| **callback-driven** | C 线程调用用户提供的函数指针 | **panic 隔离** + 闭包 **`Send + 'static`**（若 C 跨线程回调）；常配合 channel | 回调在 `Future` 取消后是否仍可能被调用须在契约中写明 |

**背压（backpressure）**：阻塞 API 用线程池长度/队列满返回错误；completion 路径用 **in-flight 上限** + CQ 消化速率；readiness 用 **写缓冲水位**。

**async wrapper 与句柄所有权**：明确 wrapper 是 **拥有** C 句柄还是 **借用** 已有 `stream`/`context`；后者需 `'driver` 生命周期或自引用结构 + `Pin`。

## 2. curl：Multi 与事件循环

- **设计要点**：libcurl 的异步故事是 **「把多个 `Easy` 放进 `Multi`，用 `perform`/`socket`/`action`/`wait` 驱动」**，而不是在绑定里实现 Future。
- **本仓库证据**：`curl-rust` 根 `Cargo.toml` 将 `mio` 置于 **dev-dependencies**，用于测试/示例级集成，而非强制终端用户依赖某 runtime。
- **选型建议**：若目标是把 curl 接进 Tokio，通常要么 **blocking 线程池里跑 `Easy::perform`**（最简单），要么 **用 `Multi` + 自己的 epoll/kqueue 与 runtime 的 waker 对接**（复杂但可控）。

## 3. OpenSSL 与 Tokio

- **openssl crate**：保持 **阻塞** 语义，便于与非 async 代码共用。
- **tokio-openssl**（crates.io）：在已有 `TcpStream` 等 async 流上包装 `SslStream`，由 Tokio IO 调度 **就绪后再调用** OpenSSL 读写；panic 与生命周期仍遵循 `openssl` 自身约束。
- **注意**：TLS 后端还可来自 **rustls** 等纯 Rust 栈；与「OpenSSL FFI」是不同路线，选型在应用层而非 `-sys` 内。

## 4. 压缩类库（zlib / zstd）

- C 侧 **无** 标准 async 接口；Rust 侧惯例：
  - **小任务**：直接同步调用。
  - **大任务或已在 async 上下文**：`spawn_blocking` + channel 传结果，避免阻塞 executor。
  - **流式**：在 `AsyncRead`/`AsyncWrite` 上包一层缓冲与 `poll_read`/`poll_write`，内部调用同步 `zstd`/`libz` API（注意 `Pin` 与内部缓冲不变量）。

## 5. RDMA（libibverbs）：CQ 语义与 Rust async

- **硬件语义**：`ibv_post_send` / `post_recv` 把 WR 推入队列；完成在 **CQ** 上通过 `ibv_poll_cq`（或 CQ EX、completion channel）收回。这与「`async` 即 RDMA 异步」**不是一回事**：高吞吐 C 栈常用 **深队列 + 忙轮询或专用 poll 线程**，无需语言级 async。
- **各 FFI 栈的默认取向**：`sideway`、`rust-ibverbs`、`rdma-io` 等以 **同步 verbs + 应用自选线程模型** 为主；**DatenLord `async-rdma`** 在 `rdma-sys` 之上把 **Tokio 级编排**作为产品边界（详见 [rdma/docs/ffi-schemes/concurrency-threading-async-vs-sync.md](../rdma/docs/ffi-schemes/concurrency-threading-async-vs-sync.md)）。
- **与 Tokio 同进程时的常见桥接**：专用 **OS 线程**跑 `poll_cq` 循环，经 `channel`/`Notify`/`oneshot` 与 async 任务通信；**勿**在 executor 线程上对 CQ **忙等 spin**；低频 `reg_mr`、建 QP 可考虑 `spawn_blocking`。
- **总览入口**：[rdma-ffi-schemes.md](rdma-ffi-schemes.md)（五条主线 FFI 与通用 `-sys` 文档的衔接）。

## 6. 文档化建议（给绑定作者）

在 README 中显写三条：

1. **哪些函数会阻塞**（DNS、连接、握手、大块压缩）。
2. **是否线程安全**、是否可与 `spawn_blocking` 组合。
3. **推荐生态 crate 名称**（若有），并声明 **非** 本仓库维护的兼容性。

---

返回总览：[README.md](README.md)。对比细节：[compare-projects.md](compare-projects.md)。方法论与模板：[methodology-any-c-project.md](methodology-any-c-project.md)、[design-output-template.md](design-output-template.md)。RDMA 专线：[rdma-ffi-schemes.md](rdma-ffi-schemes.md)。
