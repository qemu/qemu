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

int tcg_tb_size;
static bool tcg_allowed = true;

static int tcg_init(MachineClass *mc)
{
    tcg_exec_init(tcg_tb_size * 1024 * 1024);
    return 0;
}

typedef struct AccelType {
    const char *opt_name;
    const char *name;
    int (*available)(void);
    int (*init)(MachineClass *mc);
    bool *allowed;
} AccelType;

static AccelType accel_list[] = {
    { "tcg", "tcg", tcg_available, tcg_init, &tcg_allowed },
    { "xen", "Xen", xen_available, xen_init, &xen_allowed },
    { "kvm", "KVM", kvm_available, kvm_init, &kvm_allowed },
    { "qtest", "QTest", qtest_available, qtest_init_accel, &qtest_allowed },
};

/* Lookup AccelType from opt_name. Returns NULL if not found */
static AccelType *accel_find(const char *opt_name)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(accel_list); i++) {
        AccelType *acc = &accel_list[i];
        if (acc->opt_name && strcmp(acc->opt_name, opt_name) == 0) {
            return acc;
        }
    }
    return NULL;
}

int configure_accelerator(MachineClass *mc)
{
    const char *p;
    char buf[10];
    int ret;
    bool accel_initialised = false;
    bool init_failed = false;
    AccelType *acc = NULL;

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
