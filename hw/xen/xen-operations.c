/*
 * QEMU Xen backend support: Operations for true Xen
 *
 * Copyright © 2022 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "hw/xen/xen_backend_ops.h"
#include "hw/xen/xen_common.h"

/*
 * If we have new enough libxenctrl then we do not want/need these compat
 * interfaces, despite what the user supplied cflags might say. They
 * must be undefined before including xenctrl.h
 */
#undef XC_WANT_COMPAT_EVTCHN_API

#include <xenctrl.h>

/*
 * We don't support Xen prior to 4.2.0.
 */

/* Xen 4.2 through 4.6 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 40701

typedef xc_evtchn xenevtchn_handle;
typedef evtchn_port_or_error_t xenevtchn_port_or_error_t;

#define xenevtchn_open(l, f) xc_evtchn_open(l, f);
#define xenevtchn_close(h) xc_evtchn_close(h)
#define xenevtchn_fd(h) xc_evtchn_fd(h)
#define xenevtchn_pending(h) xc_evtchn_pending(h)
#define xenevtchn_notify(h, p) xc_evtchn_notify(h, p)
#define xenevtchn_bind_interdomain(h, d, p) xc_evtchn_bind_interdomain(h, d, p)
#define xenevtchn_unmask(h, p) xc_evtchn_unmask(h, p)
#define xenevtchn_unbind(h, p) xc_evtchn_unbind(h, p)

#else /* CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40701 */

#include <xenevtchn.h>

#endif

static xenevtchn_handle *libxenevtchn_backend_open(void)
{
    return xenevtchn_open(NULL, 0);
}

struct evtchn_backend_ops libxenevtchn_backend_ops = {
    .open = libxenevtchn_backend_open,
    .close = xenevtchn_close,
    .bind_interdomain = xenevtchn_bind_interdomain,
    .unbind = xenevtchn_unbind,
    .get_fd = xenevtchn_fd,
    .notify = xenevtchn_notify,
    .unmask = xenevtchn_unmask,
    .pending = xenevtchn_pending,
};

void setup_xen_backend_ops(void)
{
    xen_evtchn_ops = &libxenevtchn_backend_ops;
}
