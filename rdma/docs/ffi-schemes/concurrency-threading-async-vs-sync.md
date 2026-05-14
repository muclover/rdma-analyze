# 线程模型、高并发 RDMA 实践与 Rust 同步 / 异步选型

本文对应 `rdma/docs/ffi-schemes` 中五条 **FFI 主线**，分别说明：**各栈预设或可组合的线程模型**、**如何在同步封装下做高吞吐 / 低延迟 RDMA**，并单列一章对比 **Rust 异步与非异步** 的典型用法与适用场景。

**前置共识：** RDMA 硬件语义本质是 **「投递工作与拿回完成异步」**（`post_*` → 稍后 CQ completion）。语言层面的 `**async/await`** 解决的是 **「如何把阻塞与唤醒编排进运行时」**，并不等同于「RDMA 是否异步」——**无 async 也能极高吞吐**，只要把 **in-flight 深度、批处理、轮询线程与队列划分** 做对。

下文 **首先**从 **C + libibverbs（及常见 librdmacm）** 视角归纳业界主流做法，再对照各 Rust FFI 栈。

---

## 0. C / libibverbs：业界如何实现高并发（前置背景）

本节回答：**在原生 C 生态里**，高吞吐 / 高并行 RDMA **不靠某一门语言的 async**，而靠 **硬件队列语义 + 进程内线程与完成路径的组合**。下列做法是数据中心与高性能计算里 **最常见、文档与示例最多** 的路径。

### 0.1 硬件与 API 语义（为何「C 也没有魔法」）

- **发送侧：** 应用调用 `**ibv_post_send` / `ibv_wr_*`** 把工作请求（WR）推进 **Send Queue（SQ）**。网卡 **异步**从 SQ 取项并执行 RDMA；调用 `**post` 的线程不会在同一调用栈里「等着 DMA 做完」**。
- **接收侧：** `**post_recv`** 预填 **Recv Queue（RQ）**，对端数据到达后由网卡填缓冲区并产生完成。
- **完成侧：** 成功 / 失败结果进入 **Completion Queue（CQ）**。应用通过 `**ibv_poll_cq`**、**CQ extended（`ibv_start_poll` / `ibv_next_poll`）** 或 **completion channel + `ibv_get_cq_event`** 等 **收回**这些完成事件。

因此：**「并发」首先体现在网卡上同时有大量 WR in-flight**；CPU 侧则要 **足够快地发帖**并 **足够快地 drain CQ**，避免 CQ/SQ 溢出或背压。

### 0.2 主流用法一：单线程或少数线程 + 极深队列 + 忙轮询（latency / bandwidth benchmark 范式）

**代表：** Linux `**perftest`**（`ib_send_bw`、`ib_write_bw`、`ib_write_lat` 等）、大量厂商调优手册中的 **micro-benchmark**。

**要点：**

1. **增大 `tx_depth` / `rx_depth` / `inline`**（视 HW 与 opcode），使 **SQ/RQ 上同时挂大量未完成 WR**。
2. **批量发帖：** 旧 API 用 WR `**next` 链**一次提交多 WR；新路径用 `**ibv_wr_*` builder** 在同一 `**ibv_wr_complete`** 周期内构造多条。
3. **完成路径：** 在 **绑定的 CPU 核心**上 `**while (ibv_poll_cq(...) > 0)`** 或以 **CQ EX** 循环拉取；**刻意不使用 sleep**，用 **100% 一轮 CPU** 换 **尾延迟稳定与带宽顶满**。
4. **适用：** 打满链路、对比 RNIC、内核/驱动回归；生产里常见于 **专用 IO 线程 / polling core**。

这是业界 **最「教科书」的高吞吐路径**：**线程数不多，但单线程上的 in-flight 与 poll 极狠**。

### 0.3 主流用法二：多线程并行 —— 「分区」优先于「一把大锁」

**常见模式：**


| 模式                                   | 做法                                                                                    | 典型场景                                             |
| ------------------------------------ | ------------------------------------------------------------------------------------- | ------------------------------------------------ |
| **Thread-per-core / per-connection** | 每个线程（或每少量连接）独占 **QP + CQ**，线程内自洽 **post + poll**，**跨线程几乎不碰同一 QP**                     | 存储后端、KV、自定义 RPC；避免 `**ibv_post_send` 上的锁竞争**     |
| **CQ 专用线程**                          | 少数线程 **只做 `poll_cq`**，把 `**wc` / wr_id 摘要**塞进 **无锁队列或 ring**，业务线程只负责 **构造 WR 并 post** | 希望 **发帖逻辑复杂**但与 **poll 解耦**时                     |
| **共享 QP（少见）**                        | 多线程 `**post` 同一 QP**                                                                  | **必须**由库或应用保证 **verbs 线程安全契约**（通常应避免在延迟敏感路径依赖粗锁） |


**业界共识：** 延迟敏感栈里 **优先 partition QP**，而不是 many-writer 共享同一 QP。

### 0.4 CQ：忙轮询 vs 阻塞 / epoll（完成通知策略）


| 策略             | API / 形态                                                        | 优点                              | 代价                     |
| -------------- | --------------------------------------------------------------- | ------------------------------- | ---------------------- |
| **Busy poll**  | `ibv_poll_cq`、CQ EX                                             | **延迟低、抖动小**，易打满带宽               | **独占 CPU**             |
| **事件驱动阻塞**     | **completion channel** + `ibv_get_cq_event`、`ibv_ack_cq_events` | **省 CPU**，适合连接多、瞬时吞吐要求不极端       | **唤醒路径更长**，延迟通常高于忙轮询   |
| **与 epoll 结合** | 某些环境下对 **CQ 关联的 fd**（具体能力与内核/驱动相关）做 `**epoll_wait`**            | 与其它 fd **统一事件循环**（类似小型 reactor） | **集成复杂度**与 **平台差异**需核实 |


