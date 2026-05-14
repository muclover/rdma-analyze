# Rust RDMA 相关 FFI 方案速览与对比

本目录按 **「如何把 libibverbs（及可选 librdmacm）接到 Rust」** 拆分文档。索引不仅列出文件名，还对 **FFI 边界、架构分层、inline/链接策略** 做简要说明；细节见各专题文档正文。**各方案的线程模型、高并发 RDMA 实践，以及 Rust 同步 / 异步选型**见 [concurrency-threading-async-vs-sync.md](./concurrency-threading-async-vs-sync.md)。

**与仓库根 `ffi-docs` 的关系**：若只需一份压缩总览（五条主线对照、与 `rust-ffi-project` 通用样本的心智映射），见 [ffi-docs/rdma-ffi-schemes.md](../../../ffi-docs/rdma-ffi-schemes.md)。新项目完整 FFI **设计文档骨架**见 [ffi-docs/design-output-template.md](../../../ffi-docs/design-output-template.md)。**权威长文与代码片段**仍以本目录各专题为准。

---

## 文档索引（按架构思路分组）

### A. Bindgen + 头文件 +「谁在 Rust 里补 inline 语义」

两类主流分叉：**inline 语义放在 `-sys`**（DatenLord），或 **放在上层 safe crate**（jonhoo）。

#### [bindgen-vendored-headers-fnptr-in-safe-layer.md](./bindgen-vendored-headers-fnptr-in-safe-layer.md) — jonhoo/rust-ibverbs（`ibverbs-sys` + `ibverbs`）

**一句话：** 用 **vendor 的 rdma-core 头文件** 驱动 bindgen，生成与 **系统 `libibverbs.so`** 链接的 `extern "C"`；`verbs.h` 里大量 **`static inline`** **不在 `-sys` 补全**，而是由 **`ibverbs` 安全封装层** 在调用业务 API 时直接 **`context.ops.*` 函数指针派发**。

**架构要点：** FFI 的「完整性」拆成两半——`-sys` 提供类型与非内联符号；**数据路径热点（post_send / poll_cq 等）的语义等价实现 resides in safe 层**。好处是 `-sys` 生成逻辑单纯；代价是 **只想依赖 `ibverbs-sys` 裸调 verbs 时，仍需自己抄写 ops 派发**。详见专题文中的分层图与调用链。

#### [bindgen-system-headers-manual-inline-in-sys.md](./bindgen-system-headers-manual-inline-in-sys.md) — datenlord/rdma-sys → async-rdma

**一句话：** **pkg-config** 指向 **本机系统头文件**，bindgen 生成主体绑定；对 **`verbs.h` inline** 在 **`rdma/rdma-sys/src/verbs.rs`** 用 **`#[inline] pub unsafe fn`** 集中手写，从 `qp/cq/context` 取 **`ops` 再间接调用**，使 **`rdma-sys` 单独即可覆盖数据路径**。

**架构要点：** **单一 `-sys` crate 自洽**：`bindings.rs` + **手写 `verbs.rs` / `types.rs`**（blocklist 的 union 结构）。上层 **`async-rdma`** **不再跑 bindgen**，在 sys 之上做 **异步与安全抽象（Tokio 等）**。这是典型的 **「FFI 与 inline 补齐同层，safe/async 另一层」**。详见专题文。

### B. C 参与绑定边界：桩 dlopen vs 薄 wrapper

两者都让 **C 编译器或 C 桩**承担「inline / 符号稳定性」的一部分，但 **链接与运行时模型不同**。

#### [mummy-stub-bindgen-static-stub-dlopen-runtime.md](./mummy-stub-bindgen-static-stub-dlopen-runtime.md) — rdma-mummy-sys → sideway

**一句话：** 编译期链接的是 **`rdma-core-mummy` 静态桩**；桩在 **运行时 `dlopen` 真实 `libibverbs`/`librdmacm`**，用 **`dlsym`** 填函数指针表；bindgen 只对桩暴露的 **`extern "C"`** 生成 Rust，**通常无需在 Rust 手写一整份 inline 派发**。

