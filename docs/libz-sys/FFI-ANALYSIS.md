# libz-sys — FFI 单项目分析

**分析对象（源码路径）：** `libz-sys/`  
**计划序号：** P1（阶段 A1）  
**分析方式：** 静态阅读（未执行 `cargo build` / `cargo check`）  
**文档版本：** 2026-05-18

---

## 0. 摘要

`libz-sys` 是面向 **zlib / zlib-ng（compat 或 native）** 的 **纯 `-sys` crate**：在 Rust 侧提供与 C 头文件对齐的 `extern "C"` 绑定与常量，**不含 safe 封装**；推荐上层使用 [`flate2`](https://docs.rs/flate2) 等库。架构上为 **单层 FFI + 复杂 `build.rs` 链接策略**（系统 `libz`、pkg-config、vcpkg、内嵌 stock zlib、内嵌 zlib-ng），并通过 `links = "z"` 参与 Cargo 原生库去重。同一仓库还以 `Cargo-zng.toml` 发布 **`libz-ng-sys`**（native zlib-ng API，`links = "z-ng"`）。

---

## 1. 仓库结构

### 1.1 Workspace 成员

| 成员 | 路径 | 角色 |
|------|------|------|
| 主 crate | 根 `Cargo.toml` → `libz-sys` | FFI 绑定 + 构建 |
| `systest` | `systest/` | ABI/布局与 C 头一致性测试（`ctest2`） |
| `maint` | `maint/` | 双 crate 发布、打包校验、`libz-ng-sys` 代理命令 |

`[workspace] members = ["maint", "systest"]`（`confirmed`，`libz-sys/Cargo.toml`）。

### 1.2 双 crate 发布（同仓不同 manifest）

| Crate | Manifest | `links` | 说明 |
|-------|----------|---------|------|
| `libz-sys` | `Cargo.toml` | `z` | 默认 stock zlib；可选 zlib-ng **compat** |
| `libz-ng-sys` | `Cargo-zng.toml` | `z-ng` | 仅 native `zng_*` API；`build = "zng/cmake.rs"` |

发布流程见 `MAINTENANCE.md`、`maint/`（`confirmed`）。

### 1.3 主要目录

| 路径 | 内容 |
|------|------|
| `src/lib.rs` | 全部 Rust FFI 表面（手写） |
| `src/zlib/` | vendored **stock zlib** 1.3.2 头文件与 `.c` |
| `src/zlib-ng/` | vendored **zlib-ng** 源码树 |
| `build.rs` | 链接/编译决策 |
| `zng/cmake.rs`, `zng/cc.rs` | zlib-ng 的 cmake / cc 构建路径 |
| `ci/` | CI 脚本（`test.bash`、`run-docker.sh` 等） |
| `.github/workflows/ci.yml` | 多 target、stable/beta/nightly |

### 1.4 Examples

**无** `examples/` 目录。README 将高层用法指向 `flate2`（`confirmed`）。

### 1.5 Tests（仓库内，非本分析构建验证）

| 类型 | 位置 | 说明 |
|------|------|------|
| `systest` | `systest/` | 对 `src/lib.rs` 与 C 头做布局/符号测试 |
| 上游 C 测试 | `src/zlib-ng/test/` 等 | vendored 自带；本 crate 不在 Rust 层直接暴露 |

---

## 2. 原生依赖

### 2.1 库画像（嵌入 §0 / §2）

| 字段 | 内容 |
|------|------|
| **C 库** | zlib（RFC 1950/1951/1952）；可选 zlib-ng（高性能分支） |
| **API 形态** | 过程式 C API：`z_stream` 流式 deflate/inflate；`gzFile` 类 stdio；`compress`/`uncompress` 缓冲区 API；大量 `Z_*` 返回码 |
| **资源对** | `deflateInit*` ↔ `deflateEnd`；`inflateInit*` ↔ `inflateEnd`；`gzopen` ↔ `gzclose`；`z_stream.state` 不透明 |
| **线程安全** | 单 `z_stream` 非线程安全；若 `zalloc`/`zfree` 线程安全则库可线程安全（`zlib.h` 文档） |
| **分发方式** | 系统 `libz`、pkg-config、Windows vcpkg、内嵌编译 stock zlib、内嵌 zlib-ng（cmake 或 cc） |
| **绑定难点** | zlib-ng 与 stock 的 **类型宽度差异**（`uLong` vs `size_t`/`uint32_t`）；compat 下 `link_name`；版本化 `deflateInit_`；部分新 API 未绑定 |

### 2.2 `links` 与去重

- `libz-sys`：`links = "z"`（`confirmed`）
- `libz-ng-sys`：`links = "z-ng"`（与 stock/compat 符号空间分离）

### 2.3 链接策略决策树（`build.rs` 摘要）

```
want_ng = (zlib-ng | zlib-ng-no-cmake-…) && !stock-zlib
├─ want_ng && target != wasm32 → build_zlib_ng (compat)
├─ android / haiku / OpenHarmony → -lz 系统库
├─ pkg-config probe zlib（多数 Unix，排除 static/FreeBSD/DragonFly/MSVC 等）
├─ Windows → try vcpkg
├─ MSVC / windows-gnu / static feature / LIBZ_SYS_STATIC → vendored build_zlib
├─ smoke test 链接 -lz 成功 → 系统 libz
└─ 否则 → vendored build_zlib
```

要点（`inferred` + `confirmed` 来自 `build.rs` 注释）：

- **默认倾向系统共享 libz**，便于与系统其他原生依赖共用一份 zlib（README `confirmed`）。
- **pkg-config 不打印 system lib 路径**，避免与 OpenSSL 等 `/usr/local` 路径冲突（`build.rs` 注释）。
- **`static` feature / `LIBZ_SYS_STATIC`** 可强制静态内嵌；环境变量可覆盖 feature（`should_link_static`）。
- **wasm32**：`Z_SOLO`，省略 gz* 部分 `.c`（`build.rs`）。

### 2.4 Vendored 范围

| 来源 | 路径 | 何时编译 |
|------|------|----------|
| stock zlib 1.3.2 | `src/zlib/*.c` | 系统库不可用或强制 static/Windows 等 |
| zlib-ng | `src/zlib-ng/` | feature `zlib-ng`（cmake）或 `zlib-ng-no-cmake-…`（`zng/cc.rs`） |

**未读** vendored `.c` 实现细节（符合 `project-scope.md` §4.5）。

### 2.5 构建依赖

`pkg-config`、`cc`、`cmake`（可选 feature）、`vcpkg`（`build-dependencies`，`Cargo.toml`）。

---

## 3. 绑定生成

### 3.1 方式：**手写为主**，非 build-time bindgen

- **Rust 绑定**：`src/lib.rs` 内手写 `#[repr(C)]` 结构体、`extern "C"` 函数、`pub const Z_*`（`confirmed`）。
- **无 `wrapper.h`**：直接对照 `src/zlib/zlib.h`（及 zlib-ng 头）维护；与 bindgen 工作流不同。
- **ABI 校验**：`systest/build.rs` 使用 **`ctest2`** 读取 `zlib.h` / `zlib-ng.h`，对 `../src/lib.rs` 生成测试并 `include!`（`confirmed`）。这是「生成测试代码」，不是生成生产绑定。

### 3.2 C 头暴露的主要 API 形态（§4.4）

来自 `src/zlib/zlib.h`（vendored 1.3.2）：

| 形态 | 代表 | Rust 映射 |
|------|------|-----------|
| 流状态机 | `z_stream` + `deflate`/`inflate` + flush 常量 | `pub struct z_stream`；`deflate`/`inflate` 等 |
| 版本化初始化 | `deflateInit_` / `deflateInit2_`（带 `version`、`stream_size`） | 单独 `extern` 块；zlib-ng native 用 `zng_*` + Rust `#[inline]` 兼容壳 |
| 不透明状态 | `struct internal_state` | `pub enum internal_state {}` |
| 文件句柄 | `gzFile` | `*mut gzFile_s` + 空 enum |
| 一次性缓冲 | `compress` / `uncompress` | feature `libc` 时暴露 |
| 错误模型 | 函数返回 `int`（`Z_OK`、`Z_STREAM_ERROR`…） | `pub const Z_*`；无 Rust `Result` |
| 回调 | `alloc_func` / `free_func` / `in_func` / `out_func` | `type` alias + `extern "C" fn` |

**刻意未绑定**（`lib.rs` 注释）：如 `deflatePending`、`inflateGetDictionary`、部分 `gz*`（标注为后续版本 API，`confirmed`）。

### 3.3 zlib-ng 差异处理

| 机制 | 作用 |
|------|------|
| `cfg(zng)` | native `libz-ng-sys` 构建时由 `zng/cmake.rs` 设置（`ZLIB_COMPAT=OFF`） |
| `if_zng!` / `z_size` / `z_checksum` | compat 用 `c_ulong`，native 用 `usize`/`u32` |
| `zng_prefix!` + `#[link_name = …]` | compat 映射到标准符号；native 映射到 `zng_*` |
| `deflateInit_` 等 | native 模式用 `zng_deflateInit` + 内联兼容 `deflateInit_` 签名（BC，`confirmed` 注释） |

### 3.4 再生流程

- **绑定**：人工改 `src/lib.rs`；无 bindgen 再生成步骤。
- **维护**：`maint` 工具校验双 manifest 版本与 `cargo package` 内容（`MAINTENANCE.md`）。
- **C 源码**：vendored 随 crate 发布；子模块需在发布前初始化（`MAINTENANCE.md` `confirmed`）。

---

## 4. 分层与公开 API

### 4.1 分层

| 层 | 存在？ | 说明 |
|----|--------|------|
| `-sys` | **是**（唯一层） | 全部 API 在 `libz_sys` crate root |
| safe | **否** | README 明确指向 `flate2` |
| async | **否** | N/A |

### 4.2 模块树

单文件 crate：`src/lib.rs` 导出所有类型与函数，无 `mod` 子模块拆分。

### 4.3 典型类型与函数（目录级）

| 类别 | 符号示例 |
|------|----------|
| 核心结构 | `z_stream`, `gz_header`, `internal_state`, `gzFile_s` |
| 流 API | `deflate`, `inflate`, `deflateEnd`, `inflateEnd`, `deflateInit_`, … |
| 校验 | `adler32`, `crc32`, `*_combine` |
| gzip 文件 | `gzopen`, `gzread`, `gzwrite`, `gzclose`, …（`feature = "libc"`） |
| 缓冲 | `compress`, `uncompress`, `compressBound` |
| 常量 | `Z_NO_FLUSH`, `Z_OK`, `Z_STREAM_ERROR`, `Z_DEFLATED`, … |

### 4.4 Feature 对 API 的影响

| Feature | 效果 |
|---------|------|
| `libc`（默认） | 启用 gz*、`compress*` 等依赖 libc 的 API |
| 无 `libc` | `Z_SOLO` 式裁剪：无 gz/compress 高层 C API（`Cargo.toml` 注释 `confirmed`） |
| `stock-zlib`（默认） | 与 `zlib-ng` 互斥时优先 stock |
| `zlib-ng` | compat 构建；需 `cmake` |
| `static` | 倾向内嵌静态 zlib |

---

## 5. 资源与生命周期

### 5.1 C 侧模型（来自 `zlib.h`）

- **`z_stream`**：调用方分配结构体；`deflateInit*` 分配内部 `state`；**必须** `deflateEnd` / `inflateEnd` 释放。
- **`gzFile`**：由 `gzopen`/`gzdopen` 返回；**必须** `gzclose`。
- **缓冲指针**：`next_in` / `next_out` 由调用方拥有；库不取得所有权（除非文档另有说明）。

### 5.2 Rust 侧

- **无 RAII 包装**：`z_stream` 等为 `#[repr(C)]` 普通结构体，**无 `Drop` 实现**（`confirmed`，`src/lib.rs`）。
- **泄漏风险**：忘记 `deflateEnd`/`inflateEnd`/`gzclose`；`deflateCopy`/`inflateCopy` 后双重释放；`msg` 指针通常指向静态/内部字符串，不应 `free`（需读 C 文档，§11）。

### 5.3 `opaque` 与自定义分配器

- `zalloc`/`zfree`/`opaque` 可在 init 前设置；多线程场景需线程安全分配器（`zlib.h` `confirmed`）。

---

## 6. 错误与安全边界

### 6.1 错误模型

- C 惯例：**`int` 返回码** + 可选 `strm->msg` 字符串（`confirmed`，`zlib.h`）。
- Rust：**无** `Result`、无专用 error crate；调用方检查 `Z_OK` 等常量。
- `gzerror`：返回消息并写入 `errnum`（`libc` feature）。

### 6.2 `unsafe` 边界

- **整个公开 API 本质为 `unsafe` 语义**：所有 `extern "C"` 调用要求调用方满足 C 契约。
- crate **未** 用 `unsafe fn` 标记每个函数；依赖文档与上层 safe 封装（`-sys` 惯例）。
- **少数 safe 内联壳**（仅 `cfg(zng)`）：如 `deflateInit_` 转发到 `zng_deflateInit`，内部为 `unsafe` 调用（`confirmed`）。

### 6.3 调用契约要点

| 契约 | 说明 |
|------|------|
| 初始化 | 必须先 `deflateInit_`/`inflateInit_`（或 `*Init2_`）再操作流 |
| 版本参数 | stock zlib 的 `deflateInit_` 需正确 `zlibVersion()` 与 `stream_size` |
| 指针有效性 | `next_in`/`next_out` 在调用期间有效；长度与 `avail_*` 一致 |
| 别名 | 遵循 C 库 restrict/别名规则 |
| 符号冲突 | 同时动态链接 stock zlib 与 zlib-ng 非 compat 可能符号冲突（README `confirmed`） |

---

## 7. 并发与 async

### 7.1 线程模型

- **Per-stream 非线程安全**：同一 `z_stream` 不可并发使用（C 库通用语义，`inferred`）。
- **库级线程安全**：取决于自定义 `zalloc`/`zfree` 是否线程安全（`zlib.h` 第 150–151 行附近，`confirmed`）。
- Rust：**未** 实现 `Send`/`Sync` marker；裸指针与 C 布局结构体默认 `Send` 与否由字段决定，**无** 官方并发保证文档。

### 7.2 async

**N/A** — 无异步 API；若需 async 须在 safe 层用阻塞线程池或完全非阻塞上层协议。

---

## 8. 测试与示例

### 8.1 测试策略

| 测试 | 机制 | 硬件 |
|------|------|------|
| **`systest`** | `ctest2` 对照 `zlib.h`/`zlib-ng.h` 验证 `lib.rs` 布局与函数签名 | 否 |
| **CI** | `cargo test` + `systest`；多 target `cross`；stock / `zlib-ng` / experimental cc 路径 | 否 |
| **链接探测** | `src/smoke.c` + `cc` 试链 `-lz`（`build.rs` `zlib_installed`） | 否 |
| **发布前** | `maint publish` dry-run 多 feature 组合 | 否 |
| **上游 fuzz** | `src/zlib-ng/test/fuzz/` | §8 一句带过 |

CI 矩阵：Windows / macOS / Linux musl&gnu、stable/beta/nightly、`minimal-versions`（`.github/workflows/ci.yml` `confirmed`）。

### 8.2 Examples（简单阅读）

- **无** crate 级 examples。
- README 指向 **`flate2`** 作为高层用法与替代实现（纯 Rust 等）（`confirmed`）。

---

## 9. 证据索引

| 路径 | 支撑结论 |
|------|----------|
| `libz-sys/README.md` | 定位纯 `-sys`、默认 stock zlib、feature 约定、指向 flate2 |
| `libz-sys/Cargo.toml` | `links = "z"`、features、workspace、vendored include 列表 |
| `libz-sys/build.rs` | 链接决策树、pkg-config/vcpkg/static/wasm/zlib-ng 分支 |
| `libz-sys/src/lib.rs` | 手写 FFI、`zng_prefix`/`if_zng!`、未绑定 API 注释 |
| `libz-sys/src/zlib/zlib.h` | C API 形态、线程安全说明、`z_stream` 生命周期 |
| `libz-sys/zng/cmake.rs` | zlib-ng cmake、compat、`cfg(zng)` 设置 |
| `libz-sys/systest/build.rs` | ctest2 校验流程、zng 类型名映射 |
| `libz-sys/systest/src/main.rs` | 生成的 ABI 测试入口 |
| `libz-sys/Cargo-zng.toml` | `libz-ng-sys` 独立 `links` 与 build 路径 |
| `libz-sys/MAINTENANCE.md` | 双 crate 发布与子模块要求 |
| `libz-sys/.github/workflows/ci.yml` | CI 矩阵与 systest/zlib-ng 组合 |
| `libz-sys/ci/test.bash` | 各 target 上 test + systest + zlib-ng feature |
| `libz-sys/src/smoke.c` | 系统 libz 探测最小程序 |

---

## 10. 架构决策推断

> 默认置信度：`inferred`，除非标注 `confirmed`。

### 10.1 手写绑定 + ctest2，而不用 build-time bindgen

| 字段 | 内容 |
|------|------|
| **决策** | 生产绑定手写；用 `ctest2` 在 `systest` 中对齐 C 头 |
| **C 侧事实** | API 稳定、中等规模；存在版本化 `*Init_` 与大量 `Z_*` 常量 |
| **Rust 侧事实** | 单文件 `lib.rs`；`systest` 从 `lib.rs` 生成测试；注释承认部分新 API 未跟进 |
| **推断动机** | 手写可精确控制 `link_name`、条件编译（zlib-ng）、以及有意裁剪 API；ctest2 降低布局漂移风险且无需每次构建运行 bindgen |
| **证据** | `src/lib.rs`、`systest/build.rs` |
| **置信度** | inferred |

### 10.2 默认链接系统 libz，内嵌为回退

| 字段 | 内容 |
|------|------|
| **决策** | 优先系统 `libz`，失败或特定平台再 vendored 编译 |
| **C 侧事实** | zlib 在主流 OS 上普遍预装 |
| **Rust 侧事实** | pkg-config → smoke `-lz` → `build_zlib` 顺序；README 写明 default 用 stock zlib 以利共享库 |
| **推断动机** | 减小二进制体积、共享安全更新、与 distro 策略一致；内嵌保证 Windows/无 zlib 环境可构建 |
| **证据** | `build.rs`、`README.md` |
| **置信度** | confirmed（README）+ inferred（优先级逻辑） |

### 10.3 Feature 协调：依赖图级别的 zlib 实现选择

| 字段 | 内容 |
|------|------|
| **决策** | `stock-zlib` 默认；`zlib-ng` 需全图 opt-in；文档要求下游 `default-features = false` |
| **C 侧事实** | compat 模式符号与 zlib 相同；非 compat 的 native API 为 `zng_*` |
| **Rust 侧事实** | `want_ng = … && !stock-zlib`；README 警告依赖图中任一 crate 要 stock 则全体 stock |
| **推断动机** | 避免同一进程两套 zlib 符号；把策略权交给最终 binary / 上层 crate 的 feature 统一 |
| **证据** | `Cargo.toml`、`README.md`、`build.rs` |
| **置信度** | confirmed |

### 10.4 `links = "z"` 与构建脚本 metadata

| 字段 | 内容 |
|------|------|
| **决策** | 声明 `links` 键并输出 `cargo:root`、`cargo:include` |
| **C 侧事实** | 单一原生库 `libz` |
| **Rust 侧事实** | `links = "z"`；构建时写 include 路径供依赖 crate（如需要） |
| **推断动机** | Cargo 原生链接去重；与 `openssl-sys` 等生态惯例一致 |
| **证据** | `Cargo.toml`、`build.rs` |
| **置信度** | inferred |

### 10.5 双 crate（`libz-sys` / `libz-ng-sys`）同源发布

| 字段 | 内容 |
|------|------|
| **决策** | 同仓库两套 manifest，`libz-ng-sys` 用 `links = "z-ng"` + `cfg(zng)` |
| **C 侧事实** | zlib-ng 提供 compat 与 native 两套符号策略 |
| **Rust 侧事实** | `Cargo-zng.toml` 排除 stock zlib 路径；`maint` 统一发布 |
| **推断动机** | 需要 native `zng_*` 的 Rust 程序可与 stock zlib 共存；compat 仍走 `libz-sys` 单一符号空间 |
| **证据** | `Cargo-zng.toml`、`README-zng.md`、`MAINTENANCE.md` |
| **置信度** | confirmed |

---

## 11. 待澄清

| # | 问题 |
|---|------|
| 1 | vendored `src/zlib` / `src/zlib-ng` 相对上游的确切 commit/版本标签（仅见 `ZLIB_VERSION "1.3.2"`，ng 需对 submodule/文档核对） |
| 2 | 未绑定 API（`deflatePending` 等）是否有意永久省略，或计划按 zlib 版本补全 |
| 3 | `libz-ng-sys` 的 `systest` 是否使用 `systest/Cargo-zng.toml` 及与主 `systest` 的差异（未读 `Cargo-zng.toml` 全文） |
| 4 | 在「系统 libz + 内嵌头文件版本不一致」边缘场景下，ctypes 测试与运行时行为假设（仅静态阅读无法验证） |

---

## 完成定义自检（project-scope §8）

- [x] §0–§11 共 12 章
- [x] §0 可独立说明定位
- [x] §10 ≥ 3 条架构决策推断
- [x] §9 约 8–15 条证据
- [x] §8 examples 简单阅读结论
- [x] 无跨项目对比
- [x] 未声称执行构建验证