生产里常见 **混合**：数据面 **绑核 poll**，控制面或冷门队列 **走 event**。

### 0.5 控制路径：MR/QP/CM —— 与高并发数据面分开

- `**ibv_reg_mr`、`modify_qp`、`rdma_connect`** 等往往 **较慢**且可能触及内核 / 驱动；业界惯例是 **建连阶段做完**，**稳态数据面**只做 `**post` + `poll`**。
- `**librdmacm**` 常用于 **建连与事件**（`rdma_cm_id`）；不少应用仍把它放在 **单独线程**或 **阻塞 accept/connect**，与 **verbs 热路径**分离。

### 0.6 生态参照：不止「裸 verbs」

下列栈 **底层仍是 verbs / 内核 RDMA**，但对并发与进度引擎做了封装，属于 **业界「成品」路径**，Rust FFI 方案常在 **对齐 perftest** 或 **自研线程模型** 之间选型：

- **UCX / OpenMPI / MVAPICH** 等：**progress engine**、worker、多线程调度由库完成；应用很少直接写 `**while (poll_cq)`**。
- **SPDK、各类分布式存储**：常见 **固定 polling thread + 无锁队列** 跨模块投递请求。
- **GPU Direct / GDRCopy**：在 **CQ 路径与 GPU stream** 之间再做一层并行与同步，但仍建立在 **verbs in-flight** 之上。

### 0.7 小结（对照后文 Rust）

C 侧高并发 **一般不表述为「我上了一个 Proactor 框架」**，而是：**深队列 + 批 post + 选对 CQ 策略 + 用多 QP/多线程 partition 把锁拿走**。后文各 Rust 栈 **多数沿用同一套物理模型**；`**async-rdma`** 一类则是在此之上 **再叠一层 Rust 异步运行时编排**。

---

## 1. 通用原理：高并发在 RDMA 里通常指什么


| 维度                   | 含义                | 常见手段                                                                    |
| -------------------- | ----------------- | ----------------------------------------------------------------------- |
| **操作并发度（in-flight）** | 同一时间未完成的发送/接收请求数量 | 增大 SQ/RQ depth、`tx_depth`、WR 链、`ibv_wr_*` 批量发帖                          |
| **CPU / 线程并行**       | 多核一起干活            | 每线程独占 QP+CQ（partition）；或少量「CQ 线程」+ 多「业务发帖线程」（需严谨同步）                     |
| **完成路径策略**           | 如何消化 CQ           | **忙轮询** `poll_cq` / CQ EX（低延迟、吃 CPU）；**阻塞 / epoll** CQ fd（省 CPU、延迟通常更高） |
| **连接 / QP 扇出**       | 多连接并行             | 多 QP、多 CQ，避免单锁热点                                                        |


以下各方案 **不在 FFI 层替你选定**唯一模型（除 `async-rdma` 明确集成异步运行时外）；工程上多为 **「 verbs 同步 API + 自家线程 / channel / async 胶水」** 的组合。

---

## 2. jonhoo/rust-ibverbs（`ibverbs-sys` + `ibverbs`）

### 2.1 线程模型（栈本身）

- `**ibverbs-sys`：** 纯 FFI + 类型，**无**线程或运行时假设。
- `**ibverbs`：** 同步 safe API；资源封装（如 `QueuePair`）可在多线程间传递与否取决于 **内部是否 `Send`/`Sync`** 及你是否共享可变状态——**栈不内置 reactor/async executor**。

**结论：** 与 **手写 C ibverbs** 等价：**线程模型 100% 由应用决定**，常见是 **专用线程 poll CQ** 或 **thread-per-core**。

### 2.2 高并发用法建议

1. **数据路径：** 绑核线程内 `**busy poll` CQ** + `**post_send` 批量**（若仍走旧 API，可用 WR `next` 链；或直接在 `-sys` 层自行扩展调用 modern verbs——需注意该栈偏向经典路径）。
2. **控制路径：** `fork`/单线程建立 QP/MR，建好后再把句柄交给数据线程（避免在热点线程里反复 `ibv_reg_mr`）。
3. **跨线程：** 完成线程通过 `**crossbeam-channel` / `std::sync::mpsc`** 把「完成事件摘要」发给业务；或对每条连接 partition，**无共享可变 QP**。
4. **勿假定「封装了 magically async」**：要 `**Future`** 需自叠 `**tokio::task::spawn_blocking**` 或独立 `**poll**` 线程 + channel。

### 2.3 小结

**同步栈；高并发 = C 套路 + Rust 类型安全。** 适合教学、与 C 代码思维对齐、或你已有一套线程框架。

---

## 3. DatenLord：`rdma-sys` + `async-rdma`

### 3.1 线程模型

- `**rdma-sys`：** 同步 `unsafe` verbs（手写 inline）；**无**异步运行时。
- `**async-rdma`：** 在 `**rdma-sys`** 之上提供 **异步 / 面向 Tokio 等** 的抽象（连接、内存、队列等），意图是把 **阻塞型控制路径与可异步化的编排** 放进 `**async`** 世界。

**结论：** 这是五条主线里 **唯一把「异步封装」作为一等公民产品边界** 的路线（名称即意图）。

### 3.2 高并发用法建议

**若主要用 `async-rdma`：**

1. **与其它 Tokio IO 统一：** HTTP/RDMA 同一 runtime里 `**spawn` 多个任务**；注意 `**blocking`** 类 verbs（若封装层未完全 offload）应用 `**spawn_blocking**` 或专用 `**blocking` pool**，避免堵 executor。
2. **CQ / poll：** 需阅读 `**async-rdma` 实现**：常见模式是 **后台任务或 blocking 线程** 专职 `**poll_cq`**，再通过 `**oneshot`/`channel**` 驱动上层 `**Future**` 完成。
3. **纯 `rdma-sys`、不用 async：** 与 §2 相同——**同步 + 自家线程**，适合只想 FFI、不要 Tokio 的组件。

