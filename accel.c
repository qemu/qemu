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

#include "sysemu/accel.h"
#include "qemu-common.h"
#include "sysemu/arch_init.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"
#include "hw/xen/xen.h"
#include "qom/object.h"

int tcg_tb_size;
static bool tcg_allowed = true;

static int tcg_init(MachineClass *mc)
{
    tcg_exec_init(tcg_tb_size * 1024 * 1024);
    return 0;
}

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

int configure_accelerator(MachineClass *mc)
{
    const char *p;
    char buf[10];
    int ret;
    bool accel_initialised = false;
    bool init_failed = false;
    AccelClass *acc = NULL;

    p = qemu_opt_get(qemu_get_machine_opts(), "accel");
    if (p == NULL) {
        /* Use the default "accelerator", tcg */
        p = "tcg";
    }

    while (!accel_initialised && *p != '\0') {
        if (*p == ':') {
            p++;
        }
        p = get_opt_name(buf, sizeof(buf), p, ':');
        acc = accel_find(buf);
        if (!acc) {
            fprintf(stderr, "\"%s\" accelerator does not exist.\n", buf);
            continue;
        }
        if (!acc->available()) {
            printf("%s not supported for this target\n",
                   acc->name);
            continue;
        }
        *(acc->allowed) = true;
        ret = acc->init(mc);
        if (ret < 0) {
            init_failed = true;
            fprintf(stderr, "failed to initialize %s: %s\n",
                    acc->name,
                    strerror(-ret));
            *(acc->allowed) = false;
        } else {
            accel_initialised = true;
        }
    }

    if (!accel_initialised) {
        if (!init_failed) {
            fprintf(stderr, "No accelerator found!\n");
        }
        exit(1);
    }

    if (init_failed) {
        fprintf(stderr, "Back to %s accelerator.\n", acc->name);
    }

    return !accel_initialised;
}


static void tcg_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "tcg";
    ac->available = tcg_available;
    ac->init = tcg_init;
    ac->allowed = &tcg_allowed;
}

#define TYPE_TCG_ACCEL ACCEL_CLASS_NAME("tcg")

static const TypeInfo tcg_accel_type = {
    .name = TYPE_TCG_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = tcg_accel_class_init,
};

static void xen_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "Xen";
    ac->available = xen_available;
    ac->init = xen_init;
    ac->allowed = &xen_allowed;
}

#define TYPE_XEN_ACCEL ACCEL_CLASS_NAME("xen")

static const TypeInfo xen_accel_type = {
    .name = TYPE_XEN_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = xen_accel_class_init,
};

static void kvm_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "KVM";
    ac->available = kvm_available;
    ac->init = kvm_init;
    ac->allowed = &kvm_allowed;
}

#define TYPE_KVM_ACCEL ACCEL_CLASS_NAME("kvm")

static const TypeInfo kvm_accel_type = {
    .name = TYPE_KVM_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = kvm_accel_class_init,
};

static void qtest_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "QTest";
    ac->available = qtest_available;
    ac->init = qtest_init_accel;
    ac->allowed = &qtest_allowed;
}

#define TYPE_QTEST_ACCEL ACCEL_CLASS_NAME("qtest")

static const TypeInfo qtest_accel_type = {
    .name = TYPE_QTEST_ACCEL,
    .parent = TYPE_ACCEL,
    .class_init = qtest_accel_class_init,
};

static void register_accel_types(void)
{
    type_register_static(&accel_type);
    type_register_static(&tcg_accel_type);
    type_register_static(&xen_accel_type);
    type_register_static(&kvm_accel_type);
    type_register_static(&qtest_accel_type);
}

type_init(register_accel_types);
