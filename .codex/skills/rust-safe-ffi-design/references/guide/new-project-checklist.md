# 新 FFI 项目检查清单

> **状态**：阶段 D 正文（2026-05-18）。  
> **用法**：从「接到新 C 库」到「可发布 `-sys`/safe」分阶段勾选（约 35 项，裁定 D8=A）。  
> **决策树**：[architecture-decisions.md](./architecture-decisions.md) · **绑定路径**：[§3.2](./architecture-decisions.md#32-绑定手写--bindgen--预生成--wrapper) · **实践**：[rust-ffi-best-practices.md](./rust-ffi-best-practices.md)。

---

## 调研

- [ ] 阅读上游 C 头：opaque、回调、**inline/宏**、版本化符号（Q1–Q2）
- [ ] 确认 **许可证** 与下游生态兼容性（Q8）
- [ ] 填写 [architecture-decisions.md §1](./architecture-decisions.md#1-库画像问卷绑定前必答) 问卷 Q1–Q9
- [ ] 对照 [general-c-ffi.md §1](../comparison/general-c-ffi.md#1-对照总表) 找最接近样本（P1–P11）
- [ ] 若 **verbs/rdma-core 类**：阅读 [architecture-decisions §2.1](./architecture-decisions.md#21-rdmaverbs-画像子分支) 与 [rdma-overview §0.7](../rdma/rdma-overview.md#07-五-category-对照阶段-c)
- [ ] 记录 **create/destroy** 与线程安全说明（Q3–Q4）

---

## 脚手架

- [ ] 确定 crate 名：`yourlib-sys` + `yourlib`（**默认**；单 crate 须 ADR 说明）
- [ ] 决定是否 `links = "..."`（参考 P1–P5；pkg-config 型见 P6–P8）
- [ ] 创建 `build.rs` 链接分支草稿（系统 / pkg-config / vendored 顺序写入注释）
- [ ] 添加 `LICENSE` 与上游许可声明
- [ ] workspace 成员划分（若 API 面大：macros/errors 独立，参考 P3）

---

## 绑定（含手写 vs bindgen）

- [ ] 在 [§3.2 决策树](./architecture-decisions.md#32-绑定手写--bindgen--预生成--wrapper) 上选定主路径并 **ADR 记录**
- [ ] **手写**：评估符号数量 &lt; ~100 且 ABI 稳定；承诺 **systest**（P1、P2）
- [ ] **预生成 bindgen**：添加 `bindings_*.rs` + `regenerate.sh` 文档（P4）
- [ ] **构建时 bindgen**：`bindgen` feature + CI clang 说明（P3）
- [ ] **inline 多**：选定 wrapper.c / 手写 verbs / mummy，**非** 裸 bindgen 整头（P6–P10、P7）
- [ ] 配置 `wrapper.h` 或 bindgen **allowlist/blocklist**
- [ ] 复杂 union/bitfield：**blocklist + 手写类型**（P5、P8）
- [ ] 添加 **`systest` 或 ctest2**（对外 `-sys` **强烈建议**）

---

## 安全层（若需要）

- [ ] `-sys` 与 `yourlib` 边界：仅 safe  crate 公开 Ergonomic API
- [ ] `Drop` / `ForeignType` / 错误类型与 C 习惯对齐（§5）
- [ ] 为 `unsafe fn` 写 **`# Safety`** 与 C 契约表
- [ ] `Send`/`Sync` 仅在有依据时实现（P11）
- [ ] C 回调：panic 策略（参考 P2 `panic.rs`）
- [ ] 上层协议/运行时：**不** 在适配 crate 新增 `extern "C"`（P7）

---

## 质量

- [ ] 单元测试 / 编解码或 init-destroy 往返（P4）
- [ ] 至少一个 **safe** example（P2）
- [ ] CI：stable + 主要 feature 组合（参考 P1 CI 矩阵思路）
- [ ] 无硬件路径：mummy 或 `cargo build -p yourlib-sys` only（P10）
- [ ] 集成测试需设备时 `#[ignore]` + README（P7、P9）
- [ ] （可选）trybuild 测 API 不变量（P11）

---

## 发布与生态

- [ ] README：**链接方式**、feature、是否仅需 `-sys`
- [ ] README：**手写或 bindgen** 维护方式与再生命令
- [ ] `-sys` / safe **semver** 策略与 breaking 政策（P3）
- [ ] 声明传递依赖（zlib/ssl 等，P2）
- [ ] docs.rs：vendored 或 `links` 策略说明（P1、P5）
- [ ] 下游 feature 协调文档（参考 P1 zlib-ng 双 crate）

---

## 修订记录

| 日期 | 变更 |
|------|------|
| 2026-05-18 | 阶段 D 正文：五阶段约 35 项，对齐决策树与手写/bindgen |
