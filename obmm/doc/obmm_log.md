# obmm_log: OBMM日志查看与使用

## 名称 NAME

`obmm_log` - OBMM日志系统使用说明

## 描述 DESCRIPTION

OBMM库内置了日志记录功能，自动记录所有export/import/unexport/unimport等操作的详细信息，包括操作参数、执行结果、耗时等。本文档说明如何查看和使用OBMM日志进行问题诊断和性能分析。

**注意**：日志功能由OBMM库内部自动调用，用户无需在代码中添加任何日志相关代码。

## 日志位置 LOG LOCATION

### 默认输出位置

OBMM日志通过系统syslog机制输出，默认位置为：

* **系统日志**：
  - **openEuler/RHEL/CentOS**: `/var/log/messages`
  - **Debian/Ubuntu**: `/var/log/syslog`
* **日志设施**：`LOG_LOCAL0`
* **日志标识**：包含 `[OBMM]` 前缀

### 查找OBMM日志

由于OBMM日志输出到系统syslog，可以通过以下方式查看：

```bash
# 查看系统日志中的OBMM日志（openEuler/RHEL/CentOS）
tail -f /var/log/messages | grep OBMM

# 查看系统日志中的OBMM日志（Debian/Ubuntu）
tail -f /var/log/syslog | grep OBMM
```
## 日志格式 LOG FORMAT

### 基本格式

每条OBMM日志遵循以下格式：

```
<时间戳> <主机名> <进程名>[<PID>]: [OBMM] <函数名>: <日志内容>
```

### 日志级别

OBMM使用以下syslog级别：

* **ERR (错误)**：操作失败，包含错误原因
* **WARNING (警告)**：警告信息
* **INFO (信息)**：正常操作流程，包括操作开始和成功
* **DEBUG (调试)**：详细调试信息

### 日志字段说明

#### 操作开始日志

当OBMM操作开始时，记录操作参数：

```
[OBMM] obmm_export: START pid=0 length=0x200000 deid=0x1234:0x5678 priv_len=0
[OBMM] obmm_import: START scna=0x1 {pa=0x100000000 length=0x200000} flags=0x0 nid=0 base_dist=0 seid=0xabcd:0xefgh priv_len=0
```
具体字段参见各接口文档。

#### 操作成功日志

操作成功时，记录返回值和耗时：

```
[OBMM] obmm_export: SUCCESS, mem_id=1, elapsed=15ms
[OBMM] obmm_import: SUCCESS, mem_id=2, elapsed=8ms
```

**字段说明**：
* **elapsed**：操作耗时（毫秒）

#### 操作失败日志

操作失败时，记录失败原因：

```
[OBMM] obmm_export: FAILED, length or desc is NULL
[OBMM] obmm_import: FAILED, failed to get device fd
[OBMM] obmm_export: FAILED, operation failed
```

## 问题诊断 TROUBLESHOOTING

### 常见问题

#### 1. 日志未输出

**现象**：在系统日志中找不到OBMM日志

**排查步骤**：

1. 确认应用正在使用OBMM库
2. 检查日志文件是否存在：
```bash
ls -l /var/log/messages  # openEuler/RHEL/CentOS
ls -l /var/log/syslog    # Debian/Ubuntu
```

3. 检查syslog服务状态：
```bash
sudo systemctl status rsyslog  # 大多数系统
sudo systemctl status syslog   # 某些系统
```

#### 2. 日志信息不全

**现象**：只看到部分操作的日志

**可能原因**：
* 应用程序未正常调用OBMM接口
* 日志被系统配置过滤
* 日志文件已被轮转清理
* syslog服务错误

**解决方法**：
1. 检查应用日志，确认OBMM接口调用情况
2. 查看较早的日志文件（如果存在）
3. 检查系统日志轮转配置，查看归档日志文件
4. 尝试重启syslog服务

#### 3. 日志时间戳不准确

**现象**：日志中的时间与系统时间不一致

**解决方法**：
```bash
# 检查系统时区
timedatectl

# 同步系统时间
sudo systemctl restart systemd-timesyncd
```


### 日志管理建议

虽然OBMM本身不提供日志轮转配置，但建议系统管理员：

1. 根据系统日志轮转策略管理OBMM日志
2. 监控日志文件大小，避免占用过多磁盘空间
3. 定期备份重要的日志数据用于分析
4. 考虑使用日志集中收集工具（如ELK、Splunk）

## 相关文档

obmm_export(3), obmm_import(3), obmm_unexport(3), obmm_unimport(3), syslog(3)
