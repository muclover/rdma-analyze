已按 `rust-safe-ffi-design` skill 完成设计级审查，并在默认位置生成 6 份交付文档：

- [README.md](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/docs/ffi-design/obmm/README.md)
- [ARCHITECTURE.md](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/docs/ffi-design/obmm/ARCHITECTURE.md)
- [BINDING.md](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/docs/ffi-design/obmm/BINDING.md)
- [SAFE-API.md](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/docs/ffi-design/obmm/SAFE-API.md)
- [ASYNC-ANALYZE.md](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/docs/ffi-design/obmm/ASYNC-ANALYZE.md)
- [CHECKLIST.md](/home/muxi/workspace/rust_workspace/rust-ffi-project-analyze/docs/ffi-design/obmm/CHECKLIST.md)

核心修订结论：草案不应只做单 crate；应拆 `obmm-sys` + `obmm`。不建议默认用户构建时全量 `bindgen`，这个 API 面很小且包含 flexible array member，首选手写 raw ABI。safe 层不应暴露裸 `u64 mem_id`，应使用 `NonZeroU64` newtype 和 RAII owner；`obmm_mem_desc` / `obmm_preimport_info` 不能用普通 Rust struct + `Vec<u8>` 表示 C ABI，必须用连续 descriptor buffer。`Send`/`Sync` 默认保守，不做无依据 `unsafe impl`。

已按要求只做静态阅读，没有编译或实现代码。验证过 6 个文件齐全，并运行了内部痕迹检查：

`rg "P[0-9]+|FFI-ANALYSIS|general-c-ffi|comparison|→ §|裁定|阶段 [A-Z]" docs/ffi-design/obmm`

该检查无输出，表示没有泄漏 skill 内部 reference 标记。