### 3.3 小结

**双模式：** `**rdma-sys` → 同步高并发（等同 C）**；`**+ async-rdma` → 异步编排与高并发任务并行**。选型取决于是否要 **融入异步生态**。

---

## 4. `rdma-mummy-sys` + Sideway

### 4.1 线程模型（栈叙述与设计取向）

- `**rdma-mummy-sys`：** FFI 边界（桩 + dlopen），**无**运行时线程假设。
- `**sideway`：** **同步 API**；公开文章明确 **不把 verbs 硬焊进 Tokio**，倾向 `**thread-per-core`**、专用 RDMA worker、与其它部分 `**channel` 通信**。

**结论：** **官方叙事是同步数据面 + 显式线程划分**，异步留给未来或自建上层。

### 4.2 高并发用法建议

1. **充分利用 CQ/QP Ex、`ibv_wr_*`：** 栈面向现代 verbs，**批发帖 + EX poll** 与 **stride/perftest 对齐** 的思路一致。
2. **每核 worker：** 每线程绑定 **独立或分区 QP/CQ**，热点路径 **避免 `Arc`/锁**；资源用 `**Arc`** 仅为跨线程共享注册对象的便利（ refcount 不在最热 poll 循环里打转）。
3. **控制面可观测错误：** 高并发下调试 QP 状态失败时，利用 `**sideway` 结构化错误**（mask/GID）缩短 MTTR。
4. **若强行 Tokio：** 同 jonhoo——**专用阻塞线程 poll**，勿在 `**async` 任务里忙等 spin**。

### 4.3 小结

**高性能同步栈；高并发 = thread-per-core + EX API + 深队列**，与 C **perftest** 哲学接近。

---

## 5. rust-rdma-io（`rdma-io-sys` + `rdma-io`）

### 5.1 线程模型

- `**rdma-io-sys`：** FFI（wrapper + 预生成绑定），**无**线程模型。
- `**rdma-io`：** 同步偏重的 IO/verbs 抽象（以仓库文档为准），**不宣称**与某一 async runtime 绑定。

**结论：** 应用侧通常 **同步 + 自选线程**，或与现有 `**async` 胶水层** 自行集成。

### 5.2 高并发用法建议

1. **数据路径：** 与 §2、§4 相同——**poll 线程 + batch + 深队列**。
2. **绑定维护：** 新增热点 API 需 **改 `wrapper.c` + 再生成 bnd**；高并发场景优先保证 `**rdma_wrap_*` 覆盖你的 WR builder 路径**。
3. **混合栈：** 适合已有 **windows-bindgen/bnd** 流程或与 Windows/Linux 双平台 FFI 对齐的项目。

### 5.3 小结

**FFI 确定性 + 同步 verbs；并发模型自建**，与 jonhoo 同属「库不管 executor」。

---

## 6. SF-Zhou / `ruapc-rdma-sys`

### 6.1 线程模型

- `**ruapc-rdma-sys`：** **窄 sys**，面向 device/query/port attr、`post`/`poll` 等 **子集**，带 **serde/schema**。
- **上层 ruapc：** 线程/async 由 **主工程其它 crate** 决定；**本 crate 不提供完整 verbs 框架**。

**结论：** 常见于 **控制平面 / 运维工具 / Agent**：可能在 `**async` CLI**（如 `**tokio::main`**）里偶尔调 verbs，也可能 **单线程脚本**。

### 6.2 高并发用法建议

1. **认清边界：** allowlist **未覆盖**的路径不要用本 crate **硬撑**；数据面-heavy 应换 `**rdma-sys` / sideway / jonhoo**。
2. **若仅在管理面并发：** 多线程 `**query` device** 时注意 `**ibv_context` 线程安全**契约（与 C 相同：按对象文档使用）。
3. **与 async：** 适合 `**spawn_blocking`** 包一层阻塞 `**ibv_***`，短时间调用返回 `**Future**`。

### 6.3 小结

**偏 FFI + 类型便利；高并发 RDMA 数据面通常不是此 crate 的主战场**。

---

## 7. Rust：使用异步 vs 不使用异步 —— 典型方案与场景

### 7.1 不使用异步（同步 + 线程）

**典型方案**

- `**thread-per-core` + busy poll CQ**（存储、KV、自定义 RPC 引擎常见）。
- **专用 CQ 线程 + `channel`** 向业务线程投递完成事件（折中 CPU 与延迟）。
- `**crossbeam`/`mpmc` 队列** 在多生产者线程与单 poll 线程之间传递 WR 描述符（需谨慎设计背压）。

**适用场景**

- **延迟敏感**、尾延迟要稳（避免 runtime 抢占抖动）。
- **数据路径已占满 CPU**（polling），`async` 无赢家。
- **栈本身仅提供同步 API**（jonhoo、mummy/sideway、rust-rdma-io、`rdma-sys` 裸用）。
- **与 C/C++ 既有线程模型对齐**，便于对照 **perftest**。

**代价：** 自建线程、背压、取消（cancellation）要手写；与 **HTTP/gRPC 全 async** 服务拼接时要 **边界清晰**（专用线程 + channel）。

### 7.2 使用异步（`async`/`await` + Tokio 等）

**典型方案**

- `**async-rdma` 一类**：连接建立、内存注册流程 `**await`**；CQ 驱动 `**Future**`（具体以 crate API 为准）。
- **自集成：** `**tokio::task::spawn_blocking`** 包裹 `**poll_cq` 循环**，通过 `**watch`/`broadcast`** 唤醒上层任务。
- **混合：** RDMA 数据面 **同步线程**，控制面与网络 IO **Tokio**（常见于网关）。

**适用场景**

