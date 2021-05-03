/*
 * Machine 'none' tests.
 *
 * Copyright (c) 2018 Red Hat Inc.
 *
 * Authors:
 *  Igor Mammedov <imammedo@redhat.com>,
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "qemu/cutils.h"
#include "libqos/libqtest.h"
#include "qapi/qmp/qdict.h"


struct arch2cpu {
    const char *arch;
    const char *cpu_model;
};

static struct arch2cpu cpus_map[] = {
    /* tested targets list */
    { "arm", "cortex-a15" },
    { "aarch64", "cortex-a57" },
    { "avr", "avr6-avr-cpu" },
    { "x86_64", "qemu64,apic-id=0" },
    { "i386", "qemu32,apic-id=0" },
    { "alpha", "ev67" },
    { "cris", "crisv32" },
    { "m68k", "m5206" },
    { "microblaze", "any" },
    { "microblazeel", "any" },
    { "mips", "4Kc" },
    { "mipsel", "I7200" },
    { "mips64", "20Kc" },
    { "mips64el", "I6500" },
    { "nios2", "FIXME" },
    { "or1k", "or1200" },
    { "ppc", "604" },
    { "ppc64", "power8e_v2.1" },
    { "s390x", "qemu" },
    { "sh4", "sh7750r" },
    { "sh4eb", "sh7751r" },
    { "sparc", "LEON2" },
    { "sparc64", "Fujitsu Sparc64" },
    { "tricore", "tc1796" },
    { "xtensa", "dc233c" },
    { "xtensaeb", "fsf" },
    { "hppa", "hppa" },
    { "riscv64", "rv64" },
    { "riscv32", "rv32" },
    { "rx", "rx62n" },
};

static const char *get_cpu_model_by_arch(const char *arch)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(cpus_map); i++) {
        if (!strcmp(arch, cpus_map[i].arch)) {
            return cpus_map[i].cpu_model;
        }
    }
    return NULL;
}

static void test_machine_cpu_cli(void)
{
    QDict *response;
    const char *arch = qtest_get_arch();
    const char *cpu_model = get_cpu_model_by_arch(arch);
    QTestState *qts;

    if (!cpu_model) {
        fprintf(stderr, "WARNING: cpu name for target '%s' isn't defined,"
                " add it to cpus_map\n", arch);
        return; /* TODO: die here to force all targets have a test */
    }
    qts = qtest_initf("-machine none -cpu '%s'", cpu_model);

    response = qtest_qmp(qts, "{ 'execute': 'quit' }");
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("machine/none/cpu_option", test_machine_cpu_cli);

    return g_test_run();
}
