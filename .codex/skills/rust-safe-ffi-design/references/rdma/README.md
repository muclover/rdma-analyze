# RDMA Reference Router

本目录只在目标库属于 RDMA / verbs / rdma-core，或遇到类似的 `ops` 表、`static inline`、硬件依赖 CI 问题时读取。普通 C 库不要进入本目录。

优先读取 [rdma-overview.md](./rdma-overview.md)，只有需要具体样本细节时再选读单篇 `FFI-ANALYSIS.md`。

## 读取顺序

1. [rdma-overview.md](./rdma-overview.md) — RDMA 横向结论、五类路线、wrapper/mummy/soft RDMA 取舍。
2. 下表中最接近目标库的一到两篇样本。
3. 若仍无法决策，再回到 [../comparison/general-c-ffi.md](../comparison/general-c-ffi.md) 做横向核对。

## 样本路由

| 目标画像 | 选读样本 |
|----------|----------|
| 经典 `ibverbs-sys` + safe 双 crate，目标是最小可用 verbs safe API | [category-1/rust-ibverbs/FFI-ANALYSIS.md](./category-1/rust-ibverbs/FFI-ANALYSIS.md) |
| 单 crate 内嵌 bindings + examples，偏实验或内部使用 | [category-2/rdma/FFI-ANALYSIS.md](./category-2/rdma/FFI-ANALYSIS.md) |
| 大型 workspace，safe I/O 层之上还有 tonic/quinn 等传输适配 | [category-3/rust-rdma-io/FFI-ANALYSIS.md](./category-3/rust-rdma-io/FFI-ANALYSIS.md) |
| 只做裸 `-sys`，需要处理 verbs 类型、blocklist、pkg-config | [category-4/rdma-sys/FFI-ANALYSIS.md](./category-4/rdma-sys/FFI-ANALYSIS.md) |
| 复用外部 `-sys`，自身定位为 async 框架 | [category-4/async-rdma/FFI-ANALYSIS.md](./category-4/async-rdma/FFI-ANALYSIS.md) |
| CI 编译期不希望依赖 `libibverbs-dev`，考虑 mummy 静态导出 | [category-5/rdma-mummy-sys/FFI-ANALYSIS.md](./category-5/rdma-mummy-sys/FFI-ANALYSIS.md) |
| safe 层依赖 mummy sys，并强调类型状态或 Extended Verbs | [category-5/sideway/FFI-ANALYSIS.md](./category-5/sideway/FFI-ANALYSIS.md) |

## 交付物注意

- 在用户交付物中写结论，不写样本编号或本目录路径。
- 若目标库不是 RDMA，但有类似 inline/ops 问题，只借鉴 wrapper/mummy/手写替代路线，不引入 RDMA 业务术语。
- RDMA 样本普遍涉及硬件或软 RDMA 环境；设计 `CHECKLIST.md` 时要区分 compile-only、ABI check、soft-device integration 和真实硬件测试。
