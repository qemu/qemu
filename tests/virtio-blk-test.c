/*
 * QTest testcase for VirtIO Block Device
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 * Copyright (c) 2014 Marc Marí
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include "libqtest.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/pci-pc.h"

#define TEST_IMAGE_SIZE (64 * 1024 * 1024)
#define PCI_SLOT    0x04
#define PCI_FN      0x00

static QPCIBus *test_start(void)
{
    char cmdline[100];
    char tmp_path[] = "/tmp/qtest.XXXXXX";
    int fd, ret;

    /* Create a temporary raw image */
    fd = mkstemp(tmp_path);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    snprintf(cmdline, 100, "-drive if=none,id=drive0,file=%s "
                            "-device virtio-blk-pci,drive=drive0,addr=%x.%x",
                            tmp_path, PCI_SLOT, PCI_FN);
    qtest_start(cmdline);
    unlink(tmp_path);

    return qpci_init_pc();
}

static void test_end(void)
{
    qtest_end();
}

static QVirtioPCIDevice *virtio_blk_init(QPCIBus *bus)
{
    QVirtioPCIDevice *dev;

    dev = qvirtio_pci_device_find(bus, QVIRTIO_BLK_DEVICE_ID);
    g_assert(dev != NULL);
    g_assert_cmphex(dev->vdev.device_type, ==, QVIRTIO_BLK_DEVICE_ID);
    g_assert_cmphex(dev->pdev->devfn, ==, ((PCI_SLOT << 3) | PCI_FN));

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&qvirtio_pci, &dev->vdev);
    qvirtio_set_acknowledge(&qvirtio_pci, &dev->vdev);
    qvirtio_set_driver(&qvirtio_pci, &dev->vdev);

    return dev;
}

static void pci_basic(void)
{
    QVirtioPCIDevice *dev;
    QPCIBus *bus;
    void *addr;
    uint64_t capacity;

    bus = test_start();

    dev = virtio_blk_init(bus);

    /* MSI-X is not enabled */
    addr = dev->addr + QVIRTIO_DEVICE_SPECIFIC_NO_MSIX;

    capacity = qvirtio_config_readq(&qvirtio_pci, &dev->vdev, addr);
    g_assert_cmpint(capacity, ==, TEST_IMAGE_SIZE / 512);

    qvirtio_pci_device_disable(dev);
    g_free(dev);
    test_end();
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/virtio/blk/pci/basic", pci_basic);

    ret = g_test_run();

    return ret;
}
