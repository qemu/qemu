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

void dirtylimit_state_lock(void);
void dirtylimit_state_unlock(void);
void dirtylimit_state_initialize(void);
void dirtylimit_state_finalize(void);
bool dirtylimit_in_service(void);
bool dirtylimit_vcpu_index_valid(int cpu_index);
void dirtylimit_process(void);
void dirtylimit_change(bool start);
void dirtylimit_set_vcpu(int cpu_index,
                         uint64_t quota,
                         bool enable);
void dirtylimit_set_all(uint64_t quota,
                        bool enable);
void dirtylimit_vcpu_execute(CPUState *cpu);
#endif
