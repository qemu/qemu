/*
 * Accelerator interface, specializes CPUClass
 * This header is used only by target-specific code.
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef ACCEL_CPU_H
#define ACCEL_CPU_H

/*
 * This header is used to define new accelerator-specific target-specific
 * accelerator cpu subclasses.
 * It uses CPU_RESOLVING_TYPE, so this is clearly target-specific.
 *
 * Do not try to use for any other purpose than the implementation of new
 * subclasses in target/, or the accel implementation itself in accel/
 */

#define TYPE_ACCEL_CPU "accel-" CPU_RESOLVING_TYPE
#define ACCEL_CPU_NAME(name) (name "-" TYPE_ACCEL_CPU)
typedef struct AccelCPUClass AccelCPUClass;
DECLARE_CLASS_CHECKERS(AccelCPUClass, ACCEL_CPU, TYPE_ACCEL_CPU)

typedef struct AccelCPUClass {
    /*< private >*/
    ObjectClass parent_class;
    /*< public >*/

    void (*cpu_class_init)(CPUClass *cc);
    void (*cpu_instance_init)(CPUState *cpu);
    bool (*cpu_target_realize)(CPUState *cpu, Error **errp);
} AccelCPUClass;

#endif /* ACCEL_CPU_H */