- 已有 **全异步服务**（微服务、网关、控制面），希望 **统一 `Future` 模型**。
- **大量并发连接/会话** 的 **编排**（非单 pole），愿意用 **调度器**换 **开发效率**。
- **阻塞型 rdmacm/verbs** 调用需与 **其它 async IO** 共存时，用 `**spawn_blocking`** 或封装 crate 消化。

**代价：** executor 调度和 `**blocking` 陷阱**；极低延迟路径往往仍需 **脱离 async 或 isolate 到 blocking pool**。

### 7.3 决策简表


| 场景侧重                               | 更倾向                                     |
| ---------------------------------- | --------------------------------------- |
| 对标 **perftest**、打满带宽、微秒级延迟         | **同步 + poll + 绑核**                      |
| **Tokio/Axum** 等与 RDMA **同进程**     | `**async-rdma` 或 blocking 封装**          |
| **窄设备管理 / CLI / Agent**            | `**ruapc-rdma-sys` + 任选 sync/async 外壳** |
| **现代 verbs + 清晰错误 + 无意引入 runtime** | **Sideway（同步）+ 自建线程**                   |


---

## 8. 与「Proactor」说法的关系（澄清）

经典 `**libibverbs`** 多为：**发起异步硬件操作（post）→ 在 CQ 上同步或阻塞地「收割」完成**。这与 textbook **Proactor**（异步完成后回调处理器）在「完成队列驱动」上有相似处，但社区实现通常描述为 **CQ-driven / polling**，而非必须实现完整 Proactor 框架。**高并发关键仍是 in-flight、批处理与线程划分**，而非是否使用 `**async` 关键字**（与 **§0** 中 C 侧归纳一致）。

---

## 9. 参考文献与延伸阅读

- 各 FFI 专题：`bindgen-vendored-headers-fnptr-in-safe-layer.md` 等（本目录）。
- **§0** 所述业界范式：**Linux `perftest`**、厂商 RNIC 调优文档、UCX/MPI 等上层库（底层仍多为 verbs + 自有 progress）。
- Sideway 团队博文（知乎 / rdma-rust.github.io）：同步取向、`thread-per-core`、与 Tokio 取舍。

---

## 10. 附录：C 侧代码示意（直观理解「高并发」）

下列片段为 **教学示意**（变量如 `buf`、`mr_lkey`、`remote_addr`、`rkey` 需自行补齐），重点展示 **post / poll** 的形状，而非可编译的完整程序。**代码块内注释**标明：**传输未完成（仅 post 成功）**、**轮询 CQ 收割事件**、**在 wc 上处理已完成/失败** 等位置（后文 **§12–§13** 的 Rust 示意同样遵循这一注释约定）。

### 10.1 单线程：深队列 + 忙轮询（perftest 范式）

**含义：** CPU 上仍是 **单线程顺序执行**；**并行主要发生在网卡**——SQ 里同时挂大量未完成 WR。线程在一个循环里 **灌水（post）** 与 **抽水（poll_cq）**。

```c
/* 示意：单线程 benchmark / 专用 IO 线程 */
#define BATCH 32
#define DEPTH 1024

void rdma_hot_loop(struct ibv_qp *qp, struct ibv_cq *cq) {
    struct ibv_send_wr wr[BATCH], *bad_wr = NULL;
    struct ibv_sge sge[BATCH];
    struct ibv_wc wc[DEPTH];   /* poll 一次最多取 DEPTH 条 CQE，缓冲在此 */
    uint64_t next_wr_id = 1;

    for (;;) {
        /*
         * ----------「未完成」阶段（仍在网卡/SQ 路径上，尚未出现在 CQ）----------
         * ibv_post_send 返回 0 只表示 WR 已写入 SQ（或进一步交给 HW），
         * 不表示 RDMA 已做完、也不表示已有 CQE。
         * 真正「传输未完成」时没有单独回调；未完成 = 该 wr_id 对应的 CQE 还没被 poll 到。
         * 它每轮都提交 32 个 WR，然后只是把当前 CQ 里已有的 CQE 清掉；如果网卡还没完成，下一轮又继续提交 32 个。
         * 最终可能把 SQ 打满，ibv_post_send() 失败。
         */
        for (int i = 0; i < BATCH; i++) {
            memset(&wr[i], 0, sizeof(wr[i]));
            sge[i].addr   = (uint64_t)buf + (size_t)i * 4096;
            sge[i].length = 4096;
            sge[i].lkey   = mr_lkey;
            wr[i].wr_id       = next_wr_id++;
            wr[i].sg_list     = &sge[i];
            wr[i].num_sge     = 1;
            wr[i].opcode      = IBV_WR_RDMA_WRITE;
            /* 仅最后一笔打 SIGNALED：减少 CQE 数量；其它 WR 完成时不单独产生 CQE（示意） */
            wr[i].send_flags  = (i == BATCH - 1) ? IBV_SEND_SIGNALED : 0;
            wr[i].wr.rdma.remote_addr = remote_addr;
            wr[i].wr.rdma.rkey        = rkey;
            wr[i].next = (i < BATCH - 1) ? &wr[i + 1] : NULL;
        }
        if (ibv_post_send(qp, &wr[0], &bad_wr))
            break; /* post 失败：本地队列/参数问题，这里跳出；不是「传输未完成」语义 */

        /*
         * ---------- 轮询 CQ：批量取出「当前已经上报」的完成事件 ----------
         * ibv_poll_cq：非阻塞，立刻返回当前 CQ 里至多 n 条 wc；
         * 内层 while 反复 poll，直到一次 poll 返回 0（此刻 CQ 上暂时没有新完成可读）。
         * 这才是「轮询所有当前可取到的完成事件」的位置。
         */
        int n;
        while ((n = ibv_poll_cq(cq, DEPTH, wc)) > 0) { // 最多取1024个，如果有完成队列的，那么就处理，如果没有，就继续发送32个
            for (int i = 0; i < n; i++) {
                /*
                 * ----------「传输完成」与失败的处理 ----------
                 * wc[i]：每条对应一个已消费的 CQE（因上面 SIGNALED 策略，并非每个 WR 都有 CQE）。
                 * IBV_WC_SUCCESS：该 WR 从 HW 视角已完成（可做计数、回收信用、触发上层逻辑）。
                 * 非 SUCCESS：错误完成，同样在此处处理（重连、打日志、断言等）。
                 */
                if (wc[i].status != IBV_WC_SUCCESS) {
                    /* 传输失败 / 出错完成：错误处理 */
                } else {
                    /* 传输成功完成：例如根据 wc[i].wr_id 对齐业务请求 */
                    (void)wc[i].wr_id;
                }
            }
        }
        /* 跳出上面 while 表示：此刻 CQ 里没有更多可读完成（可能仍有 WR 在飞，尚未产生 CQE） */
    }
}
```

