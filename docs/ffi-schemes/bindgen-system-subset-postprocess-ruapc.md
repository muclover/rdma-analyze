# FFI 方案：Bindgen + pkg-config（窄白名单）+ 生成结果 AST 后处理

**代表项目：** [SF-Zhou/ruapc-rdma-sys](https://github.com/SF-Zhou/ruapc-rdma-sys)

> 注：本仓库未 vendor 该项目；下列结论来自其公开的 `Cargo.toml` / `build.rs`。

## Bindgen：是否使用、怎么用

| 项 | 说明 |
|----|------|
| **是否使用** | **是**。`build-dependencies` 含 **`bindgen`**；`build.rs` 里 **`bindgen::Builder::default()`** 生成绑定。 |
| **头文件 / include** | **`pkg-config`** 探测 **`libibverbs`**，收集 **`include_paths`**，并 **`clang_arg("-I...")`**；再用 **`.header_contents(..., "#include <infiniband/verbs.h>")`**（以仓库实际字符串为准）拉入 verbs 声明。 |
| **策略：窄白名单** | **`allowlist_type` / `allowlist_function`** 只保留 ruapc 需要的子集（device、CQ、QP、MR、poll/post、query 等），减小生成体积与编译时间。 |
| **bindgen 插件能力** | **`ParseCallbacks`（`CustomDerive`）**：对 **`ibv_device_attr`、`ibv_port_attr`** 等结构自动 **`derive(Serialize, Deserialize, JsonSchema)`**。 |
| **精细化类型控制** | **`opaque_type`**（如 pthread 类型）、**`no_copy` / `no_debug`** 针对含 **函数指针** 或不希望自动 **`Copy`** 的类型。 |
| **产物** | 字符串写入 **`$OUT_DIR/bindings.rs`**（经后处理，见下）。 |

## bindgen 之外还做了什么

| 工作 | 说明 |
|------|------|
| **`syn` + `prettyplease` 后处理** | 把 bindgen 输出的 Rust 源码 **parse 成 AST**，将 **`fw_ver`、`node_guid`、`wr_id`、`link_layer`** 等字段类型 **替换为自定义包装类型**（`FwVer`、`Guid`、`WRID`、`LinkLayer`），再 **`prettyplease::unparse`** 写回文件。 |
| **运行时链接** | **`pkg-config`**，**动态**链接 **`libibverbs`**（非 mummy、非静态桩）。 |
| **`libc` / `serde` / `schemars` / `clap`** | 依赖侧为 **生成的类型与 CLI/序列化场景** 服务；不属于 bindgen，但与「绑定如何被上层用」强相关。 |
| **inline / 符号** | **未**引入 C wrapper 或 Rust `verbs.rs` 大全；依赖 **`libibverbs.so` 导出符号** 与 **`allowlist_function`** 对齐。若某环境链接失败，需按上游策略追加 wrapper 或 ops 派发（一般 Linux rdma-core 可链上）。 |

## 层级定位

`ruapc-rdma-sys` 定位为 **偏底层的 FFI crate**，面向 **libibverbs**（README 强调 device 管理与类型安全辅助）；**不是**「sys + 大型 safe 框架」的第二层，上层逻辑在 ruapc 主工程其它 crate 中（若有）。

与 DatenLord 对比：

| 维度 | `rdma-sys` | `ruapc-rdma-sys` |
|------|-----------|------------------|
| 覆盖面 | ibverbs + **rdmacm**，绑定面广 | allowlist **精简子集**（device/CQ/QP/MR/post/send/poll 等） |
| 绑定后处理 | 主要靠手写模块配合生成码 | **`syn` + `prettyplease`** 改写结构体字段类型（`FwVer`、`Guid`、`WRID` 等） |
| serde | 非核心目标 | 对 **`ibv_device_attr`、`ibv_port_attr`** 等加 **`Serialize` / `Deserialize` / `JsonSchema`** |

## 构建要点（公开 `build.rs`）

1. **pkg-config** 探测 **`libibverbs`**（非静态）。
2. **bindgen** 使用 **窄 allowlist** 的类型与函数；包含 **`ibv_post_send`、`ibv_poll_cq`** 等数据路径符号。
3. 生成字符串后经 **`replace_custom_types`** 把部分字段替换为 Rust 包装类型，再写入 `OUT_DIR/bindings.rs`。

## static inline / 符号从哪里来？

bindgen 对 **`allowlist_function("ibv_post_send")`** 等会生成 `extern "C"`。能否链接取决于 **目标 `.so` 是否导出对应符号**。Linux 上 rdma-core 通常为兼容 ABI **导出带版本标签的 `ibv_*` 实现**，因此这类 crate 常能 **不再写 Rust inline 包装**也能链接（若遇链接失败再退回「手写 ops 派发」或 C wrapper 策略）。

**结论：** `ruapc-rdma-sys` 在工作区分类里与 **`rdma-sys` 同属「系统头文件 + bindgen + 动态链 libibverbs」一族**，差异在于 **白名单更窄、生成码二次加工、类型衍生更重**，而不是再走一层 C wrapper 或 mummy。

## 优缺点摘要

| 优点 | 缺点 |
|------|------|
| 依赖面小，绑定聚焦 ruapc 用量 | **不是完整 ibverbs/rdmacm** 克隆 |
| 结构化类型（GUID、FW 版本）利于上层配置/序列化 | bindgen + syn 后处理 **调试门槛**略高 |
| 新项目迭代活跃（以仓库为准） | 生态引用样本少于老牌 crate |

## 小结一句

**同一层的 sys-style FFI（直连系统 libibverbs），特色是「窄绑定 + bindgen 后 AST 手术」，而非两层 FFI 或 mummy。**
