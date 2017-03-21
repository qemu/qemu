/*
 * Xen 9p backend
 *
 * Copyright Aporeto 2017
 *
 * Authors:
 *  Stefano Stabellini <stefano@aporeto.com>
 *
 */

#include "qemu/osdep.h"

#include "hw/hw.h"
#include "hw/9pfs/9p.h"
#include "hw/xen/xen_backend.h"
#include "hw/9pfs/xen-9pfs.h"
#include "qemu/config-file.h"
#include "fsdev/qemu-fsdev.h"

typedef struct Xen9pfsDev {
    struct XenDevice xendev;  /* must be first */
} Xen9pfsDev;

static ssize_t xen_9pfs_pdu_vmarshal(V9fsPDU *pdu,
                                     size_t offset,
                                     const char *fmt,
                                     va_list ap)
{
    return 0;
}

static ssize_t xen_9pfs_pdu_vunmarshal(V9fsPDU *pdu,
                                       size_t offset,
                                       const char *fmt,
                                       va_list ap)
{
    return 0;
}

static void xen_9pfs_init_out_iov_from_pdu(V9fsPDU *pdu,
                                           struct iovec **piov,
                                           unsigned int *pniov)
{
}

static void xen_9pfs_init_in_iov_from_pdu(V9fsPDU *pdu,
                                          struct iovec **piov,
                                          unsigned int *pniov,
                                          size_t size)
{
}

static void xen_9pfs_push_and_notify(V9fsPDU *pdu)
{
}

static const struct V9fsTransport xen_9p_transport = {
    .pdu_vmarshal = xen_9pfs_pdu_vmarshal,
    .pdu_vunmarshal = xen_9pfs_pdu_vunmarshal,
    .init_in_iov_from_pdu = xen_9pfs_init_in_iov_from_pdu,
    .init_out_iov_from_pdu = xen_9pfs_init_out_iov_from_pdu,
    .push_and_notify = xen_9pfs_push_and_notify,
};

static int xen_9pfs_init(struct XenDevice *xendev)
{
    return 0;
}

static int xen_9pfs_free(struct XenDevice *xendev)
{
    return -1;
}

static int xen_9pfs_connect(struct XenDevice *xendev)
{
    return 0;
}

static void xen_9pfs_alloc(struct XenDevice *xendev)
{
}

static void xen_9pfs_disconnect(struct XenDevice *xendev)
{
}

struct XenDevOps xen_9pfs_ops = {
    .size       = sizeof(Xen9pfsDev),
    .flags      = DEVOPS_FLAG_NEED_GNTDEV,
    .alloc      = xen_9pfs_alloc,
    .init       = xen_9pfs_init,
    .initialise = xen_9pfs_connect,
    .disconnect = xen_9pfs_disconnect,
    .free       = xen_9pfs_free,
};
