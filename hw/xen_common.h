#ifndef QEMU_HW_XEN_COMMON_H
#define QEMU_HW_XEN_COMMON_H 1

#include "config-host.h"

#include <stddef.h>
#include <inttypes.h>

#include <xenctrl.h>
#include <xs.h>
#include <xen/io/xenbus.h>

#include "hw.h"
#include "xen.h"
#include "qemu-queue.h"

/*
 * We don't support Xen prior to 3.3.0.
 */

/* Xen before 4.0 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 400
static inline void *xc_map_foreign_bulk(int xc_handle, uint32_t dom, int prot,
                                        xen_pfn_t *arr, int *err,
                                        unsigned int num)
{
    return xc_map_foreign_batch(xc_handle, dom, prot, arr, num);
}
#endif


/* Xen before 4.1 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 410

typedef int XenXC;
typedef int XenEvtchn;
typedef int XenGnttab;

#  define XC_INTERFACE_FMT "%i"
#  define XC_HANDLER_INITIAL_VALUE    -1

static inline XenEvtchn xen_xc_evtchn_open(void *logger,
                                           unsigned int open_flags)
{
    return xc_evtchn_open();
}

static inline XenGnttab xen_xc_gnttab_open(void *logger,
                                           unsigned int open_flags)
{
    return xc_gnttab_open();
}

static inline XenXC xen_xc_interface_open(void *logger, void *dombuild_logger,
                                          unsigned int open_flags)
{
    return xc_interface_open();
}

static inline int xc_fd(int xen_xc)
{
    return xen_xc;
}


static inline int xc_domain_populate_physmap_exact
    (XenXC xc_handle, uint32_t domid, unsigned long nr_extents,
     unsigned int extent_order, unsigned int mem_flags, xen_pfn_t *extent_start)
{
    return xc_domain_memory_populate_physmap
        (xc_handle, domid, nr_extents, extent_order, mem_flags, extent_start);
}

static inline int xc_domain_add_to_physmap(int xc_handle, uint32_t domid,
                                           unsigned int space, unsigned long idx,
                                           xen_pfn_t gpfn)
{
    struct xen_add_to_physmap xatp = {
        .domid = domid,
        .space = space,
        .idx = idx,
        .gpfn = gpfn,
    };

    return xc_memory_op(xc_handle, XENMEM_add_to_physmap, &xatp);
}


/* Xen 4.1 */
#else

typedef xc_interface *XenXC;
typedef xc_evtchn *XenEvtchn;
typedef xc_gnttab *XenGnttab;

#  define XC_INTERFACE_FMT "%p"
#  define XC_HANDLER_INITIAL_VALUE    NULL

static inline XenEvtchn xen_xc_evtchn_open(void *logger,
                                           unsigned int open_flags)
{
    return xc_evtchn_open(logger, open_flags);
}

static inline XenGnttab xen_xc_gnttab_open(void *logger,
                                           unsigned int open_flags)
{
    return xc_gnttab_open(logger, open_flags);
}

static inline XenXC xen_xc_interface_open(void *logger, void *dombuild_logger,
                                          unsigned int open_flags)
{
    return xc_interface_open(logger, dombuild_logger, open_flags);
}

/* FIXME There is now way to have the xen fd */
static inline int xc_fd(xc_interface *xen_xc)
{
    return -1;
}
#endif

void destroy_hvm_domain(void);

#endif /* QEMU_HW_XEN_COMMON_H */
