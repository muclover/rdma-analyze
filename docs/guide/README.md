# Rust FFI 新项目指导（阶段 D）

> **状态**：**已完成**（2026-05-18）。  
> **依据**：[plan.md](../../plan.md) 阶段 D、[general-c-ffi.md](../comparison/general-c-ffi.md)、11 份 `FFI-ANALYSIS.md`。  
> **裁定摘要**：D1=C · D2=B · D3=B · D6=C · D7=B · D9=A · D10=团队/生态 · D12=虚构演练 · **手写/bindgen 显式**见 [architecture-decisions §3.2](./architecture-decisions.md#32-绑定手写--bindgen--预生成--wrapper)。

## 阅读顺序

```text
1. comparison/general-c-ffi.md §1、§3   ← 样本做了什么（可选速览）
2. architecture-decisions.md      ← 绑新库：问卷 + 总览树 + 分主题树 + 附录演练
3. rust-ffi-best-practices.md     ← 原则、手写/bindgen、反模式
4. new-project-checklist.md       ← 分阶段勾选
5. 按需 docs/**/FFI-ANALYSIS.md
```

## 文档地图

| 文件 | 用途 | 状态 |
|------|------|------|
| [rust-ffi-best-practices.md](./rust-ffi-best-practices.md) | 原则、推荐做法、**§4 手写 vs bindgen**、反模式、索引 | 完成 |
| [architecture-decisions.md](./architecture-decisions.md) | 问卷 + 总览/分主题决策树 + 可选 ADR + **附录 B 虚构库演练** | 完成 |
| [new-project-checklist.md](./new-project-checklist.md) | 新项目分阶段检查清单（~35 项） | 完成 |

## 与阶段 C 的分工

| 阶段 | 产出 | 读者问题 |
|------|------|----------|
| **C** | `comparison/*`、`rdma-overview.md` | 「这 11 个项目**做了什么**？」 |
| **D** | `guide/*`（本目录） | 「**我们**绑新库时该怎么选、怎么开工？」 |

阶段 D 每条规范性表述须链到 C 或单项目证据，或标「规范建议（无样本）」。

## 完成定义（阶段 D）

- [x] 仅凭 `guide/` 可完成首版架构方案（附录 B 虚构 `libchart` 演练）
- [x] 每条应做/不应做有样本引用或「规范建议（无样本）」
- [x] 与 `comparison/` 无未解释矛盾；RDMA 为子分支非第二范式

---

## Agent 技能

绑新 C 库并产出多文件架构设计时，可使用项目技能 **`.cursor/skills/rust-safe-ffi-design/`**（`@rust-safe-ffi-design`）。技能从 **`references/`** 目录读取本指南与 `comparison/` 等的**副本**；更新 `docs/` 后请同步复制到 `references/`（见 `references/SOURCE.md`）。

---

## 修订记录

| 日期 | 变更 |
|------|------|
| 2026-05-18 | 启动：README + 三份正文骨架 |
| 2026-05-18 | 阶段 D 完成：三份正文 + 完成定义勾选 |
| 2026-05-18 | 增加 `rust-safe-ffi-design` 技能索引 |