**架构要点：** **FFI 的稳定边界在「桩提供的符号」**，而非「系统头文件里的 inline 正文」。上层 **`sideway`** 依赖 **`rdma-mummy-sys`**，提供 **现代 verbs API、builder、CQ ex 等 Rust 封装**（刻意保留部分 `unsafe`，完整 safe 由更上层或用户自建）。与 DatenLord **同样是 sys + 封装两层**，但 **sys 内核是「桩 + dlopen」而非「Rust 手写 verbs.rs」**。详见专题文。

#### [c-wrapper-pregenerated-bnd-sys-headers.md](./c-wrapper-pregenerated-bnd-sys-headers.md) — rust-rdma-io / `rdma-io-sys`

**一句话：** **`wrapper.c`** 为每个需要的 inline API 提供 **`rdma_wrap_*` 非 inline C 函数**，由 **C 编译器**展开对真实 `ibv_*` 的调用；用户构建时 **`cc` 只编 wrapper**，**Rust FFI 声明来自仓库内已提交的、bnd 预生成模块**，**消费者构建不跑 bindgen**。

**架构要点：** **符号边界 = 薄 wrapper 静态库 + 动态链官方 `.so`**（不是 mummy 的 dlopen 桩）。仓库内还有 **`rdma-io`** 等对 sys 的进一步封装（专题文会写清 **sys vs 上层 crate** 分工）。详见专题文。

### C. 窄表面 + 生成码二次加工

#### [bindgen-system-subset-postprocess-ruapc.md](./bindgen-system-subset-postprocess-ruapc.md) — SF-Zhou/ruapc-rdma-sys

**一句话：** 仍是 **系统头 + pkg-config + bindgen + 动态链 `libibverbs`**，但通过 **极窄 allowlist** 控制绑定表面；生成后 **`syn`/`prettyplease` 改写 AST**，把部分字段换成 **`FwVer`、`Guid` 等强类型**，并对关键结构 **`derive(Serialize, Deserialize, JsonSchema)`**。

**架构要点：** **没有第二层「大型官方 safe verbs」与本仓库其它专题对等**——定位是 **贴合 ruapc 场景的 sys 层**；**inline/post_send 等**依赖 **`.so` 导出符号**与 allowlist 对齐，而非 C wrapper 或 Rust `verbs.rs` 大全。上层逻辑在 ruapc **其它 crate**。详见专题文。

### D. 并发、线程模型与 Rust 同步 / 异步选型

#### [concurrency-threading-async-vs-sync.md](./concurrency-threading-async-vs-sync.md) — 横跨五条 FFI 主线

**一句话：** 归纳 **各方案自带的线程/运行时假设**（若有）、**在同步封装下如何做高并发 RDMA**（in-flight、批处理、poll/event、`thread-per-core`、`channel` 分工），并单独讨论 **`async-rdma`（Tokio）** 与其余 **同步栈** 的典型用法与选型场景。

**架构要点：** FFI 方案解决 **绑定与符号**；**高并发**绝大多数仍落在 **应用线程模型 + CQ 策略**。只有 **DatenLord `async-rdma`** 把 **异步封装**作为清晰产品层；**Sideway** 等公开取向为 **同步 verbs + 专用线程**。详见该文 §1–§7 的分栈说明与决策表。**文末附录（§10–§13）**补充：**C 侧 post/poll 代码示意**、**与 Redis 单线程类比**、**Rust async / 多 runtime 取舍**、**Tokio TCP + FFI RDMA 桥接示意**、**四种数据传输形态的 2×2 Rust 示意（§13）**。

---

## 索引对照表（快速检索）

