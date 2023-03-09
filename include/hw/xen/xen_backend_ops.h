/*
 * QEMU Xen backend support
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_XEN_BACKEND_OPS_H
#define QEMU_XEN_BACKEND_OPS_H

#include "hw/xen/xen.h"
#include "hw/xen/interface/xen.h"
#include "hw/xen/interface/io/xenbus.h"

/*
 * For the time being, these operations map fairly closely to the API of
 * the actual Xen libraries, e.g. libxenevtchn. As we complete the migration
 * from XenLegacyDevice back ends to the new XenDevice model, they may
 * evolve to slightly higher-level APIs.
 *
 * The internal emulations do not emulate the Xen APIs entirely faithfully;
 * only enough to be used by the Xen backend devices. For example, only one
 * event channel can be bound to each handle, since that's sufficient for
 * the device support (only the true Xen HVM backend uses more). And the
 * behaviour of unmask() and pending() is different too because the device
 * backends don't care.
 */

typedef struct xenevtchn_handle xenevtchn_handle;
typedef int xenevtchn_port_or_error_t;
typedef uint32_t evtchn_port_t;
typedef uint16_t domid_t;
typedef uint32_t grant_ref_t;

#define XEN_PAGE_SHIFT       12
#define XEN_PAGE_SIZE        (1UL << XEN_PAGE_SHIFT)
#define XEN_PAGE_MASK        (~(XEN_PAGE_SIZE - 1))

#ifndef xen_rmb
#define xen_rmb() smp_rmb()
#endif
#ifndef xen_wmb
#define xen_wmb() smp_wmb()
#endif
#ifndef xen_mb
#define xen_mb() smp_mb()
#endif

struct evtchn_backend_ops {
    xenevtchn_handle *(*open)(void);
    int (*bind_interdomain)(xenevtchn_handle *xc, uint32_t domid,
                            evtchn_port_t guest_port);
    int (*unbind)(xenevtchn_handle *xc, evtchn_port_t port);
    int (*close)(struct xenevtchn_handle *xc);
    int (*get_fd)(struct xenevtchn_handle *xc);
    int (*notify)(struct xenevtchn_handle *xc, evtchn_port_t port);
    int (*unmask)(struct xenevtchn_handle *xc, evtchn_port_t port);
    int (*pending)(struct xenevtchn_handle *xc);
};

extern struct evtchn_backend_ops *xen_evtchn_ops;

static inline xenevtchn_handle *qemu_xen_evtchn_open(void)
{
    if (!xen_evtchn_ops) {
        return NULL;
    }
    return xen_evtchn_ops->open();
}

static inline int qemu_xen_evtchn_bind_interdomain(xenevtchn_handle *xc,
                                                   uint32_t domid,
                                                   evtchn_port_t guest_port)
{
    if (!xen_evtchn_ops) {
        return -ENOSYS;
    }
    return xen_evtchn_ops->bind_interdomain(xc, domid, guest_port);
}

static inline int qemu_xen_evtchn_unbind(xenevtchn_handle *xc,
                                         evtchn_port_t port)
{
    if (!xen_evtchn_ops) {
        return -ENOSYS;
    }
    return xen_evtchn_ops->unbind(xc, port);
}

static inline int qemu_xen_evtchn_close(xenevtchn_handle *xc)
{
    if (!xen_evtchn_ops) {
        return -ENOSYS;
    }
    return xen_evtchn_ops->close(xc);
}

static inline int qemu_xen_evtchn_fd(xenevtchn_handle *xc)
{
    if (!xen_evtchn_ops) {
        return -ENOSYS;
    }
    return xen_evtchn_ops->get_fd(xc);
}

static inline int qemu_xen_evtchn_notify(xenevtchn_handle *xc,
                                         evtchn_port_t port)
{
    if (!xen_evtchn_ops) {
        return -ENOSYS;
    }
    return xen_evtchn_ops->notify(xc, port);
}

static inline int qemu_xen_evtchn_unmask(xenevtchn_handle *xc,
                                         evtchn_port_t port)
{
    if (!xen_evtchn_ops) {
        return -ENOSYS;
    }
    return xen_evtchn_ops->unmask(xc, port);
}

static inline int qemu_xen_evtchn_pending(xenevtchn_handle *xc)
{
    if (!xen_evtchn_ops) {
        return -ENOSYS;
    }
    return xen_evtchn_ops->pending(xc);
}

typedef struct xengntdev_handle xengnttab_handle;

typedef struct XenGrantCopySegment {
    union {
        void *virt;
        struct {
            uint32_t ref;
            off_t offset;
        } foreign;
    } source, dest;
    size_t len;
} XenGrantCopySegment;

