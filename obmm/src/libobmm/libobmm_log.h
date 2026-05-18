/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 * libobmm is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 *
 * Description: libobmm logging interface
 * Author: Gao Chao
 * Create: 2026-01-28
 */

#ifndef LIBOBMM_LOG_H
#define LIBOBMM_LOG_H

#include <stdint.h>
#include <syslog.h>
#include <time.h>

/* OBMM Logging configuration */
#define OBMM_LOG_FACILITY LOG_LOCAL0

/* Time conversion constants */
#define NSEC_PER_MSEC  1000000  /* Nanoseconds per millisecond */
#define MSEC_PER_SEC   1000     /* Milliseconds per second */

/* Format 16-byte EID (deid/seid) directly - no buffer needed */
#define EID_FMT64 "%#016llx:%#016llx"
#define EID_ARGS64(eid) (unsigned long long)*(uint64_t *)&(eid)[8], (unsigned long long)*(uint64_t *)&(eid)[0]

/* Calculate elapsed time in milliseconds */
static inline long obmm_elapsed_ms(const struct timespec *start)
{
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    return (end.tv_sec - start->tv_sec) * MSEC_PER_SEC +
           (end.tv_nsec - start->tv_nsec) / NSEC_PER_MSEC;
}

/* Convenience logging macros for different levels */
#define OBMM_LOGE(fmt, ...) \
    syslog(OBMM_LOG_FACILITY | LOG_ERR, \
           "[OBMM] %s: " fmt, __func__, ##__VA_ARGS__)

#define OBMM_LOGW(fmt, ...) \
    syslog(OBMM_LOG_FACILITY | LOG_WARNING, \
           "[OBMM] %s: " fmt, __func__, ##__VA_ARGS__)

#define OBMM_LOGI(fmt, ...) \
    syslog(OBMM_LOG_FACILITY | LOG_INFO, \
           "[OBMM] %s: " fmt, __func__, ##__VA_ARGS__)

#define OBMM_LOGD(fmt, ...) \
    syslog(OBMM_LOG_FACILITY | LOG_DEBUG, \
           "[OBMM] %s: " fmt, __func__, ##__VA_ARGS__)

/* Public logging macros for operation tracking - parameters passed through directly.
 * NOTE: OBMM_LOG_START must be followed by OBMM_LOG_SUCCESS/OBMM_LOG_FAIL in the same block.
 * Multiple OBMM_LOG_START calls in the same function need separate blocks. */
#define OBMM_LOG_START(fmt, ...) \
    struct timespec _obmm_ts; \
    clock_gettime(CLOCK_REALTIME, &_obmm_ts); \
    syslog(OBMM_LOG_FACILITY | LOG_INFO, "[OBMM] %s: " fmt, __func__, ##__VA_ARGS__)

#define OBMM_LOG_SUCCESS(fmt, ...) \
    syslog(OBMM_LOG_FACILITY | LOG_INFO, \
           "[OBMM] %s: SUCCESS, elapsed=%ldms, " fmt, \
           __func__, obmm_elapsed_ms(&_obmm_ts), ##__VA_ARGS__)

#define OBMM_LOG_FAIL(fmt, ...) \
    syslog(OBMM_LOG_FACILITY | LOG_ERR, \
           "[OBMM] %s: FAILED, " fmt, __func__, ##__VA_ARGS__)

#endif /* LIBOBMM_LOG_H */
