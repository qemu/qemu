/*
 * QTest testcase for vga cards
 *
 * Copyright (c) 2014 Red Hat, Inc
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

static void pci_cirrus(void)
{
    qtest_start("-vga none -device cirrus-vga");
    qtest_end();
}

static void pci_stdvga(void)
{
    qtest_start("-vga none -device VGA");
    qtest_end();
}

static void pci_secondary(void)
{
    qtest_start("-vga none -device secondary-vga");
    qtest_end();
}

static void pci_multihead(void)
{
    qtest_start("-vga none -device VGA -device secondary-vga");
    qtest_end();
}

static void pci_virtio_gpu(void)
{
    qtest_start("-vga none -device virtio-gpu-pci");
    qtest_end();
}

#ifdef CONFIG_VIRTIO_VGA
static void pci_virtio_vga(void)
{
    qtest_start("-vga none -device virtio-vga");
    qtest_end();
}
#endif

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/display/pci/cirrus", pci_cirrus);
    qtest_add_func("/display/pci/stdvga", pci_stdvga);
    qtest_add_func("/display/pci/secondary", pci_secondary);
    qtest_add_func("/display/pci/multihead", pci_multihead);
    qtest_add_func("/display/pci/virtio-gpu", pci_virtio_gpu);
#ifdef CONFIG_VIRTIO_VGA
    qtest_add_func("/display/pci/virtio-vga", pci_virtio_vga);
#endif
    ret = g_test_run();

    return ret;
}
