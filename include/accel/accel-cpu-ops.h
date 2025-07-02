/*
 * Accelerator per-vCPU handlers
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_ACCEL_CPU_OPS_H
#define QEMU_ACCEL_CPU_OPS_H

#include "qemu/accel.h"
#include "exec/vaddr.h"
#include "qom/object.h"

#define ACCEL_OPS_SUFFIX "-ops"
#define TYPE_ACCEL_OPS "accel" ACCEL_OPS_SUFFIX
#define ACCEL_OPS_NAME(name) (name "-" TYPE_ACCEL_OPS)

DECLARE_CLASS_CHECKERS(AccelOpsClass, ACCEL_OPS, TYPE_ACCEL_OPS)

/**
 * struct AccelOpsClass - accelerator interfaces
 *
 * This structure is used to abstract accelerator differences from the
 * core CPU code. Not all have to be implemented.
 */
struct AccelOpsClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    /* initialization function called when accel is chosen */
    void (*ops_init)(AccelClass *ac);

    bool (*cpus_are_resettable)(void);
    void (*cpu_reset_hold)(CPUState *cpu);

    void (*create_vcpu_thread)(CPUState *cpu); /* MANDATORY NON-NULL */
    void (*kick_vcpu_thread)(CPUState *cpu);
    bool (*cpu_thread_is_idle)(CPUState *cpu);

    /**
     * synchronize_post_reset:
     * synchronize_post_init:
     * @cpu: The vCPU to synchronize.
     *
     * Request to synchronize QEMU vCPU registers to the hardware accelerator
     * (QEMU is the reference).
     */
    void (*synchronize_post_reset)(CPUState *cpu);
    void (*synchronize_post_init)(CPUState *cpu);
    /**
     * synchronize_state:
     * synchronize_pre_loadvm:
     * @cpu: The vCPU to synchronize.
     *
     * Request to synchronize QEMU vCPU registers from the hardware accelerator
     * (the hardware accelerator is the reference).
     */
    void (*synchronize_state)(CPUState *cpu);
    void (*synchronize_pre_loadvm)(CPUState *cpu);

    /* handle_interrupt is mandatory. */
    void (*handle_interrupt)(CPUState *cpu, int mask);

    /* get_vcpu_stats: Append statistics of this @cpu to @buf */
    void (*get_vcpu_stats)(CPUState *cpu, GString *buf);

    /**
     * @get_virtual_clock: fetch virtual clock
     * @set_virtual_clock: set virtual clock
     *
     * These allow the timer subsystem to defer to the accelerator to
     * fetch time. The set function is needed if the accelerator wants
     * to track the changes to time as the timer is warped through
     * various timer events.
     */
    int64_t (*get_virtual_clock)(void);
    void (*set_virtual_clock)(int64_t time);

    int64_t (*get_elapsed_ticks)(void);

    /* gdbstub hooks */
    bool (*supports_guest_debug)(void);
    int (*update_guest_debug)(CPUState *cpu);
    int (*insert_breakpoint)(CPUState *cpu, int type, vaddr addr, vaddr len);
    int (*remove_breakpoint)(CPUState *cpu, int type, vaddr addr, vaddr len);
    void (*remove_all_breakpoints)(CPUState *cpu);
};

void generic_handle_interrupt(CPUState *cpu, int mask);

#endif /* QEMU_ACCEL_CPU_OPS_H */
