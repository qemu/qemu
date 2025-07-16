/*
 * QMP commands related to accelerators
 *
 * Copyright (c) Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qapi/type-helpers.h"
#include "qapi/qapi-commands-accelerator.h"
#include "accel/accel-ops.h"
#include "accel/accel-cpu-ops.h"
#include "hw/core/cpu.h"

HumanReadableText *qmp_x_accel_stats(Error **errp)
{
    AccelState *accel = current_accel();
    AccelClass *acc = ACCEL_GET_CLASS(accel);
    g_autoptr(GString) buf = g_string_new("");

    if (acc->get_stats) {
        acc->get_stats(accel, buf);
    }
    if (acc->ops->get_vcpu_stats) {
        CPUState *cpu;

        CPU_FOREACH(cpu) {
            acc->ops->get_vcpu_stats(cpu, buf);
        }
    }

    return human_readable_text_from_str(buf);
}
