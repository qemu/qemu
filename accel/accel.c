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
#include "sysemu/arch_init.h"
#include "sysemu/sysemu.h"
#include "qom/object.h"

static const TypeInfo accel_type = {
    .name = TYPE_ACCEL,
    .parent = TYPE_OBJECT,
    .class_size = sizeof(AccelClass),
    .instance_size = sizeof(AccelState),
};

/* Lookup AccelClass from opt_name. Returns NULL if not found */
AccelClass *accel_find(const char *opt_name)
{
    char *class_name = g_strdup_printf(ACCEL_CLASS_NAME("%s"), opt_name);
    AccelClass *ac = ACCEL_CLASS(object_class_by_name(class_name));
    g_free(class_name);
    return ac;
}

int accel_init_machine(AccelState *accel, MachineState *ms)
{
    AccelClass *acc = ACCEL_GET_CLASS(accel);
    int ret;
    ms->accelerator = accel;
    *(acc->allowed) = true;
    ret = acc->init_machine(ms);
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
        acc->setup_post(ms, accel);
    }
}

static void register_accel_types(void)
{
    type_register_static(&accel_type);
}

type_init(register_accel_types);