- `rdma_hot_loop` 有两种调用方式：
  - 直接在 main 调用（演示，demo，单任务benchmark）
  - 在独立线程中调用，主线程做其他事情，**放进 thread** = 要并行其它工作时用；生产里 **绑核 IO 线程** 很常见。



### 10.2 多线程：每线程独占 QP + CQ（分区优先）

**含义：** **多核 CPU 并行**；每线程只碰自己的 **QP/CQ**，避免多写同一 QP 带来的锁与verbs线程安全问题。

```c
#include <pthread.h>

/* 每个 worker 独占 qp/cq，多线程之间不在同一 QP 上并发 post，避免锁 */

struct worker_ctx {
    int id;
    struct ibv_qp *qp;
    struct ibv_cq *cq;
};

static void *worker_thread(void *arg) {
    struct worker_ctx *w = arg;
    for (;;) {
        /*
         * 「未完成」阶段：构造 WR 并 ibv_post_send。
         * 返回成功仅表示 WR 进入本线程 QP 的发送路径；DMA 完成时机未知。
         */
        struct ibv_send_wr wr = {0};
        struct ibv_sge sge = {
            .addr = /* ... */, .length = 4096, .lkey = mr_lkey,
        };
        wr.wr_id = (uint64_t)w->id;
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_SEND;
        wr.send_flags = IBV_SEND_SIGNALED; /* 本示例每笔都 signaling，每笔完成应有 CQE */
        struct ibv_send_wr *bad = NULL;
        ibv_post_send(w->qp, &wr, &bad);

        /*
         * 「轮询当前可取到的完成事件」：只 drain **本线程**绑定的 cq。
         * while 直到 ibv_poll_cq 返回 0，表示此刻该 CQ 上没有更多待读 CQE。
         */
        struct ibv_wc wc[64];
        int n;
        while ((n = ibv_poll_cq(w->cq, 64, wc)) > 0) {
            for (int i = 0; i < n; i++) {
                /* 「传输完成 / 失败」：在此根据 wc[i].status / wr_id 推进业务 */
                if (wc[i].status != IBV_WC_SUCCESS) { /* 错误完成 */ }
            }
        }
    }
    return NULL;
}
```

### 10.3 可选：completion channel —— 阻塞等单位时间内省 CPU

与忙轮询对照：**等内核通知**再 `poll_cq`，更适合「连接多、不能绑死 CPU」的场景，延迟通常高于 spin。

```c
void cq_event_loop(struct ibv_cq *cq, struct ibv_comp_channel *ch) {
    for (;;) {
        struct ibv_cq *ev_cq = NULL;
        void *cq_ctx = NULL;

        /*
         * 「阻塞等待事件」：没有完成时线程睡眠，不占满 CPU。
         * 与 §10.1 忙轮询相对；内核/驱动在有新 CQE 可读倾向时唤醒这条路径。
         */
        if (ibv_get_cq_event(ch, &ev_cq, &cq_ctx))
            break;

        /* 确认消费事件计数，避免 completion channel 背压 */
        ibv_ack_cq_events(cq, 1);

        /*
         * 「轮询收割」：被唤醒后仍需 ibv_poll_cq 把 wc 拷出；
         * 内层 while 尽量 drain 本次可读的所有 CQE。
         */
        struct ibv_wc wc[64];
        int n;
        while ((n = ibv_poll_cq(cq, 64, wc)) > 0) {
            for (int i = 0; i < n; i++) {
                /* 「传输完成 / 失败」：同 §10.1，在此处理 wc[i] */
            }
        }
    }
}
```

### 10.4 与「Redis 式单线程」的类比（澄清）


| 相像之处                   | 差异                                                                       |
| ---------------------- | ------------------------------------------------------------------------ |
| **单线程 own 全流程**，少锁、好推理 | RDMA 热路径常是 `**post` + `poll_cq`（忙等 CQ）**，不是 `**epoll_wait` 等 socket 可读** |
| 都可在一个 loop 里搞定「发」与「收成」 | Redis：**事件驱动 + 命令执行**；RDMA：**硬件队列 pump/drain + in-flight 叠在网卡**          |


因此：**可以说风格上像「单线程包圆」**；但 **不能说「和 Redis 一样就是 epoll 轮询」**——benchmark 型 RDMA 往往是 **CPU spin + 深 SQ**，语义不同。

---

## 11. 附录：Rust FFI 是否要做 async、要不要多套 runtime？

### 11.1 FFI 层要不要 async？

- `**-sys` / 薄 FFI：** **不必**。verbs 数据面本质是 **同步 API + CQ 异步完成**，用 `**unsafe fn` / 同步**最贴切。
- **产品层要打进 Tokio 生态：** 可 **单独**提供 `**async` crate**（如 DatenLord `**async-rdma`**），与 `**rdma-sys**` 分层——这是 **产品线选择**，不是 FFI 必备。

### 11.2 要不要「多个 async runtime」？