#define XEN_GNTTAB_OP_FEATURE_MAP_MULTIPLE  (1U << 0)

struct gnttab_backend_ops {
    uint32_t features;
    xengnttab_handle *(*open)(void);
    int (*close)(xengnttab_handle *xgt);
    int (*grant_copy)(xengnttab_handle *xgt, bool to_domain, uint32_t domid,
                      XenGrantCopySegment *segs, uint32_t nr_segs,
                      Error **errp);
    int (*set_max_grants)(xengnttab_handle *xgt, uint32_t nr_grants);
    void *(*map_refs)(xengnttab_handle *xgt, uint32_t count, uint32_t domid,
                      uint32_t *refs, int prot);
    int (*unmap)(xengnttab_handle *xgt, void *start_address, uint32_t *refs,
                 uint32_t count);
};

extern struct gnttab_backend_ops *xen_gnttab_ops;

static inline bool qemu_xen_gnttab_can_map_multi(void)
{
    return xen_gnttab_ops &&
        !!(xen_gnttab_ops->features & XEN_GNTTAB_OP_FEATURE_MAP_MULTIPLE);
}

static inline xengnttab_handle *qemu_xen_gnttab_open(void)
{
    if (!xen_gnttab_ops) {
        return NULL;
    }
    return xen_gnttab_ops->open();
}

static inline int qemu_xen_gnttab_close(xengnttab_handle *xgt)
{
    if (!xen_gnttab_ops) {
        return -ENOSYS;
    }
    return xen_gnttab_ops->close(xgt);
}

static inline int qemu_xen_gnttab_grant_copy(xengnttab_handle *xgt,
                                             bool to_domain, uint32_t domid,
                                             XenGrantCopySegment *segs,
                                             uint32_t nr_segs, Error **errp)
{
    if (!xen_gnttab_ops) {
        return -ENOSYS;
    }

    return xen_gnttab_ops->grant_copy(xgt, to_domain, domid, segs, nr_segs,
                                      errp);
}

static inline int qemu_xen_gnttab_set_max_grants(xengnttab_handle *xgt,
                                                 uint32_t nr_grants)
{
    if (!xen_gnttab_ops) {
        return -ENOSYS;
    }
    return xen_gnttab_ops->set_max_grants(xgt, nr_grants);
}

static inline void *qemu_xen_gnttab_map_refs(xengnttab_handle *xgt,
                                             uint32_t count, uint32_t domid,
                                             uint32_t *refs, int prot)
{
    if (!xen_gnttab_ops) {
        return NULL;
    }
    return xen_gnttab_ops->map_refs(xgt, count, domid, refs, prot);
}

static inline int qemu_xen_gnttab_unmap(xengnttab_handle *xgt,
                                        void *start_address, uint32_t *refs,
                                        uint32_t count)
{
    if (!xen_gnttab_ops) {
        return -ENOSYS;
    }
    return xen_gnttab_ops->unmap(xgt, start_address, refs, count);
}

struct foreignmem_backend_ops {
    void *(*map)(uint32_t dom, void *addr, int prot, size_t pages,
                 xen_pfn_t *pfns, int *errs);
    int (*unmap)(void *addr, size_t pages);
};

extern struct foreignmem_backend_ops *xen_foreignmem_ops;

static inline void *qemu_xen_foreignmem_map(uint32_t dom, void *addr, int prot,
                                            size_t pages, xen_pfn_t *pfns,
                                            int *errs)
{
    if (!xen_foreignmem_ops) {
        return NULL;
    }
    return xen_foreignmem_ops->map(dom, addr, prot, pages, pfns, errs);
}

static inline int qemu_xen_foreignmem_unmap(void *addr, size_t pages)
{
    if (!xen_foreignmem_ops) {
        return -ENOSYS;
    }
    return xen_foreignmem_ops->unmap(addr, pages);
}

typedef void (*xs_watch_fn)(void *opaque, const char *path);

struct qemu_xs_handle;
struct qemu_xs_watch;
typedef uint32_t xs_transaction_t;

#define XBT_NULL 0

#define XS_PERM_NONE  0x00
#define XS_PERM_READ  0x01
#define XS_PERM_WRITE 0x02

