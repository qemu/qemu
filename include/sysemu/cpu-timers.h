/*
 * CPU timers state API
 *
 * Copyright 2020 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef SYSEMU_CPU_TIMERS_H
#define SYSEMU_CPU_TIMERS_H

#include "qemu/timer.h"

/* init the whole cpu timers API, including icount, ticks, and cpu_throttle */
void cpu_timers_init(void);

/* icount - Instruction Counter API */

/*
 * icount enablement state:
 *
 * 0 = Disabled - Do not count executed instructions.
 * 1 = Enabled - Fixed conversion of insn to ns via "shift" option
 * 2 = Enabled - Runtime adaptive algorithm to compute shift
 */
#ifdef CONFIG_TCG
extern int use_icount;
#define icount_enabled() (use_icount)
#else
#define icount_enabled() 0
#endif

/*
 * Update the icount with the executed instructions. Called by
 * cpus-tcg vCPU thread so the main-loop can see time has moved forward.
 */
void cpu_update_icount(CPUState *cpu);

/* get raw icount value */
int64_t cpu_get_icount_raw(void);

/* return the virtual CPU time in ns, based on the instruction counter. */
int64_t cpu_get_icount(void);
/*
 * convert an instruction counter value to ns, based on the icount shift.
 * This shift is set as a fixed value with the icount "shift" option
 * (precise mode), or it is constantly approximated and corrected at
 * runtime in adaptive mode.
 */
int64_t cpu_icount_to_ns(int64_t icount);

/* configure the icount options, including "shift" */
void configure_icount(QemuOpts *opts, Error **errp);

/* used by tcg vcpu thread to calc icount budget */
int64_t qemu_icount_round(int64_t count);

/* if the CPUs are idle, start accounting real time to virtual clock. */
void qemu_start_warp_timer(void);
void qemu_account_warp_timer(void);

/*
 * CPU Ticks and Clock
 */

/* Caller must hold BQL */
void cpu_enable_ticks(void);
/* Caller must hold BQL */
void cpu_disable_ticks(void);

/*
 * return the time elapsed in VM between vm_start and vm_stop.  Unless
 * icount is active, cpu_get_ticks() uses units of the host CPU cycle
 * counter.
 */
int64_t cpu_get_ticks(void);

/*
 * Returns the monotonic time elapsed in VM, i.e.,
 * the time between vm_start and vm_stop
 */
int64_t cpu_get_clock(void);

void qemu_timer_notify_cb(void *opaque, QEMUClockType type);

#endif /* SYSEMU_CPU_TIMERS_H */
