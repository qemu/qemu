/*
 * QEMU accel class, system emulation components
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qapi/qapi-commands-accelerator.h"
#include "monitor/monitor.h"
#include "hw/boards.h"
#include "hw/core/cpu.h"
#include "accel/accel-ops.h"
#include "accel/accel-cpu-ops.h"
#include "system/cpus.h"
#include "qemu/error-report.h"
#include "accel-internal.h"

int accel_init_machine(AccelState *accel, MachineState *ms)
{
    AccelClass *acc = ACCEL_GET_CLASS(accel);
    int ret;
    ms->accelerator = accel;
    *(acc->allowed) = true;
    ret = acc->init_machine(accel, ms);
    if (ret < 0) {
        ms->accelerator = NULL;
        *(acc->allowed) = false;
        object_unref(OBJECT(accel));
    } else {
        object_set_accelerator_compat_props(acc->compat_props);
    }
    return ret;
}

AccelState *current_accel(void)
{
    return current_machine->accelerator;
}

void accel_setup_post(MachineState *ms)
{
    AccelState *accel = ms->accelerator;
    AccelClass *acc = ACCEL_GET_CLASS(accel);
    if (acc->setup_post) {
        acc->setup_post(accel);
    }
}

void accel_pre_resume(MachineState *ms, bool step_pending)
{
    AccelState *accel = ms->accelerator;
    AccelClass *acc = ACCEL_GET_CLASS(accel);
    if (acc->pre_resume_vm) {
        acc->pre_resume_vm(accel, step_pending);
    }
}

/* initialize the arch-independent accel operation interfaces */
void accel_init_ops_interfaces(AccelClass *ac)
{
    const char *ac_name;
    char *ops_name;
    ObjectClass *oc;
    AccelOpsClass *ops;

    ac_name = object_class_get_name(OBJECT_CLASS(ac));
    g_assert(ac_name != NULL);

    ops_name = g_strdup_printf("%s" ACCEL_OPS_SUFFIX, ac_name);
    oc = module_object_class_by_name(ops_name);
    if (!oc) {
        error_report("fatal: could not load module for type '%s'", ops_name);
        exit(1);
    }
    g_free(ops_name);
    /*
     * all accelerators need to define ops, providing at least a mandatory
     * non-NULL create_vcpu_thread operation.
     */
    ops = ACCEL_OPS_CLASS(oc);
    ac->ops = ops;
    if (ops->ops_init) {
        ops->ops_init(ac);
    }
    cpus_register_accel(ops);
}

static void accel_ops_class_init(ObjectClass *oc, const void *data)
{
    monitor_register_hmp_info_hrt("accel", qmp_x_accel_stats);
}

static const TypeInfo accel_ops_type_info = {
    .name = TYPE_ACCEL_OPS,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(AccelOpsClass),
    .class_init = accel_ops_class_init,
};

static void accel_system_register_types(void)
{
    type_register_static(&accel_ops_type_info);
}
type_init(accel_system_register_types);
