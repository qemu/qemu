/*
 * PXE test cases.
 *
 * Copyright (c) 2016, 2017 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *  Victor Kaplansky <victork@redhat.com>
 *  Thomas Huth <thuth@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>
#include "qemu-common.h"
#include "libqtest.h"
#include "boot-sector.h"
#include "libqos/libqos-spapr.h"

#define NETNAME "net0"

static char disk[] = "tests/pxe-test-disk-XXXXXX";

typedef struct testdef {
    const char *machine;    /* Machine type */
    const char *model;      /* NIC device model */
    const char *extra;      /* Any additional parameters */
} testdef_t;

static testdef_t x86_tests[] = {
    { "pc", "e1000" },
    { "pc", "virtio-net-pci" },
    { "q35", "e1000e" },
    { "q35", "virtio-net-pci", },
    { NULL },
};

static testdef_t x86_tests_slow[] = {
    { "pc", "ne2k_pci", },
    { "pc", "i82550", },
    { "pc", "rtl8139" },
    { "pc", "vmxnet3" },
    { NULL },
};

static testdef_t ppc64_tests[] = {
    { "pseries", "spapr-vlan",
      "-machine vsmt=8," PSERIES_DEFAULT_CAPABILITIES },
    { "pseries", "virtio-net-pci",
      "-machine vsmt=8," PSERIES_DEFAULT_CAPABILITIES },
    { NULL },
};

static testdef_t ppc64_tests_slow[] = {
    { "pseries", "e1000",
      "-machine vsmt=8," PSERIES_DEFAULT_CAPABILITIES },
    { NULL },
};

static testdef_t s390x_tests[] = {
    { "s390-ccw-virtio", "virtio-net-ccw" },
    { NULL },
};

static void test_pxe_one(const testdef_t *test, bool ipv6)
{
    QTestState *qts;
    char *args;
    const char *extra = test->extra;

    if (!extra) {
        extra = "";
    }

    args = g_strdup_printf(
        "-accel kvm -accel tcg -machine %s -nodefaults -boot order=n "
        "-netdev user,id=" NETNAME ",tftp=./,bootfile=%s,ipv4=%s,ipv6=%s "
        "-device %s,bootindex=1,netdev=" NETNAME " %s",
        test->machine, disk, ipv6 ? "off" : "on", ipv6 ? "on" : "off",
        test->model, extra);

    qts = qtest_init(args);
    boot_sector_test(qts);
    qtest_quit(qts);
    g_free(args);
}

static void test_pxe_ipv4(gconstpointer data)
{
    const testdef_t *test = data;

    test_pxe_one(test, false);
}

static void test_pxe_ipv6(gconstpointer data)
{
    const testdef_t *test = data;

    test_pxe_one(test, true);
}

static void test_batch(const testdef_t *tests, bool ipv6)
{
    int i;

    for (i = 0; tests[i].machine; i++) {
        const testdef_t *test = &tests[i];
        char *testname;

        testname = g_strdup_printf("pxe/ipv4/%s/%s",
                                   test->machine, test->model);
        qtest_add_data_func(testname, test, test_pxe_ipv4);
        g_free(testname);

        if (ipv6) {
            testname = g_strdup_printf("pxe/ipv6/%s/%s",
                                       test->machine, test->model);
            qtest_add_data_func(testname, test, test_pxe_ipv6);
            g_free(testname);
        }
    }
}

int main(int argc, char *argv[])
{
    int ret;
    const char *arch = qtest_get_arch();

    ret = boot_sector_init(disk);
    if(ret)
        return ret;

    g_test_init(&argc, &argv, NULL);

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        test_batch(x86_tests, false);
        if (g_test_slow()) {
            test_batch(x86_tests_slow, false);
        }
    } else if (strcmp(arch, "ppc64") == 0) {
        test_batch(ppc64_tests, g_test_slow());
        if (g_test_slow()) {
            test_batch(ppc64_tests_slow, true);
        }
    } else if (g_str_equal(arch, "s390x")) {
        test_batch(s390x_tests, g_test_slow());
    }
    ret = g_test_run();
    boot_sector_cleanup(disk);
    return ret;
}
