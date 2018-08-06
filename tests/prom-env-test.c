/*
 * Test Open-Firmware-based machines.
 *
 * Copyright (c) 2016, 2017 Red Hat Inc.
 *
 * Author:
 *    Thomas Huth <thuth@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2
 * or later. See the COPYING file in the top-level directory.
 *
 * This test is used to check that some Open Firmware based machines (i.e.
 * OpenBIOS or SLOF) can be started successfully in TCG mode. To do this, we
 * first put some Forth code into the "boot-command" Open Firmware environment
 * variable. This Forth code writes a well-known magic value to a known location
 * in memory. Then we start the guest so that the firmware can boot and finally
 * run the Forth code.
 * The testing code here then can finally check whether the value has been
 * successfully written into the guest memory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define MAGIC   0xcafec0de
#define ADDRESS 0x4000

static void check_guest_memory(void)
{
    uint32_t signature;
    int i;

    /* Poll until code has run and modified memory. Wait at most 600 seconds */
    for (i = 0; i < 60000; ++i) {
        signature = readl(ADDRESS);
        if (signature == MAGIC) {
            break;
        }
        g_usleep(10000);
    }

    g_assert_cmphex(signature, ==, MAGIC);
}

static void test_machine(const void *machine)
{
    const char *extra_args;

    /* The pseries firmware boots much faster without the default devices */
    extra_args = strcmp(machine, "pseries") == 0 ? "-nodefaults" : "";

    global_qtest = qtest_initf("-M %s,accel=tcg %s "
                               "-prom-env 'use-nvramrc?=true' "
                               "-prom-env 'nvramrc=%x %x l!' ",
                               (const char *)machine, extra_args,
                               MAGIC, ADDRESS);
    check_guest_memory();
    qtest_quit(global_qtest);
}

static void add_tests(const char *machines[])
{
    int i;
    char *name;

    for (i = 0; machines[i] != NULL; i++) {
        name = g_strdup_printf("prom-env/%s", machines[i]);
        qtest_add_data_func(name, machines[i], test_machine);
        g_free(name);
    }
}

int main(int argc, char *argv[])
{
    const char *sparc_machines[] = { "SPARCbook", "Voyager", "SS-20", NULL };
    const char *sparc64_machines[] = { "sun4u", NULL };
    const char *ppc_machines[] = { "mac99", "g3beige", NULL };
    const char *arch = qtest_get_arch();

    g_test_init(&argc, &argv, NULL);

    if (!strcmp(arch, "ppc")) {
        add_tests(ppc_machines);
    } else if (!strcmp(arch, "ppc64")) {
        add_tests(ppc_machines);
        if (g_test_slow()) {
            qtest_add_data_func("prom-env/pseries", "pseries", test_machine);
        }
    } else if (!strcmp(arch, "sparc")) {
        add_tests(sparc_machines);
    } else if (!strcmp(arch, "sparc64")) {
        add_tests(sparc64_machines);
    } else {
        g_assert_not_reached();
    }

    return g_test_run();
}
