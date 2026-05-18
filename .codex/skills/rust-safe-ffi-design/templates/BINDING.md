# [库名] — 绑定与构建

## 1. 绑定策略选定

### 1.1 路径选择（必选其一或组合）

- [ ] **手写** `extern` + `#[repr(C)]`
- [ ] **预生成 bindgen**（检入 `bindings_*.rs`）
- [ ] **构建时 bindgen**（feature / 默认）
- [ ] **C wrapper**（`wrapper.c` + `cc`）
- [ ] **其他**：

### 1.2 选择理由

[结合 Q2 inline、API 规模、CI、维护人力；**明确排除**未选路径的原因。]

### 1.3 与 `-sys` / safe 边界

- 绑定代码仅存在于：`[路径]`
- safe 层 **不** 重复 bindgen

## 2. 头文件与 bindgen 配置

| 项 | 约定 |
|----|------|
| 入口头文件 | `wrapper.h` / … |
| allowlist / blocklist | |
| 手写替代类型 | （union、bitfield 等） |
| 再生命令 | `regenerate.sh` 或文档命令 |

## 3. `build.rs` 与链接

### 3.1 探测顺序

```text
[例如：pkg-config → 环境变量 → vendored feature]
```

### 3.2 `links`

- `links = "[name]"`：**有 / 无**
- 理由：

### 3.3 Feature（构建相关）

| Feature | 作用 |
|---------|------|
| | |

## 4. 测试与 ABI 守卫生

| 类型 | 方案 |
|------|------|
| systest / ctest2 | |
| 绑定再生检查 | |
| CI 无原生库路径 | |

## 5. 维护说明

- 升级上游 C 库时的步骤：
- 已知绑定难点：