| 文件 | 头文件来源 | inline / 热点路径 | 链接 / 运行时 | 典型上层 |
|------|------------|-------------------|---------------|----------|
| [bindgen-vendored-headers-fnptr-in-safe-layer.md](./bindgen-vendored-headers-fnptr-in-safe-layer.md) | vendor rdma-core（cmake） | **safe 层 `ops` 指针** | 链 `libibverbs.so` | `ibverbs` |
| [bindgen-system-headers-manual-inline-in-sys.md](./bindgen-system-headers-manual-inline-in-sys.md) | 系统 pkg-config | **`rdma-sys` `verbs.rs` 手写** | 链 ibverbs + rdmacm | `async-rdma` |
| [mummy-stub-bindgen-static-stub-dlopen-runtime.md](./mummy-stub-bindgen-static-stub-dlopen-runtime.md) | mummy 捆绑 include | **C 桩 + dlsym** | 静态桩 → 运行时开 `.so` | `sideway` |
| [c-wrapper-pregenerated-bnd-sys-headers.md](./c-wrapper-pregenerated-bnd-sys-headers.md) | 系统（生成管线） | **薄 C `rdma_wrap_*`** | 静态 wrapper + `.so` | `rdma-io` 等 |
| [bindgen-system-subset-postprocess-ruapc.md](./bindgen-system-subset-postprocess-ruapc.md) | 系统 pkg-config | **依赖 `.so` 导出 + allowlist** | 动态 `libibverbs` | ruapc 其它 crate |
| [concurrency-threading-async-vs-sync.md](./concurrency-threading-async-vs-sync.md) | — | — | — | **并发专题**：五栈线程模型 + 高并发实践 + async/sync 选型 |

---

## 术语：手写 inline、dlopen/dlsym、为什么要这么做

### 手写 inline

`verbs.h` 里大量 API（如 `ibv_post_send`、`ibv_poll_cq`）在 C 里是 **`static inline`**：编译时把函数体展开，运行时往往是 **`qp/cq → context → ops → 函数指针 → 间接调用`**。这类符号**不一定**在 `libibverbs.so` 里表现为链接器可直接绑的独立导出（具体因版本与绑定策略而异）。

**手写 inline** 指：不在 Rust 里指望 bindgen 自动生成这些函数体，而是由人用 **`#[inline] pub unsafe fn`** 写出**与头文件语义等价**的派发——典型一行就是「解引用 `ops` 再 `.unwrap()(…)` 调用」。见下文「最小代码实例」。

### 运行时 dlopen、dlsym 与「真 .so」

**rdma-core-mummy** 路线下，Rust **链接的是静态桩库**；进程启动后桩里用 **`dlopen("libibverbs.so.1", …)`** 打开系统里的 **`libibverbs.so`**，再用 **`dlsym`** 取出各 `ibv_*` 的真实地址填入函数指针表。

这里的 **「真 .so」** 即系统安装的 **`libibverbs.so`**（含版本后缀习惯写法）；**运行时 dlopen/dlsym** 表示真正实现是在**进程运行阶段**动态载入、解析符号，而不是只靠链接期把每个符号静态解析到同一份库（桩只做转发）。

### 为什么要折腾这些

根本原因是：**bindgen 通常既不生成可靠的 `static inline` 体，也难单独保证与各家 ops 布局一致**。补齐方式只能选其一二：**在 Rust 手写派发**（rdma-sys / rust-ibverbs 思路）、**让 C 编译器展开 inline**（薄 wrapper）、或 **用桩 + dlopen 给出稳定 `extern "C"` 边界**（mummy）。任选一条路都是为了：**让「发帖 / 轮询 CQ」等数据路径在 Rust 里可调、且 ABI 正确**。

### 最小代码实例（C 头文件示意 ↔ Rust 手写等价）

下面是 **`ibv_poll_cq`** 一例：**先**给出头文件里 **inline + ops** 的典型语义（示意，简写），**再**给出工作区 **`rdma/rdma-sys`** 中与之一致的 **手写**实现。

