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

void setup_xen_backend_ops(void);

#endif /* QEMU_XEN_BACKEND_OPS_H */