**默认不建议**在同一进程跑多套 Tokio（或多套 executor）：边界复杂、线程池重复、尾延迟难控。

**常见正经组合：**


| 模式               | 做法                                                                                                |
| ---------------- | ------------------------------------------------------------------------------------------------- |
| **零 runtime**    | 数据面 `**std::thread`** + `**poll_cq**`                                                             |
| **单 Tokio + 桥接** | `**spawn_blocking`** 做低频 `**reg_mr` / 建 QP**；**专用线程**跑 CQ，经 `**channel` / `Notify`** 与 async 任务通信 |
| **强隔离**          | **多进程**各启 runtime，而不是 **单进程多 Tokio**                                                              |


### 11.3 何时 async「值得」？

- 应用已在 **Axum / Tonic / Tokio TCP** 里写了全套 `**Future`**，希望 **控制流统一**。
- **大量会话编排**、定时器、与别的 IO `**select!`**，同步线程 + 手写状态机成本过高。
- **能接受**：热点 `**poll`** 仍在 blocking 线程或 `**spawn_blocking` pool**，executor **不负责 spin**。

---

## 12. 附录：Tokio TCP 与 RDMA 语义迁移（示意 Rust）

### 12.1 TCP vs RDMA（为何要「换语义」）


| TCP（Tokio 习惯）                  | RDMA（FFI）                                             |
| ------------------------------ | ----------------------------------------------------- |
| `AsyncRead` / `AsyncWrite` 字节流 | **QP + MR**；须 **注册缓冲区**并与对端交换 `**rkey` / VA / QP 信息** |
| `write().await`                | `**post_send`**：只进 SQ；**完成在 CQ**                      |
| `TcpStream::connect`           | 常用：**TCP（或 rdma_cm）换元数据**，数据再走 **WRITE/SEND**         |


### 12.2 推荐架构：**Tokio 管 TCP + 独立线程管 verbs**

1. `**TcpStream`**：**异步**交换握手（JSON/protobuf/自定二进制），对齐双方 MR/QP 信息。
2. `**std::thread`**：**只做 `poll_cq` + `post`**（或 CQ EX），**不 `.await`**。
3. **Tokio 任务**：经 `**mpsc` / `oneshot` / `Notify`** 向引擎发「逻辑发送」、等待「逻辑完成」。

### 12.3 示意代码（结构级，不可直接编译；注释标明 TCP 与 RDMA 线程分工）

```rust
//! 示意：TCP 控制面在 Tokio，RDMA 数据面在 OS 线程。
//! 真实工程需补齐 PD/QP/MR、modify_qp、错误处理等。

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::oneshot;

/// 从 Tokio 投递到 `rdma_engine` 的逻辑命令；携带 oneshot 用于「传输完成」后唤醒 async 侧。
enum RdmaCmd {
    Write {
        wr_id: u64,
        reply: oneshot::Sender<Result<(), &'static str>>,
    },
}

/// **RDMA 引擎线程**：只做 try_recv 命令 + post/poll，**禁止 `.await`**。
fn rdma_engine(mut rx: std::sync::mpsc::Receiver<RdmaCmd>) {
    loop {
        /* 先尽力消化队列里的待发命令（「未完成」起点：即将 ibv_post_send） */
        while let Ok(cmd) = rx.try_recv() {
            match cmd {
                RdmaCmd::Write { wr_id: _, reply } => {
                    // unsafe { ibv_post_send / ibv_wr_* … }
                    /*
                     * 示意直接 send(Ok)：真实代码应在 **poll 到对应 wr_id 的 CQE 且 SUCCESS**
                     * 之后再 `reply.send`，否则 async 侧会误以为已完成。
                     */
                    let _ = reply.send(Ok(()));
                }
            }
        }
        /*
         * 「轮询 CQ」：非阻塞 drain；根据 wc 匹配 wr_id，再给尚未满足的 oneshot send。
         * unsafe { ibv_poll_cq(...); }
         */
    }
}

#[tokio::main]
async fn demo() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    /* 启动引擎 OS 线程 */
    let (tx, rx) = std::sync::mpsc::channel();
    std::thread::spawn(move || rdma_engine(rx));

    /* ---- TCP：异步握手，换 RDMA 元数据（仍在 Tokio runtime）---- */
    let mut tcp = TcpStream::connect("peer:9000").await?;
    let mut meta = [0u8; 256];
    tcp.read_exact(&mut meta).await?;
    tcp.write_all(b"my_rdma_handshake").await?;

    /* ---- 发起一次逻辑「RDMA 写」：发到引擎线程 ---- */
    let (reply_tx, reply_rx) = oneshot::channel();
    tx.send(RdmaCmd::Write {
        wr_id: 1,
        reply: reply_tx,
    })?;

    /* 「等待完成」：阻塞交给 oneshot，executor 可调度其它任务 */
    reply_rx.await??;

    /* 低频阻塞 verbs（如 reg_mr）可用 spawn_blocking，勿放在热点 post 路径 */
    // tokio::task::spawn_blocking(|| unsafe { ibv_reg_mr(...) }).await?;

    Ok(())
}
```

**要点：** **不要**在每个热点 `**post`** 上都 `**spawn_blocking**`；只适合 **初始化 / 偶尔控制调用**。

---

## 13. 附录：四种数据传输形态的 Rust 上层示意（2×2 矩阵）

本节回答两个问题：**（a）从零写还是用 TCP 迁移；（b）上层要不要 async**。四种组合互相独立，代码均为 **结构示意**，占位 `**unsafe { ibv_* /* … */ }`** / `**Conn**` / `**payload**`，**不可直接编译**。**每个代码块均含中文注释**，对应 **未完成 / post / poll / 完成通知（oneshot）** 等语义。

### 13.1 矩阵说明