struct xenstore_backend_ops {
    struct qemu_xs_handle *(*open)(void);
    void (*close)(struct qemu_xs_handle *h);
    char *(*get_domain_path)(struct qemu_xs_handle *h, unsigned int domid);
    char **(*directory)(struct qemu_xs_handle *h, xs_transaction_t t,
                        const char *path, unsigned int *num);
    void *(*read)(struct qemu_xs_handle *h, xs_transaction_t t,
                  const char *path, unsigned int *len);
    bool (*write)(struct qemu_xs_handle *h, xs_transaction_t t,
                  const char *path, const void *data, unsigned int len);
    bool (*create)(struct qemu_xs_handle *h, xs_transaction_t t,
                   unsigned int owner, unsigned int domid,
                   unsigned int perms, const char *path);
    bool (*destroy)(struct qemu_xs_handle *h, xs_transaction_t t,
               const char *path);
    struct qemu_xs_watch *(*watch)(struct qemu_xs_handle *h, const char *path,
                                   xs_watch_fn fn, void *opaque);
    void (*unwatch)(struct qemu_xs_handle *h, struct qemu_xs_watch *w);
    xs_transaction_t (*transaction_start)(struct qemu_xs_handle *h);
    bool (*transaction_end)(struct qemu_xs_handle *h, xs_transaction_t t,
                            bool abort);
};

extern struct xenstore_backend_ops *xen_xenstore_ops;

static inline struct qemu_xs_handle *qemu_xen_xs_open(void)
{
    if (!xen_xenstore_ops) {
        return NULL;
    }
    return xen_xenstore_ops->open();
}

static inline void qemu_xen_xs_close(struct qemu_xs_handle *h)
{
    if (!xen_xenstore_ops) {
        return;
    }
    xen_xenstore_ops->close(h);
}

static inline char *qemu_xen_xs_get_domain_path(struct qemu_xs_handle *h,
                                                unsigned int domid)
{
    if (!xen_xenstore_ops) {
        return NULL;
    }
    return xen_xenstore_ops->get_domain_path(h, domid);
}

static inline char **qemu_xen_xs_directory(struct qemu_xs_handle *h,
                                           xs_transaction_t t, const char *path,
                                           unsigned int *num)
{
    if (!xen_xenstore_ops) {
        return NULL;
    }
    return xen_xenstore_ops->directory(h, t, path, num);
}

static inline void *qemu_xen_xs_read(struct qemu_xs_handle *h,
                                     xs_transaction_t t, const char *path,
                                     unsigned int *len)
{
    if (!xen_xenstore_ops) {
        return NULL;
    }
    return xen_xenstore_ops->read(h, t, path, len);
}

static inline bool qemu_xen_xs_write(struct qemu_xs_handle *h,
                                     xs_transaction_t t, const char *path,
                                     const void *data, unsigned int len)
{
    if (!xen_xenstore_ops) {
        return false;
    }
    return xen_xenstore_ops->write(h, t, path, data, len);
}

static inline bool qemu_xen_xs_create(struct qemu_xs_handle *h,
                                      xs_transaction_t t, unsigned int owner,
                                      unsigned int domid, unsigned int perms,
                                      const char *path)
{
    if (!xen_xenstore_ops) {
        return false;
    }
    return xen_xenstore_ops->create(h, t, owner, domid, perms, path);
}

static inline bool qemu_xen_xs_destroy(struct qemu_xs_handle *h,
                                       xs_transaction_t t, const char *path)
{
    if (!xen_xenstore_ops) {
        return false;
    }
    return xen_xenstore_ops->destroy(h, t, path);
}

static inline struct qemu_xs_watch *qemu_xen_xs_watch(struct qemu_xs_handle *h,
                                                      const char *path,
                                                      xs_watch_fn fn,
                                                      void *opaque)
{
    if (!xen_xenstore_ops) {
        return NULL;
    }
    return xen_xenstore_ops->watch(h, path, fn, opaque);
}

static inline void qemu_xen_xs_unwatch(struct qemu_xs_handle *h,
                                       struct qemu_xs_watch *w)
{
    if (!xen_xenstore_ops) {
        return;
    }
    xen_xenstore_ops->unwatch(h, w);
}

static inline xs_transaction_t qemu_xen_xs_transaction_start(struct qemu_xs_handle *h)
{
    if (!xen_xenstore_ops) {
        return XBT_NULL;
    }
    return xen_xenstore_ops->transaction_start(h);
}

static inline bool qemu_xen_xs_transaction_end(struct qemu_xs_handle *h,
                                               xs_transaction_t t, bool abort)
{
    if (!xen_xenstore_ops) {
        return false;
    }
    return xen_xenstore_ops->transaction_end(h, t, abort);
}

void setup_xen_backend_ops(void);

#endif /* QEMU_XEN_BACKEND_OPS_H */