```c
/* verbs.h 典型语义（示意，非逐字拷贝 upstream） */
static inline int ibv_poll_cq(struct ibv_cq *cq, int num_entries,
                              struct ibv_wc *wc) {
    return cq->context->ops.poll_cq(cq, num_entries, wc);
}
```

bindgen 往往**不会**产出可直接调用、且与运行时 ops 表一致的 `ibv_poll_cq` Rust 实现；于是在 `-sys` 层手写：

```628:632:rdma/rdma-sys/src/verbs.rs
#[inline]
pub unsafe fn ibv_poll_cq(cq: *mut ibv_cq, num_entries: i32, wc: *mut ibv_wc) -> c_int {
    (*(*cq).context).ops.poll_cq.unwrap()(cq, num_entries, wc)
}
```

`ibv_post_send` 同理：**从 `qp` 取 `context`，再取 `ops.post_send`**：

```932:939:rdma/rdma-sys/src/verbs.rs
#[inline]
pub unsafe fn ibv_post_send(
    qp: *mut ibv_qp,
    wr: *mut ibv_send_wr,
    bad_wr: *mut *mut ibv_send_wr,
) -> c_int {
    (*(*qp).context).ops.post_send.unwrap()(qp, wr, bad_wr)
}
```

可以把上面的 Rust 函数体理解为：**用人写的 Rust「复述」了 C inline 里那次经 `ops` 的间接跳转**；这就是各方案文档里说的 **手写 inline**。

## Bindgen 使用情况总览

| 方案 | 构建 `cargo build` 时是否跑 bindgen | 代码生成工具 |
|------|--------------------------------------|--------------|
| rust-ibverbs (`ibverbs-sys`) | **是** → `$OUT_DIR/bindings.rs` | **bindgen** |
| datenlord (`rdma-sys`) | **是** → `$OUT_DIR/bindings.rs` | **bindgen** |
| rdma-mummy-sys | **是** → `$OUT_DIR/bindings.rs` | **bindgen** |
| rust-rdma-io (`rdma-io-sys`) | **否**（绑定已预生成在 `src/rdma/`） | **bnd**（维护者生成）；**`cc`** 只编 `wrapper.c` |
| ruapc-rdma-sys | **是** → `$OUT_DIR/bindings.rs`（再 AST 改写） | **bindgen** + **syn/prettyplease** |

除 bindgen/bnd 外，各路线普遍还有：**pkg-config 或 cmake**、**手写类型/verbs** 或 **C 桩/wrapper**、**链接指令**、以及上游 **safe/async crate**。详见各方案文档。

## 一张表看清差异（维度汇总）

| 方案 | 编译期头文件来源 | 解决 inline 的方式 | 链接/runtime | `-sys` 能否单独覆盖数据路径 |
|------|------------------|---------------------|--------------|---------------------------|
| rust-ibverbs | vendor rdma-core（或指定路径） | safe 层 **ops 指针** | 动态 `libibverbs` | 不完整（依赖上层抄指针逻辑） |
| rdma-sys | **系统** pkg-config | **Rust `verbs.rs`** | 动态 ibverbs+rdmacm | **可以** |
| rdma-mummy-sys | mummy 捆绑 include | **C 桩符号** + dlsym | 静态链桩 → 运行时打开 `.so` | **可以**（经由桩） |
| rdma-io-sys | **系统** `-dev` | **薄 C wrapper** | 静态 wrapper + 动态 `.so` | **可以**（wrapper 符号） |
| ruapc-rdma-sys | **系统** pkg-config | 依赖 **`.so` 导出符号** + 窄绑定 | 动态 `libibverbs` | 在其白名单内可以 |

## 其它 FFI / 绑定路线（本目录未单独成篇）

除了上表与五篇专题文档外，业界还存在下列变体或窄场景路线；**内核仍是同一种问题**：让 Rust 能正确调用 **`verbs.h` static inline + ops 派发**（或其它 RDMA C API）。