|                                                                                 | **不使用 async 封装**（同步上层：`main` + `std::thread` + channel） | **使用 async 封装**（Tokio / `Future`，verbs 仍在阻塞线程池或引擎线程） |
| ------------------------------------------------------------------------------- | ------------------------------------------------------- | ---------------------------------------------------- |
| **从 0 开始**：整条链路假定你已建好 QP/MR，只做「发载荷 → 等 CQ」                                      | **§13.2 实例 A**                                          | **§13.3 实例 B**                                       |
| **TCP → RDMA**：原先逻辑走 `**TcpStream::write/read`**，迁移后 **TCP 只换握手**，载荷改 **verbs** | **§13.4 实例 C**                                          | **§13.5 实例 D**                                       |


**共通占位：** `Conn` 表示你已持有的上下文（PD/QP/CQ/MR）；`**post_then_spin_until`** / `**rdma_engine**` 代替真实 `**rdma-sys`/`sideway`/`async-rdma**` 调用。

### 13.2 实例 A —— 从零 · 同步（单进程「收发同源」，偏 benchmark）

「从零」指：**不显式依赖 TcpStream 传输载荷**，假设两端 QP、缓冲区已通过别的路径建好。

```rust
//! §13.2：从零 · 纯同步：典型是一条线程内 post + poll，或多个线程分区 QP。
//! cargo deps：仅需 std（无 tokio）。

use std::sync::mpsc;

/// **占位**：内部应为 `unsafe { ibv_post_send + 循环 ibv_poll_cq }` 直到目标 wr 完成或出错。
/// - **未完成**：post 成功到 poll 到 CQE 之间，无单独回调。
/// - **完成**：在 poll 到的 `wc` 分支里处理。
fn post_then_spin_until(_conn: &mut Conn, payload: &[u8]) -> Result<(), ()> {
    // unsafe { ibv_post_send / ibv_wr_* ，绑定本地 MR … }
    // loop { unsafe { ibv_poll_cq(...) } until wc matches wr_id }
    let _ = payload;
    Ok(())
}

pub fn main() {
    /* 路径一：单线程直传（同 §10.1 哲学） */
    let mut conn = Conn::bootstrap();
    let payload = vec![7u8; 4096];
    post_then_spin_until(&mut conn, &payload).expect("xfer");

    /*
     * 路径二：专用线程消费 channel —— 主线程只 send 缓冲区描述，
     * 子线程内 post + poll，避免与别的模块共享 QP。
     */
    let (cmd_tx, cmd_rx) = mpsc::channel::<Vec<u8>>();
    std::thread::spawn(move || {
        let mut eng = Conn::bootstrap();
        for chunk in cmd_rx {
            /* 每个 chunk：一次「未完成→轮询→完成」闭环（示意） */
            let _ = post_then_spin_until(&mut eng, &chunk);
        }
    });
    cmd_tx.send(vec![9u8; 4096]).unwrap();
}

struct Conn;
impl Conn {
    fn bootstrap() -> Self {
        Conn /* PD/QP/CQ/MR … */
    }
}
```

**对照语义：** 与 **§10.1** 的 C 单线程循环同一哲学：**无 `.await`**。

---

### 13.3 实例 B —— 从零 · async（Tokio 编排 + 引擎线程）

仍从零：**不设 TcpSession**。异步体现在：**Tokio 任务 `await` 完成**，底层 `**poll_cq` 在引擎线程** 收到 `**wc`** 后 **填满 `oneshot`**。

```rust
//! §13.3：从零 · Tokio：数据路径仍是引擎线程 poll；Future 只包住「逻辑传输请求」。
//! cargo：`tokio` with macros。

use tokio::sync::oneshot;

/// 异步侧发来的负载 + **完成信号通道**（poll 到成功/失败后再 send）。
enum Cmd {
    Xfer(Vec<u8>, oneshot::Sender<Result<(), &'static str>>),
}

fn rdma_engine(rx: std::sync::mpsc::Receiver<Cmd>) {
    let mut conn = Conn::bootstrap();
    loop {
        /* 处理待发：post 后 WR 进入「未完成」 */
        while let Ok(Cmd::Xfer(blob, reply)) = rx.try_recv() {
            match post_then_spin_until(&mut conn, &blob) {
                /* 「传输完成」示意：真实应在 wc SUCCESS 分支 reply.send(Ok(())) */
                Ok(()) => {
                    let _ = reply.send(Ok(()));
                }
                /* post 失败或 poll 到错误完成 */
                Err(_) => {
                    let _ = reply.send(Err("xfer"));
                }
            }
        }
        /* 「轮询 CQ」可与上面合并；此处 spin drain 未关联 reply 的 wc */
        // unsafe { ibv_poll_cq … }
    }
}

#[tokio::main]
async fn main() {
    let (tx, rx) = std::sync::mpsc::channel::<Cmd>();
    std::thread::spawn(move || rdma_engine(rx));

    let (done_tx, done_rx) = oneshot::channel();
    tx.send(Cmd::Xfer(vec![1u8; 4096], done_tx)).unwrap();

    /* 「等待传输完成」—— 仅此处进入 async 等待；poll 仍在引擎线程 */
    done_rx.await.unwrap().unwrap();
}

fn post_then_spin_until(_conn: &mut Conn, _payload: &[u8]) -> Result<(), ()> {
    Ok(())
}

struct Conn;
impl Conn {
    fn bootstrap() -> Self {
        Conn
    }
}
```

若改用 `**async-rdma**`，可把 `**Xfer**` 换成库的 `**post_*.await**`（具体 API 以该 crate 为准），本节强调的是：**executor 不负责 spin**，spin 仍在下层。

---

### 13.4 实例 C —— TCP → RDMA · 同步（迁移对照）

**场景：** 原先 **整条链路都是阻塞 TCP**，现在要保留 `**TcpStream::connect/read/write`** 建链与小帧握手，**真实载荷改走 verbs**。下面是「迁移前后」同一逻辑的骨架对照。

