# libobmm — 绑定与构建

## 1. 绑定策略选定

### 1.1 路径选择

- [x] **预生成 bindgen**（检入 `obmm-sys/src/bindings/obmm_bindings.rs`）
- [ ] 手写 `extern`（仅作补充，不作为主路径）
- [ ] 构建时 bindgen 为默认
- [ ] C wrapper（`wrapper.c`）
- [ ] 其他

**未选：整库纯手写** — `obmm_mem_desc` / `obmm_preimport_info` 的 FAM 与 `ub/obmm.h` 大量常量使手写易漂移。  
**未选：构建时 bindgen 默认** — 下游与 docs.rs 不宜强依赖 libclang；CI 用预生成 + 再生 job 校验。  
**未选：wrapper.c** — 公开 API 无必须链接的 `static inline` 函数。

### 1.2 选择理由

| 因素 | 结论 |
|------|------|
| API 函数数量 | ~12 个公开函数，规模中等 |
| 结构体复杂度 | FAM、`uint8_t seid[16]`、依赖 `ub/obmm.h` 宏常量 |
| inline | 公开头无；日志头不绑定 |
| 维护 | 预生成 + **systest** 对齐布局；升级 `obmm/VERSION` 时再生命令 |

### 1.3 与 `-sys` / safe 边界

- 绑定与常量：**仅** `obmm-sys`
- `obmm` **不** 运行 bindgen；不 `pub use` 原始 FAM 指针类型给下游直接填 `priv[]`

## 2. 头文件与 bindgen 配置

| 项 | 约定 |
|----|------|
| 入口 | `obmm-sys/wrapper.h`：`#include <libobmm.h>`、`#include <ub/obmm.h>` |
| allowlist | 函数前缀 `obmm_`；结构体 `obmm_*`、`mem_id`；必要 typedef/常量 |
| blocklist | 含 FAM 的 `obmm_mem_desc`、`obmm_preimport_info` 的 **不完整** 生成 → 改用手写 `#[repr(C)]` + `priv` 用 `*mut u8` + `priv_len` 或分离 buffer 类型 |
| 手写补充 | `ObmmMemDescHeader`（固定字段）、`ObmmPreimportInfoHeader`；常量 `OBMM_INVALID_MEMID`、`OBMM_MAX_PRIV_LEN` 等 |
| 再生命令 | `obmm-sys/tools/regenerate_bindings.sh`（文档化 `BINDGEN_EXTRA_CLANG_ARGS=-I...`） |

**bindgen 配置要点**：`use_core()`（若需 `no_std` 再评估）、`rustified_enum` 用于 flags（若 C 为宏则 `pub const`）、`layout_tests` 在 systest 中做。

## 3. `build.rs` 与链接

### 3.1 探测顺序

```text
1. 环境变量 OBMM_DIR（含 include/ + lib64/）
2. pkg-config --libs --cflags obmm（若提供 .pc）
3. 常见路径 /usr/include + -lobmm（文档化，弱探测）
4. 失败：cfg 报错 + README 安装说明（openEuler / 自编译 obmm）
```

### 3.2 `links`

- `links = "obmm"`：**有**
- `cargo:rustc-link-lib=obmm`
- `cargo:rustc-link-search` 来自 pkg-config 或 `OBMM_DIR/lib64`

### 3.3 Feature（构建相关）

| Feature | 作用 |
|---------|------|
| `bindgen` | 本地再生成绑定（非默认） |
| `vendored-headers` | 可选：打包快照头文件仅用于 docs.rs 布局测试（**不** vendored .so） |

## 4. 测试与 ABI 守卫生

| 类型 | 方案 |
|------|------|
| systest | `ctest`/`ctest2` 对照 `wrapper.h`：结构体大小、对齐、`mem_id` 类型 |
| 绑定再生检查 | CI job：头文件变更时 `regenerate_bindings.sh` diff 为空 |
| 无硬件 CI | 默认 `cargo test -p obmm-sys` 仅 layout；`obmm` 集成测试 `#[ignore]` |

## 5. 维护说明

- **升级 obmm**：读 `VERSION` → 再生成绑定 → 跑 systest → 检查 `OBMM_MAX_PRIV_LEN` 等常量 changelog。
- **已知难点**：FAM 描述符在 Rust 中必须通过 owned buffer 传递；禁止 `desc.priv` 栈上柔性数组假结构。
