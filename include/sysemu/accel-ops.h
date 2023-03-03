/*
 * Accelerator OPS, used for cpus.c module
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ACCEL_OPS_H
#define ACCEL_OPS_H

#include "exec/cpu-common.h"
#include "qom/object.h"

#define ACCEL_OPS_SUFFIX "-ops"
#define TYPE_ACCEL_OPS "accel" ACCEL_OPS_SUFFIX
#define ACCEL_OPS_NAME(name) (name "-" TYPE_ACCEL_OPS)

typedef struct AccelOpsClass AccelOpsClass;
DECLARE_CLASS_CHECKERS(AccelOpsClass, ACCEL_OPS, TYPE_ACCEL_OPS)

/* cpus.c operations interface */
struct AccelOpsClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    /* initialization function called when accel is chosen */
    void (*ops_init)(AccelOpsClass *ops);

    bool (*cpus_are_resettable)(void);

    void (*create_vcpu_thread)(CPUState *cpu); /* MANDATORY NON-NULL */
    void (*kick_vcpu_thread)(CPUState *cpu);
    bool (*cpu_thread_is_idle)(CPUState *cpu);

    void (*synchronize_post_reset)(CPUState *cpu);
    void (*synchronize_post_init)(CPUState *cpu);
    void (*synchronize_state)(CPUState *cpu);
    void (*synchronize_pre_loadvm)(CPUState *cpu);
    void (*synchronize_pre_resume)(bool step_pending);

    void (*handle_interrupt)(CPUState *cpu, int mask);

    int64_t (*get_virtual_clock)(void);
    int64_t (*get_elapsed_ticks)(void);

    /* gdbstub hooks */
    bool (*supports_guest_debug)(void);
    int (*update_guest_debug)(CPUState *cpu);
    int (*insert_breakpoint)(CPUState *cpu, int type, vaddr addr, vaddr len);
    int (*remove_breakpoint)(CPUState *cpu, int type, vaddr addr, vaddr len);
    void (*remove_all_breakpoints)(CPUState *cpu);
};

#endif /* ACCEL_OPS_H */
