/*
 * PXE test cases.
 *
 * Copyright (c) 2016 Red Hat Inc.
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>,
 *  Victor Kaplansky <victork@redhat.com>
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

static const char *disk = "tests/pxe-test-disk.raw";

static void test_pxe_one(const char *params)
{
    char *args;

    args = g_strdup_printf("-machine accel=tcg "
                           "-netdev user,id=" NETNAME ",tftp=./,bootfile=%s "
                           "%s ",
                           disk, params);

    qtest_start(args);
    boot_sector_test();
    qtest_quit(global_qtest);
    g_free(args);
}

static void test_pxe_e1000(void)
{
    test_pxe_one("-device e1000,netdev=" NETNAME);
}

static void test_pxe_virtio_pci(void)
{
    test_pxe_one("-device virtio-net-pci,netdev=" NETNAME);
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
        qtest_add_func("pxe/e1000", test_pxe_e1000);
        qtest_add_func("pxe/virtio", test_pxe_virtio_pci);
    }
    ret = g_test_run();
    boot_sector_cleanup(disk);
    return ret;
}
