/*
 * Dirty page rate limit common functions
 *
 * Copyright (c) 2022 CHINA TELECOM CO.,LTD.
 *
 * Authors:
 *  Hyman Huang(黄勇) <huangy81@chinatelecom.cn>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef QEMU_DIRTYRLIMIT_H
#define QEMU_DIRTYRLIMIT_H

#define DIRTYLIMIT_CALC_TIME_MS         1000    /* 1000ms */

int64_t vcpu_dirty_rate_get(int cpu_index);
void vcpu_dirty_rate_stat_start(void);
void vcpu_dirty_rate_stat_stop(void);
void vcpu_dirty_rate_stat_initialize(void);
void vcpu_dirty_rate_stat_finalize(void);
#endif
