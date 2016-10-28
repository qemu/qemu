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
#include "libqos/libqos-pc.h"
#include "libqos/libqos-spapr.h"
#include "libqos/virtio.h"
#include "libqos/virtio-pci.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_pci.h"

static const char mount_tag[] = "qtest";
static char *test_share;


static QOSState *qvirtio_9p_start(void)
{
    const char *arch = qtest_get_arch();
    const char *cmd = "-fsdev local,id=fsdev0,security_model=none,path=%s "
                      "-device virtio-9p-pci,fsdev=fsdev0,mount_tag=%s";

    test_share = g_strdup("/tmp/qtest.XXXXXX");
    g_assert_nonnull(mkdtemp(test_share));

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        return qtest_pc_boot(cmd, test_share, mount_tag);
    }
    if (strcmp(arch, "ppc64") == 0) {
        return qtest_spapr_boot(cmd, test_share, mount_tag);
    }

    g_printerr("virtio-9p tests are only available on x86 or ppc64\n");
    exit(EXIT_FAILURE);
}

static void qvirtio_9p_stop(QOSState *qs)
{
    qtest_shutdown(qs);
    rmdir(test_share);
    g_free(test_share);
}

static void pci_nop(void)
{
    QOSState *qs;

    qs = qvirtio_9p_start();
    qvirtio_9p_stop(qs);
}

typedef struct {
    QVirtioDevice *dev;
    QOSState *qs;
    QVirtQueue *vq;
} QVirtIO9P;

static QVirtIO9P *qvirtio_9p_pci_init(QOSState *qs)
{
    QVirtIO9P *v9p;
    QVirtioPCIDevice *dev;

    v9p = g_new0(QVirtIO9P, 1);

    v9p->qs = qs;
    dev = qvirtio_pci_device_find(v9p->qs->pcibus, VIRTIO_ID_9P);
    g_assert_nonnull(dev);
    g_assert_cmphex(dev->vdev.device_type, ==, VIRTIO_ID_9P);
    v9p->dev = (QVirtioDevice *) dev;

    qvirtio_pci_device_enable(dev);
    qvirtio_reset(v9p->dev);
    qvirtio_set_acknowledge(v9p->dev);
    qvirtio_set_driver(v9p->dev);

    v9p->vq = qvirtqueue_setup(v9p->dev, v9p->qs->alloc, 0);
    return v9p;
}

static void qvirtio_9p_pci_free(QVirtIO9P *v9p)
{
    qvirtqueue_cleanup(v9p->dev->bus, v9p->vq, v9p->qs->alloc);
    qvirtio_pci_device_disable(container_of(v9p->dev, QVirtioPCIDevice, vdev));
    g_free(v9p->dev);
    g_free(v9p);
}

static void pci_basic_config(void)
{
    QVirtIO9P *v9p;
    size_t tag_len;
    char *tag;
    int i;
    QOSState *qs;

    qs = qvirtio_9p_start();
    v9p = qvirtio_9p_pci_init(qs);

    tag_len = qvirtio_config_readw(v9p->dev, 0);
    g_assert_cmpint(tag_len, ==, strlen(mount_tag));

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(v9p->dev, i + 2);
    }
    g_assert_cmpmem(tag, tag_len, mount_tag, tag_len);
    g_free(tag);

    qvirtio_9p_pci_free(v9p);
    qvirtio_9p_stop(qs);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/virtio/9p/pci/nop", pci_nop);
    qtest_add_func("/virtio/9p/pci/basic/configuration", pci_basic_config);

    return g_test_run();
}
