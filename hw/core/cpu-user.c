/*
 * QEMU CPU model (user specific)
 *
 * Copyright (c) Linaro, Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "hw/core/cpu.h"
#include "migration/vmstate.h"

static const Property cpu_user_props[] = {
    /*
     * Create a property for the user-only object, so users can
     * adjust prctl(PR_SET_UNALIGN) from the command-line.
     * Has no effect if the target does not support the feature.
     */
    DEFINE_PROP_BOOL("prctl-unalign-sigbus", CPUState,
                     prctl_unalign_sigbus, false),
};

void cpu_class_init_props(DeviceClass *dc)
{
    device_class_set_props(dc, cpu_user_props);
}

void cpu_exec_class_post_init(CPUClass *cc)
{
    /* nothing to do */
}

void cpu_exec_initfn(CPUState *cpu)
{
    /* nothing to do */
}

void cpu_vmstate_register(CPUState *cpu)
{
    assert(qdev_get_vmsd(DEVICE(cpu)) == NULL ||
           qdev_get_vmsd(DEVICE(cpu))->unmigratable);
}

void cpu_vmstate_unregister(CPUState *cpu)
{
    /* nothing to do */
}
