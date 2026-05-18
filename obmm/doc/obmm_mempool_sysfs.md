# obmm_mempool_sysfs: OBMM 内存池 sysfs

OBMM 内存池 sysfs 用于展示系统内各 NUMA 节点的内存池状态。

## 路径

内存池 sysfs 目录位于 `/sys/kernel/obmm_mempool/`，每个本地 NUMA 节点对应一个子目录 `obmm-${nid}/`，其中 `${nid}` 为 NUMA 节点编号。

```
obmm_mempool/
├── obmm-0/
│   ├── total
│   ├── used
│   ├── available_cleared
│   └── available_uncleared
├── obmm-1/
│   └── ...
└── ...
```

## 属性说明

**total**
- 类型：文本、十六进制数
- 描述：内存池中该 NUMA 节点的总内存大小（字节），包含已使用和可用的内存

**used**
- 类型：文本、十六进制数
- 描述：已分配给 OBMM 内存的内存大小（字节）

**available_cleared**
- 类型：文本、十六进制数
- 描述：已清零、可立即分配的内存大小（字节）。使用 obmm_export(3) 的 `OBMM_EXPORT_FLAG_FAST` 标志时仅从此处分配，可在调用前查询确认可用内存是否满足需求。

**available_uncleared**
- 类型：文本、十六进制数
- 描述：未清零、需要异步清零后才能分配的内存大小（字节）

## 使用示例

```bash
# 查看节点 0 的内存池状态
cat /sys/kernel/obmm_mempool/obmm-0/total
cat /sys/kernel/obmm_mempool/obmm-0/used
cat /sys/kernel/obmm_mempool/obmm-0/available_cleared
cat /sys/kernel/obmm_mempool/obmm-0/available_uncleared
```

## 注意事项

- 所有属性均为只读，反映读取时刻的瞬时状态
- 数值以十六进制格式显示，如 `0x40000000` 表示 1GB
- 内存池大小通过内核模块参数 `mempool_size` 配置
- 内存池会在系统内存不足时自动收缩，在内存充足时自动扩充
