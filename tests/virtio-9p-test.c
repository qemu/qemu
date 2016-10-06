/*
 * QTest testcase for VirtIO 9P
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu-common.h"
#include "libqos/pci-pc.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "libqos/malloc.h"
#include "libqos/malloc-pc.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_pci.h"

static const char mount_tag[] = "qtest";
static char *test_share;

static void qvirtio_9p_start(void)
{
    char *args;

    test_share = g_strdup("/tmp/qtest.XXXXXX");
    g_assert_nonnull(mkdtemp(test_share));

    args = g_strdup_printf("-fsdev local,id=fsdev0,security_model=none,path=%s "
                           "-device virtio-9p-pci,fsdev=fsdev0,mount_tag=%s",
                           test_share, mount_tag);

    qtest_start(args);
    g_free(args);
}

static void qvirtio_9p_stop(void)
{
    qtest_end();
    rmdir(test_share);
    g_free(test_share);
}

static void pci_nop(void)
{
    qvirtio_9p_start();
    qvirtio_9p_stop();
}

typedef struct {
    QVirtioDevice *dev;
    QGuestAllocator *alloc;
    QPCIBus *bus;
    QVirtQueue *vq;
} QVirtIO9P;

static QVirtIO9P *qvirtio_9p_pci_init(void)
{
    QVirtIO9P *v9p;
    QVirtioPCIDevice *dev;

    v9p = g_new0(QVirtIO9P, 1);
    v9p->alloc = pc_alloc_init();
    v9p->bus = qpci_init_pc(NULL);

    dev = qvirtio_pci_device_find(v9p->bus, VIRTIO_ID_9P);
    g_assert_nonnull(dev);
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_9P);
    v9p->dev = (QVirtioDevice *) dev;

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(&qvirtio_pci, v9p->dev);
    qvirtio_set_acknowledge(&qvirtio_pci, v9p->dev);
    qvirtio_set_driver(&qvirtio_pci, v9p->dev);

    v9p->vq = qvirtqueue_setup(&qvirtio_pci, v9p->dev, v9p->alloc, 0);
    return v9p;
}

static void qvirtio_9p_pci_free(QVirtIO9P *v9p)
{
    qvirtqueue_cleanup(&qvirtio_pci, v9p->vq, v9p->alloc);
    pc_alloc_uninit(v9p->alloc);
    qvirtio_pci_device_disable(container_of(v9p->dev, QVirtioPCIDevice, vdev));
    g_free(v9p->dev);
    qpci_free_pc(v9p->bus);
    g_free(v9p);
}

static void pci_basic_config(void)
{
    QVirtIO9P *v9p;
    void *addr;
    size_t tag_len;
    char *tag;
    int i;

    qvirtio_9p_start();
    v9p = qvirtio_9p_pci_init();

    addr = ((QVirtioPCIDevice *) v9p->dev)->addr + VIRTIO_PCI_CONFIG_OFF(false);
    tag_len = qvirtio_config_readw(&qvirtio_pci, v9p->dev,
                                   (uint64_t)(uintptr_t)addr);
    g_assert_cmpint(tag_len, ==, strlen(mount_tag));
    addr += sizeof(uint16_t);

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(&qvirtio_pci, v9p->dev,
                                      (uint64_t)(uintptr_t)addr + i);
    }
    g_assert_cmpmem(tag, tag_len, mount_tag, tag_len);
    g_free(tag);

    qvirtio_9p_pci_free(v9p);
    qvirtio_9p_stop();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/9p/pci/nop", pci_nop);
    qtest_add_func("/virtio/9p/pci/basic/configuration", pci_basic_config);

    return g_test_run();
}
