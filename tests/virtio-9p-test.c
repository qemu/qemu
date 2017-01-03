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

typedef struct {
    QVirtioDevice *dev;
    QOSState *qs;
    QVirtQueue *vq;
    char *test_share;
} QVirtIO9P;

static QVirtIO9P *qvirtio_9p_start(const char *driver)
{
    const char *arch = qtest_get_arch();
    const char *cmd = "-fsdev local,id=fsdev0,security_model=none,path=%s "
                      "-device %s,fsdev=fsdev0,mount_tag=%s";
    QVirtIO9P *v9p = g_new0(QVirtIO9P, 1);

    v9p->test_share = g_strdup("/tmp/qtest.XXXXXX");
    g_assert_nonnull(mkdtemp(v9p->test_share));

    if (strcmp(arch, "i386") == 0 || strcmp(arch, "x86_64") == 0) {
        v9p->qs = qtest_pc_boot(cmd, v9p->test_share, driver, mount_tag);
    } else if (strcmp(arch, "ppc64") == 0) {
        v9p->qs = qtest_spapr_boot(cmd, v9p->test_share, driver, mount_tag);
    } else {
        g_printerr("virtio-9p tests are only available on x86 or ppc64\n");
        exit(EXIT_FAILURE);
    }

    return v9p;
}

static void qvirtio_9p_stop(QVirtIO9P *v9p)
{
    qtest_shutdown(v9p->qs);
    rmdir(v9p->test_share);
    g_free(v9p->test_share);
    g_free(v9p);
}

static QVirtIO9P *qvirtio_9p_pci_start(void)
{
    QVirtIO9P *v9p = qvirtio_9p_start("virtio-9p-pci");
    QVirtioPCIDevice *dev = qvirtio_pci_device_find(v9p->qs->pcibus,
                                                    VIRTIO_ID_9P);
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

static void qvirtio_9p_pci_stop(QVirtIO9P *v9p)
{
    qvirtqueue_cleanup(v9p->dev->bus, v9p->vq, v9p->qs->alloc);
    qvirtio_pci_device_disable(container_of(v9p->dev, QVirtioPCIDevice, vdev));
    g_free(v9p->dev);
    qvirtio_9p_stop(v9p);
}

static void pci_config(QVirtIO9P *v9p)
{
    size_t tag_len = qvirtio_config_readw(v9p->dev, 0);
    char *tag;
    int i;

    g_assert_cmpint(tag_len, ==, strlen(mount_tag));

    tag = g_malloc(tag_len);
    for (i = 0; i < tag_len; i++) {
        tag[i] = qvirtio_config_readb(v9p->dev, i + 2);
    }
    g_assert_cmpmem(tag, tag_len, mount_tag, tag_len);
    g_free(tag);
}

typedef void (*v9fs_test_fn)(QVirtIO9P *v9p);

static void v9fs_run_pci_test(gconstpointer data)
{
    v9fs_test_fn fn = data;
    QVirtIO9P *v9p = qvirtio_9p_pci_start();

    if (fn) {
        fn(v9p);
    }
    qvirtio_9p_pci_stop(v9p);
}

static void v9fs_qtest_pci_add(const char *path, v9fs_test_fn fn)
{
    qtest_add_data_func(path, fn, v9fs_run_pci_test);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    v9fs_qtest_pci_add("/virtio/9p/pci/nop", NULL);
    v9fs_qtest_pci_add("/virtio/9p/pci/config", pci_config);

    return g_test_run();
}
