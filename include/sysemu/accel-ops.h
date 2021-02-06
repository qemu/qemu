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

    void (*create_vcpu_thread)(CPUState *cpu); /* MANDATORY NON-NULL */
    void (*kick_vcpu_thread)(CPUState *cpu);

    void (*synchronize_post_reset)(CPUState *cpu);
    void (*synchronize_post_init)(CPUState *cpu);
    void (*synchronize_state)(CPUState *cpu);
    void (*synchronize_pre_loadvm)(CPUState *cpu);

    void (*handle_interrupt)(CPUState *cpu, int mask);

    int64_t (*get_virtual_clock)(void);
    int64_t (*get_elapsed_ticks)(void);
};

#endif /* ACCEL_OPS_H */
