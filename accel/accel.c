/*
 * QEMU System Emulator, accelerator interfaces
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
#include "sysemu/accel.h"
#include "hw/boards.h"
#include "qemu-common.h"
#include "sysemu/arch_init.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"
#include "hw/xen/xen.h"
#include "qom/object.h"
#include "qemu/error-report.h"

static const TypeInfo accel_type = {
    .name = TYPE_ACCEL,
    .parent = TYPE_OBJECT,
    .class_size = sizeof(AccelClass),
    .instance_size = sizeof(AccelState),
};

/* Lookup AccelClass from opt_name. Returns NULL if not found */
static AccelClass *accel_find(const char *opt_name)
{
    char *class_name = g_strdup_printf(ACCEL_CLASS_NAME("%s"), opt_name);
    AccelClass *ac = ACCEL_CLASS(object_class_by_name(class_name));
    g_free(class_name);
    return ac;
}

static int accel_init_machine(AccelClass *acc, MachineState *ms)
{
    ObjectClass *oc = OBJECT_CLASS(acc);
    const char *cname = object_class_get_name(oc);
    AccelState *accel = ACCEL(object_new(cname));
    int ret;
    ms->accelerator = accel;
    *(acc->allowed) = true;
    ret = acc->init_machine(ms);
    if (ret < 0) {
        ms->accelerator = NULL;
        *(acc->allowed) = false;
        object_unref(OBJECT(accel));
    }
    return ret;
}

void configure_accelerator(MachineState *ms)
{
    const char *accel, *p;
    char buf[10];
    int ret;
    bool accel_initialised = false;
    bool init_failed = false;
    AccelClass *acc = NULL;

    accel = qemu_opt_get(qemu_get_machine_opts(), "accel");
    if (accel == NULL) {
        /* Use the default "accelerator", tcg */
        accel = "tcg";
    }

    p = accel;
    while (!accel_initialised && *p != '\0') {
        if (*p == ':') {
            p++;
        }
        p = get_opt_name(buf, sizeof(buf), p, ':');
        acc = accel_find(buf);
        if (!acc) {
            continue;
        }
        if (acc->available && !acc->available()) {
            printf("%s not supported for this target\n",
                   acc->name);
            continue;
        }
        ret = accel_init_machine(acc, ms);
        if (ret < 0) {
            init_failed = true;
            error_report("failed to initialize %s: %s",
                         acc->name, strerror(-ret));
        } else {
            accel_initialised = true;
        }
    }

    if (!accel_initialised) {
        if (!init_failed) {
            error_report("-machine accel=%s: No accelerator found", accel);
        }
        exit(1);
    }

    if (init_failed) {
        error_report("Back to %s accelerator", acc->name);
    }
}

void accel_register_compat_props(AccelState *accel)
{
    AccelClass *class = ACCEL_GET_CLASS(accel);
    register_compat_props_array(class->global_props);
}

static void register_accel_types(void)
{
    type_register_static(&accel_type);
}

type_init(register_accel_types);
