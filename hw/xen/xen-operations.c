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
#undef XC_WANT_COMPAT_GNTTAB_API

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

typedef xc_gnttab xengnttab_handle;

#define xengnttab_open(l, f) xc_gnttab_open(l, f)
#define xengnttab_close(h) xc_gnttab_close(h)
#define xengnttab_set_max_grants(h, n) xc_gnttab_set_max_grants(h, n)
#define xengnttab_map_grant_ref(h, d, r, p) xc_gnttab_map_grant_ref(h, d, r, p)
#define xengnttab_unmap(h, a, n) xc_gnttab_munmap(h, a, n)
#define xengnttab_map_grant_refs(h, c, d, r, p) \
    xc_gnttab_map_grant_refs(h, c, d, r, p)
#define xengnttab_map_domain_grant_refs(h, c, d, r, p) \
    xc_gnttab_map_domain_grant_refs(h, c, d, r, p)

#else /* CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40701 */

#include <xenevtchn.h>
#include <xengnttab.h>

#endif

/* Xen before 4.8 */

static int libxengnttab_fallback_grant_copy(xengnttab_handle *xgt,
                                            bool to_domain, uint32_t domid,
                                            XenGrantCopySegment segs[],
                                            unsigned int nr_segs, Error **errp)
{
    uint32_t *refs = g_new(uint32_t, nr_segs);
    int prot = to_domain ? PROT_WRITE : PROT_READ;
    void *map;
    unsigned int i;
    int rc = 0;

    for (i = 0; i < nr_segs; i++) {
        XenGrantCopySegment *seg = &segs[i];

        refs[i] = to_domain ? seg->dest.foreign.ref :
            seg->source.foreign.ref;
    }
    map = xengnttab_map_domain_grant_refs(xgt, nr_segs, domid, refs, prot);
    if (!map) {
        if (errp) {
            error_setg_errno(errp, errno,
                             "xengnttab_map_domain_grant_refs failed");
        }
        rc = -errno;
        goto done;
    }

    for (i = 0; i < nr_segs; i++) {
        XenGrantCopySegment *seg = &segs[i];
        void *page = map + (i * XEN_PAGE_SIZE);

        if (to_domain) {
            memcpy(page + seg->dest.foreign.offset, seg->source.virt,
                   seg->len);
        } else {
            memcpy(seg->dest.virt, page + seg->source.foreign.offset,
                   seg->len);
        }
    }

    if (xengnttab_unmap(xgt, map, nr_segs)) {
        if (errp) {
            error_setg_errno(errp, errno, "xengnttab_unmap failed");
        }
        rc = -errno;
    }

done:
    g_free(refs);
    return rc;
}

#if CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40800

static int libxengnttab_backend_grant_copy(xengnttab_handle *xgt,
                                           bool to_domain, uint32_t domid,
                                           XenGrantCopySegment *segs,
                                           uint32_t nr_segs, Error **errp)
{
    xengnttab_grant_copy_segment_t *xengnttab_segs;
    unsigned int i;
    int rc;

    xengnttab_segs = g_new0(xengnttab_grant_copy_segment_t, nr_segs);

    for (i = 0; i < nr_segs; i++) {
        XenGrantCopySegment *seg = &segs[i];
        xengnttab_grant_copy_segment_t *xengnttab_seg = &xengnttab_segs[i];

        if (to_domain) {
            xengnttab_seg->flags = GNTCOPY_dest_gref;
            xengnttab_seg->dest.foreign.domid = domid;
            xengnttab_seg->dest.foreign.ref = seg->dest.foreign.ref;
            xengnttab_seg->dest.foreign.offset = seg->dest.foreign.offset;
            xengnttab_seg->source.virt = seg->source.virt;
        } else {
            xengnttab_seg->flags = GNTCOPY_source_gref;
            xengnttab_seg->source.foreign.domid = domid;
            xengnttab_seg->source.foreign.ref = seg->source.foreign.ref;
            xengnttab_seg->source.foreign.offset =
                seg->source.foreign.offset;
            xengnttab_seg->dest.virt = seg->dest.virt;
        }

        xengnttab_seg->len = seg->len;
    }

    if (xengnttab_grant_copy(xgt, nr_segs, xengnttab_segs)) {
        if (errp) {
            error_setg_errno(errp, errno, "xengnttab_grant_copy failed");
        }
        rc = -errno;
        goto done;
    }

    rc = 0;
    for (i = 0; i < nr_segs; i++) {
        xengnttab_grant_copy_segment_t *xengnttab_seg = &xengnttab_segs[i];

        if (xengnttab_seg->status != GNTST_okay) {
            if (errp) {
                error_setg(errp, "xengnttab_grant_copy seg[%u] failed", i);
            }
            rc = -EIO;
            break;
        }
    }

done:
    g_free(xengnttab_segs);
    return rc;
}
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

static xengnttab_handle *libxengnttab_backend_open(void)
{
    return xengnttab_open(NULL, 0);
}

static int libxengnttab_backend_unmap(xengnttab_handle *xgt,
                                      void *start_address, uint32_t *refs,
                                      uint32_t count)
{
    return xengnttab_unmap(xgt, start_address, count);
}


static struct gnttab_backend_ops libxengnttab_backend_ops = {
    .features = XEN_GNTTAB_OP_FEATURE_MAP_MULTIPLE,
    .open = libxengnttab_backend_open,
    .close = xengnttab_close,
    .grant_copy = libxengnttab_fallback_grant_copy,
    .set_max_grants = xengnttab_set_max_grants,
    .map_refs = xengnttab_map_domain_grant_refs,
    .unmap = libxengnttab_backend_unmap,
};

void setup_xen_backend_ops(void)
{
#if CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40800
    xengnttab_handle *xgt = xengnttab_open(NULL, 0);

    if (xgt) {
        if (xengnttab_grant_copy(xgt, 0, NULL) == 0) {
            libxengnttab_backend_ops.grant_copy = libxengnttab_backend_grant_copy;
        }
        xengnttab_close(xgt);
    }
#endif
    xen_evtchn_ops = &libxenevtchn_backend_ops;
    xen_gnttab_ops = &libxengnttab_backend_ops;
}
