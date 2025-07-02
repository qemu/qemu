/*
 * Accelerator handlers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_OPS_H
#define ACCEL_OPS_H

#include "exec/hwaddr.h"
#include "qemu/accel.h"
#include "qom/object.h"

struct AccelState {
    Object parent_obj;
};

struct AccelClass {
    ObjectClass parent_class;

    const char *name;
    /* Cached by accel_init_ops_interfaces() when created */
    AccelOpsClass *ops;

    int (*init_machine)(AccelState *as, MachineState *ms);
    bool (*cpu_common_realize)(CPUState *cpu, Error **errp);
    void (*cpu_common_unrealize)(CPUState *cpu);
    /* get_stats: Append statistics to @buf */
    void (*get_stats)(AccelState *as, GString *buf);

    /* system related hooks */
    void (*setup_post)(AccelState *as);
    void (*pre_resume_vm)(AccelState *as, bool step_pending);
    bool (*has_memory)(AccelState *accel, AddressSpace *as,
                       hwaddr start_addr, hwaddr size);

    /* gdbstub related hooks */
    int (*gdbstub_supported_sstep_flags)(AccelState *as);

    bool *allowed;
    /*
     * Array of global properties that would be applied when specific
     * accelerator is chosen. It works like MachineClass.compat_props
     * but it's for accelerators not machines. Accelerator-provided
     * global properties may be overridden by machine-type
     * compat_props or user-provided global properties.
     */
    GPtrArray *compat_props;
};

#endif /* ACCEL_OPS_H */