| 路线 | 做法概要 | 典型用途 / 代表 |
|------|-----------|-----------------|
| **纯手工 `extern "C"`** | 不用 bindgen，手写声明 + 少量类型 | 极简原型、教学；规模一大即难维护 |
| **bindgen 一次生成，绑定检入仓库** | 维护者在有 `-dev`/Docker 的环境里跑 bindgen，把 `bindings.rs` **提交进 git**，用户 `cargo build` **不再执行 bindgen** | 与 rust-rdma-io 的「预生成」思想相同，只是生成器仍是 bindgen；利于 docs.rs、可 diff 审计 |
| **Rust 侧 `libloading` / `dlopen`** | 无 C 桩，在 Rust 里 `dlopen` + `get`，手写符号名字符串与函数类型 | 可做可选插件式 RDMA；易错（符号版本、类型不匹配），全 verbs 表面积大时不划算 |
| **系统头 + bindgen + 大规模手写 inline** | 与 `rdma-sys` 同类，但手写包装函数更多、版本探测更细 | [Nugine/rdma](https://github.com/Nugine/rdma)（参见工作区 `rdma/rust-rdma-io/docs/background/Bindings.md`） |
| **多 ABI / 厂商树探测** | 探测 MLNX_OFED、主线 rdma-core、或由源码构建等，feature 切换不同绑定模块 | [rrddmma](https://github.com/GiantVM/rrddmma) 一类学术 / 实验栈 |
| **包装既有 C++ RDMA 库** | `cxx`、`autocxx` 等对接 C++ 类型与 RAII | 历史上如包装 C++ Infinity 的仓库（多已停更）；**不等于**直接绑 `verbs.h` |
| **厂商直连 verbs（如 `mlx5dv`）** | 另一套头文件与能力模型，常与普通 ibverbs 叠用 | NVIDIA/Mellanox 扩展特性；需单独 FFI，不在「通用 ibverbs 五选一」里 |

**结论：** 并非「只有五种解法」。本目录的五篇文档对应 **当前开源里最常讨论的五种「主线分叉」**（vendor / 系统 bindgen、Rust 手写、mummy、薄 C wrapper、bnd 预生成 + 窄绑定变体）。上表路线多数是它们的 **组合、裁剪或换壳**；未单独写长文，不等于不重要，而是 **读者可先认准主线，再按场景叠加**。

## 方案优缺点对照（归纳）

下列按 **选型维度** 归纳；具体工程仍以目标发行版、CI、团队栈为准。

### 维度说明

| 维度 | 含义 |
|------|------|
| **编译依赖** | 是否需要系统 `libibverbs-dev` / cmake / clang / 子模块 |
| **维护负担** | 随 rdma-core 升级时，手写 Rust/C、桩、wrapper 的跟进成本 |
| **CI / docs.rs** | 在无 RDMA 硬件或不装全套 `-dev` 的环境能否顺利构建 |
| **运行时模型** | 纯动态链 `.so`、静态 wrapper、或 dlopen 桩 |
| **心智与工具链** | 团队是否要掌握 bindgen、mummy、bnd、syn 后处理等 |

### 各主线方案简评

| 方案 | 优点 | 缺点 |
|------|------|------|
| **rust-ibverbs** | vendor 头文件版本可控；教程与社区引用多；bindgen 路径主流 | cmake + 子模块重；**inline 语义落在 safe 层**，`-sys` 单独不完整；无 rdmacm |
| **rdma-sys** | **`-sys` 自洽**（手写 `verbs.rs`）；ibverbs + rdmacm 一体；与 async-rdma 分工清晰 | **强依赖**编译机上的 `-dev`；手写表面随上游演进持续更新 |
| **rdma-mummy-sys** | **编译绑定弱依赖系统 `-dev`**；bindgen 面对的是真实 `extern "C"`；便于 CI；可设计降级 | **C 桩 + cmake** 维护成本高；**dlopen/dlsym** 与符号版本需测试矩阵 |
| **rdma-io-sys（wrapper + bnd 预生成）** | inline 由 **C 编译器**解决；绑定可 **评审 diff**；终端构建不跑重型 codegen | 依赖 **bnd** 流水线；扩展 API 要改 wrapper + 再生；仍要 `-dev`（维护者侧） |
| **ruapc-rdma-sys** | **窄表面**，绑定与上层场景贴合；**serde/schema** 可与运维配置结合 | **非全量 ibverbs**；bindgen + AST 改写调试门槛高；依赖 `.so` 导出符号假设 |

### 横评一句话

- 要 **最少魔法、与 Rust Book 叙事一致**：倾向 **bindgen +（Rust 手写 inline 或 薄 C wrapper）**。  
- 要 **最少折腾 CI、弱化 `-dev`**：倾向 **vendor 头** 或 **mummy**。  
- 要 **可审计的 FFI diff**：倾向 **预生成绑定**（bnd 或检入的 bindgen 输出）。

## 业界「最佳实践」是哪一种？

**先把范围说清楚：**  
在 **数据中心 RDMA 存量代码**里，**主体仍是 C/C++ 直接对接 rdma-core / libibverbs**；Rust 处于 **快速增长但非垄断** 的阶段，因此不存在类似 「HTTP 就用某某框架」那种 **全行业唯一标准答案**。

在 **「Rust 绑定 libibverbs / librdmacm」** 这一子问题内，可以分两层说：

### Rust 语言社区的默认叙事

官方文档与绝大多数 `-sys` crate 的路径是：**bindgen（或等价生成器）消费 C 头文件 + 链接系统动态库**。  
因此，若问「**最符合 Rust 共同体习惯的抽象答案**」：**bindgen + `-sys`，并对 `verbs.h` inline 用 Rust 手写派发或薄 C 封装补齐** —— 这与 **`rdma-sys`、rust-rdma-io 的 wrapper 思路、`Nugine/rdma`** 等大方向一致，而不是某种小众独门技巧。

### 落到具体项目时的务实选择（推荐按约束决策）

| 若你的约束是…… | 更值得优先考虑的路线 |
|----------------|----------------------|
| 团队只想跟主流文档走、接受装 `-dev` | **rdma-sys 类**（系统头 + bindgen + Rust `verbs.rs`），或 **wrapper + 预生成**（rust-rdma-io 哲学） |
| CI/docs 经常缺 RDMA 开发包、希望绑定仍可生成 | **mummy**，或 **vendor 头 + bindgen**（rust-ibverbs） |
| 要强绑定变更的可读 diff、控制构建确定性 | **预生成 FFI**（rust-rdma-io 的 bnd，或 **bindgen 输出检入仓库**） |
| 只需要很小子集、要强类型与序列化 | **ruapc-rdma-sys** 这类 **窄绑定 + 后处理** |

**综上：** 不设前提时，**没有一个方案在所有维度碾压其它方案**。若必须选一个「**叙述上的默认推荐**」：**以 bindgen（或检入的绑定产物）对齐 C ABI，并用 Rust 手写 inline 或薄 C 层解决 `verbs.h` 热点路径** —— 这是在 **工程可理解性、社区工具链与维护成本**之间折中最常见的「实践公约」。**mummy** 与 **vendor** 则是 **在 CI/依赖约束更强时的合理专业化变体**，而非「违反最佳实践」。

## 工作区对照

路径 `rdma/rust-ibverbs/`、`rdma/rdma-sys/`、`rdma/rdma-mummy-sys/`、`rdma/sideway/`、`rdma/rust-rdma-io/` 为本仓库内可参考的源码树；更细的动机叙述见 `rdma/rust-rdma-io/docs/background/Bindings.md`（相对本仓库根目录）。

---

*统计与维护状态以各上游仓库为准；上文「最佳实践」为约束驱动的归纳，非厂商或基金会颁布的标准。*
