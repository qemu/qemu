/*
 * QEMU paravirtual RDMA - Generic RDMA backend
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qapi/qmp/qlist.h"
#include "qapi/qmp/qnum.h"
#include "trace.h"
#include "rdma_utils.h"

void *rdma_pci_dma_map(PCIDevice *dev, dma_addr_t addr, dma_addr_t plen)
{
    void *p;
    hwaddr len = plen;

    if (!addr) {
        rdma_error_report("addr is NULL");
        return NULL;
    }

    p = pci_dma_map(dev, addr, &len, DMA_DIRECTION_TO_DEVICE);
    if (!p) {
        rdma_error_report("pci_dma_map fail, addr=0x%"PRIx64", len=%"PRId64,
                          addr, len);
        return NULL;
    }

    if (len != plen) {
        rdma_pci_dma_unmap(dev, p, len);
        return NULL;
    }

    trace_rdma_pci_dma_map(addr, p, len);

    return p;
}

void rdma_pci_dma_unmap(PCIDevice *dev, void *buffer, dma_addr_t len)
{
    trace_rdma_pci_dma_unmap(buffer);
    if (buffer) {
        pci_dma_unmap(dev, buffer, len, DMA_DIRECTION_TO_DEVICE, 0);
    }
}

void rdma_protected_qlist_init(RdmaProtectedQList *list)
{
    qemu_mutex_init(&list->lock);
    list->list = qlist_new();
}

void rdma_protected_qlist_destroy(RdmaProtectedQList *list)
{
    if (list->list) {
        qlist_destroy_obj(QOBJECT(list->list));
        qemu_mutex_destroy(&list->lock);
        list->list = NULL;
    }
}

void rdma_protected_qlist_append_int64(RdmaProtectedQList *list, int64_t value)
{
    qemu_mutex_lock(&list->lock);
    qlist_append_int(list->list, value);
    qemu_mutex_unlock(&list->lock);
}

int64_t rdma_protected_qlist_pop_int64(RdmaProtectedQList *list)
{
    QObject *obj;

    qemu_mutex_lock(&list->lock);
    obj = qlist_pop(list->list);
    qemu_mutex_unlock(&list->lock);

    if (!obj) {
        return -ENOENT;
    }

    return qnum_get_uint(qobject_to(QNum, obj));
}

void rdma_protected_gslist_init(RdmaProtectedGSList *list)
{
    qemu_mutex_init(&list->lock);
}

void rdma_protected_gslist_destroy(RdmaProtectedGSList *list)
{
    if (list->list) {
        g_slist_free(list->list);
        qemu_mutex_destroy(&list->lock);
        list->list = NULL;
    }
}

void rdma_protected_gslist_append_int32(RdmaProtectedGSList *list,
                                        int32_t value)
{
    qemu_mutex_lock(&list->lock);
    list->list = g_slist_prepend(list->list, GINT_TO_POINTER(value));
    qemu_mutex_unlock(&list->lock);
}

void rdma_protected_gslist_remove_int32(RdmaProtectedGSList *list,
                                        int32_t value)
{
    qemu_mutex_lock(&list->lock);
    list->list = g_slist_remove(list->list, GINT_TO_POINTER(value));
    qemu_mutex_unlock(&list->lock);
}