```rust
//! §13.4：TCP → RDMA · 同步。注释里的 OLD 表示迁移前的等价写法。

use std::io::{Read, Write};
use std::net::TcpStream;
use std::sync::mpsc;

fn migrate_tcp_to_rdma_blocking(peer: &str) -> std::io::Result<()> {
    /* 阻塞 TCP 连接：用于后续带外握手 */
    let mut tcp = TcpStream::connect(peer)?;

    // OLD（迁移前）：整段 payload 走 TCP 字节流
    // let mut payload = vec![0u8; 65536];
    // tcp.read_exact(&mut payload)?;
    // tcp.write_all(&payload)?;

    /*
     * NEW：TCP 仅传输 **RDMA 元数据**（对端 rkey、VA、QP 编号等），
     * **大块数据不再 read_exact/write_all 在 TCP 上**。
     */
    let mut handshake = [0u8; 128];
    tcp.read_exact(&mut handshake)?;
    tcp.write_all(b"READY_META_FROM_ME")?;

    /* 可选：把 tcp 移到后台线程或持有 guard，避免握手后立刻 drop 连接 */
    let (_tcp_guard_tx, _tcp_guard_rx) = mpsc::channel::<()>();

    /*
     * **载荷走 verbs**：根据 handshake 构造 Conn，再 post + poll 直至完成。
     * 「未完成 / 轮询 / 完成」均在 post_then_spin_until 内部（占位）。
     */
    let payload = vec![3u8; 65536];
    let mut conn = Conn::from_handshake(&handshake /* … */);
    post_then_spin_until(&mut conn, &payload).map_err(|_| {
        std::io::Error::new(std::io::ErrorKind::Other, "rdma xfer")
    })?;

    Ok(())
}

fn post_then_spin_until(_conn: &mut Conn, _payload: &[u8]) -> Result<(), ()> {
    Ok(())
}

struct Conn;
impl Conn {
    fn bootstrap() -> Self {
        Conn
    }

    fn from_handshake(bytes: &[u8]) -> Self {
        let _ = bytes;
        Conn
    }
}
```

**迁移要点：** 业务 `**read_exact` / `write_all` 大块** → `**conn + MR`**；**TCP socket** 降级为 **带外 bootstrap**，仍可顺带 ACK。

---

### 13.5 实例 D —— TCP → RDMA · async（Tokio 保留 TcpStream + Future）

与 **§13.4** 同一迁移语义，但 **握手放在 Tokio**，verbs **仍在阻塞线程**；适合 `**#[tokio::main]`** 里已有 `**TcpStream**` 的工程最小侵入改动。

```rust
//! §13.5：TCP → RDMA · Tokio：TcpStream 用 `.await`，RDMA 用 §13.3 同款 Cmd。

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream as AsyncTcp;
use tokio::sync::oneshot;

enum Cmd {
    Xfer(Vec<u8>, oneshot::Sender<Result<(), &'static str>>),
}

fn rdma_engine(rx: std::sync::mpsc::Receiver<Cmd>) {
    let mut conn = Conn::bootstrap();
    loop {
        while let Ok(Cmd::Xfer(blob, reply)) = rx.try_recv() {
            match post_then_spin_until(&mut conn, &blob) {
                Ok(()) => {
                    /* 示意：应在 CQ SUCCESS 后 reply；此处同 §13.3 */
                    let _ = reply.send(Ok(()));
                }
                Err(()) => {
                    let _ = reply.send(Err("xfer"));
                }
            }
        }
        /* 轮询 CQ，处理「已完成但未在上面对齐 reply」的 wc（生产需 wr_id→reply 映射表） */
        // ibv_poll_cq …
    }
}

#[tokio::main]
async fn migrate_tcp_to_rdma_async(peer: &str) -> Result<(), Box<dyn std::error::Error>> {
    let (cmd_tx, cmd_rx) = std::sync::mpsc::channel::<Cmd>();
    std::thread::spawn(move || rdma_engine(cmd_rx));

    /* Tokio 上异步建 TCP */
    let mut tcp = AsyncTcp::connect(peer).await?;

    // OLD：payload 全走 TcpStream —— 已省略

    /* **异步握手**：换 RDMA 参数；载荷不走这两次 read/write 之外的 TCP */
    let mut handshake = [0u8; 128];
    tcp.read_exact(&mut handshake).await?;
    tcp.write_all(b"READY_META_FROM_ME").await?;

    /* **异步等待 RDMA 传输结束**：引擎线程 poll 完成后触发 oneshot */
    let blob = vec![5u8; 65536];
    let (tx_done, rx_done) = oneshot::channel();
    cmd_tx.send(Cmd::Xfer(blob, tx_done))?;
    rx_done.await.unwrap().unwrap();

    Ok(())
}

struct Conn;
impl Conn {
    fn bootstrap() -> Self {
        Conn
    }
}

fn post_then_spin_until(_conn: &mut Conn, _payload: &[u8]) -> Result<(), ()> {
    Ok(())
}
```

---

### 13.6 小结（选型口诀）


| 组合               | 适用直觉                                                         |
| ---------------- | ------------------------------------------------------------ |
| **A 从零·同步**      | 对齐 **perftest / C**，最少运行时依赖                                  |
| **B 从零·async**   | 进程内仍有 Tokio，但仍要把 `**poll_cq` 隔离出 executor**                  |
| **C TCP→·同步**    | **CLI / blocking server**，TCP handshake → verbs bulk         |
| **D TCP→·async** | **现有 Axum/gRPC worker**，TcpStream `**await`**，verbs **线程桥接** |


更多语义对照仍见 **§12**；C 侧形状对照 **§10**。

---

*各上游 crate API 随版本变化；生产选型请以对应仓库 README、examples 与 CHANGELOG 为准。*