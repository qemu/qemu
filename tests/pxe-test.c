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

#define NETNAME "net0"

static char disk[] = "tests/pxe-test-disk-XXXXXX";

static void test_pxe_one(const char *params, bool ipv6)
{
    char *args;
    const char *machine_props;

    machine_props =
        strcmp(qtest_get_arch(), "ppc64") == 0 ? ",cap-htm=off" : "";

    args = g_strdup_printf("-machine accel=kvm:tcg%s -nodefaults -boot order=n "
                           "-netdev user,id=" NETNAME ",tftp=./,bootfile=%s,"
                           "ipv4=%s,ipv6=%s %s", machine_props, disk,
                           ipv6 ? "off" : "on", ipv6 ? "on" : "off", params);

    qtest_start(args);
    boot_sector_test();
    qtest_quit(global_qtest);
    g_free(args);
}

static void test_pxe_ipv4(gconstpointer data)
{
    const char *model = data;
    char *dev_arg;

    dev_arg = g_strdup_printf("-device %s,netdev=" NETNAME, model);
    test_pxe_one(dev_arg, false);
    g_free(dev_arg);
}

static void test_pxe_spapr_vlan(void)
{
    test_pxe_one("-device spapr-vlan,netdev=" NETNAME, true);
}

static void test_pxe_virtio_ccw(void)
{
    test_pxe_one("-device virtio-net-ccw,bootindex=1,netdev=" NETNAME, false);
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
        qtest_add_data_func("pxe/e1000", "e1000", test_pxe_ipv4);
        qtest_add_data_func("pxe/virtio", "virtio-net-pci", test_pxe_ipv4);
        if (g_test_slow()) {
            qtest_add_data_func("pxe/ne2000", "ne2k_pci", test_pxe_ipv4);
            qtest_add_data_func("pxe/eepro100", "i82550", test_pxe_ipv4);
            qtest_add_data_func("pxe/pcnet", "pcnet", test_pxe_ipv4);
            qtest_add_data_func("pxe/rtl8139", "rtl8139", test_pxe_ipv4);
            qtest_add_data_func("pxe/vmxnet3", "vmxnet3", test_pxe_ipv4);
        }
    } else if (strcmp(arch, "ppc64") == 0) {
        qtest_add_func("pxe/spapr-vlan", test_pxe_spapr_vlan);
        if (g_test_slow()) {
            qtest_add_data_func("pxe/virtio", "virtio-net-pci", test_pxe_ipv4);
            qtest_add_data_func("pxe/e1000", "e1000", test_pxe_ipv4);
        }
    } else if (g_str_equal(arch, "s390x")) {
        qtest_add_func("pxe/virtio-ccw", test_pxe_virtio_ccw);
    }
    ret = g_test_run();
    boot_sector_cleanup(disk);
    return ret;
}
