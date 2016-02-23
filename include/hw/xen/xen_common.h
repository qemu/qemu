#ifndef QEMU_HW_XEN_COMMON_H
#define QEMU_HW_XEN_COMMON_H 1



/*
 * If we have new enough libxenctrl then we do not want/need these compat
 * interfaces, despite what the user supplied cflags might say. They
 * must be undefined before including xenctrl.h
 */
#undef XC_WANT_COMPAT_EVTCHN_API
#undef XC_WANT_COMPAT_GNTTAB_API
#undef XC_WANT_COMPAT_MAP_FOREIGN_API

#include <xenctrl.h>
#include <xenstore.h>
#include <xen/io/xenbus.h>

#include "hw/hw.h"
#include "hw/xen/xen.h"
#include "hw/pci/pci.h"
#include "qemu/queue.h"
#include "trace.h"

/*
 * We don't support Xen prior to 4.2.0.
 */

/* Xen 4.2 thru 4.6 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 471

typedef xc_interface xenforeignmemory_handle;
typedef xc_evtchn xenevtchn_handle;
typedef xc_gnttab xengnttab_handle;

#define xenevtchn_open(l, f) xc_evtchn_open(l, f);
#define xenevtchn_close(h) xc_evtchn_close(h)
#define xenevtchn_fd(h) xc_evtchn_fd(h)
#define xenevtchn_pending(h) xc_evtchn_pending(h)
#define xenevtchn_notify(h, p) xc_evtchn_notify(h, p)
#define xenevtchn_bind_interdomain(h, d, p) xc_evtchn_bind_interdomain(h, d, p)
#define xenevtchn_unmask(h, p) xc_evtchn_unmask(h, p)
#define xenevtchn_unbind(h, p) xc_evtchn_unbind(h, p)

#define xengnttab_open(l, f) xc_gnttab_open(l, f)
#define xengnttab_close(h) xc_gnttab_close(h)
#define xengnttab_set_max_grants(h, n) xc_gnttab_set_max_grants(h, n)
#define xengnttab_map_grant_ref(h, d, r, p) xc_gnttab_map_grant_ref(h, d, r, p)
#define xengnttab_unmap(h, a, n) xc_gnttab_munmap(h, a, n)
#define xengnttab_map_grant_refs(h, c, d, r, p) \
    xc_gnttab_map_grant_refs(h, c, d, r, p)

#define xenforeignmemory_open(l, f) xen_xc

static inline void *xenforeignmemory_map(xc_interface *h, uint32_t dom,
                                         int prot, size_t pages,
                                         const xen_pfn_t arr[/*pages*/],
                                         int err[/*pages*/])
{
    if (err)
        return xc_map_foreign_bulk(h, dom, prot, arr, err, pages);
    else
        return xc_map_foreign_pages(h, dom, prot, arr, pages);
}

#define xenforeignmemory_unmap(h, p, s) munmap(p, s * XC_PAGE_SIZE)

#else /* CONFIG_XEN_CTRL_INTERFACE_VERSION >= 471 */

#include <xenevtchn.h>
#include <xengnttab.h>
#include <xenforeignmemory.h>

#endif

void destroy_hvm_domain(bool reboot);

/* shutdown/destroy current domain because of an error */
void xen_shutdown_fatal_error(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

#ifdef HVM_PARAM_VMPORT_REGS_PFN
static inline int xen_get_vmport_regs_pfn(xc_interface *xc, domid_t dom,
                                          xen_pfn_t *vmport_regs_pfn)
{
    int rc;
    uint64_t value;
    rc = xc_hvm_param_get(xc, dom, HVM_PARAM_VMPORT_REGS_PFN, &value);
    if (rc >= 0) {
        *vmport_regs_pfn = (xen_pfn_t) value;
    }
    return rc;
}
#else
static inline int xen_get_vmport_regs_pfn(xc_interface *xc, domid_t dom,
                                          xen_pfn_t *vmport_regs_pfn)
{
    return -ENOSYS;
}
#endif

/* Xen before 4.6 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 460

#ifndef HVM_IOREQSRV_BUFIOREQ_ATOMIC
#define HVM_IOREQSRV_BUFIOREQ_ATOMIC 2
#endif

#endif

/* Xen before 4.5 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 450

#ifndef HVM_PARAM_BUFIOREQ_EVTCHN
#define HVM_PARAM_BUFIOREQ_EVTCHN 26
#endif

#define IOREQ_TYPE_PCI_CONFIG 2

typedef uint16_t ioservid_t;

static inline void xen_map_memory_section(xc_interface *xc, domid_t dom,
                                          ioservid_t ioservid,
                                          MemoryRegionSection *section)
{
}

static inline void xen_unmap_memory_section(xc_interface *xc, domid_t dom,
                                            ioservid_t ioservid,
                                            MemoryRegionSection *section)
{
}

static inline void xen_map_io_section(xc_interface *xc, domid_t dom,
                                      ioservid_t ioservid,
                                      MemoryRegionSection *section)
{
}

static inline void xen_unmap_io_section(xc_interface *xc, domid_t dom,
                                        ioservid_t ioservid,
                                        MemoryRegionSection *section)
{
}

static inline void xen_map_pcidev(xc_interface *xc, domid_t dom,
                                  ioservid_t ioservid,
                                  PCIDevice *pci_dev)
{
}

static inline void xen_unmap_pcidev(xc_interface *xc, domid_t dom,
                                    ioservid_t ioservid,
                                    PCIDevice *pci_dev)
{
}

static inline int xen_create_ioreq_server(xc_interface *xc, domid_t dom,
                                          ioservid_t *ioservid)
{
    return 0;
}

static inline void xen_destroy_ioreq_server(xc_interface *xc, domid_t dom,
                                            ioservid_t ioservid)
{
}

static inline int xen_get_ioreq_server_info(xc_interface *xc, domid_t dom,
                                            ioservid_t ioservid,
                                            xen_pfn_t *ioreq_pfn,
                                            xen_pfn_t *bufioreq_pfn,
                                            evtchn_port_t *bufioreq_evtchn)
{
    unsigned long param;
    int rc;

    rc = xc_get_hvm_param(xc, dom, HVM_PARAM_IOREQ_PFN, &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_IOREQ_PFN\n");
        return -1;
    }

    *ioreq_pfn = param;

    rc = xc_get_hvm_param(xc, dom, HVM_PARAM_BUFIOREQ_PFN, &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_BUFIOREQ_PFN\n");
        return -1;
    }

    *bufioreq_pfn = param;

    rc = xc_get_hvm_param(xc, dom, HVM_PARAM_BUFIOREQ_EVTCHN,
                          &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_BUFIOREQ_EVTCHN\n");
        return -1;
    }

    *bufioreq_evtchn = param;

    return 0;
}

static inline int xen_set_ioreq_server_state(xc_interface *xc, domid_t dom,
                                             ioservid_t ioservid,
                                             bool enable)
{
    return 0;
}

/* Xen 4.5 */
#else

static inline void xen_map_memory_section(xc_interface *xc, domid_t dom,
                                          ioservid_t ioservid,
                                          MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    trace_xen_map_mmio_range(ioservid, start_addr, end_addr);
    xc_hvm_map_io_range_to_ioreq_server(xc, dom, ioservid, 1,
                                        start_addr, end_addr);
}

static inline void xen_unmap_memory_section(xc_interface *xc, domid_t dom,
                                            ioservid_t ioservid,
                                            MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    trace_xen_unmap_mmio_range(ioservid, start_addr, end_addr);
    xc_hvm_unmap_io_range_from_ioreq_server(xc, dom, ioservid, 1,
                                            start_addr, end_addr);
}

static inline void xen_map_io_section(xc_interface *xc, domid_t dom,
                                      ioservid_t ioservid,
                                      MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    trace_xen_map_portio_range(ioservid, start_addr, end_addr);
    xc_hvm_map_io_range_to_ioreq_server(xc, dom, ioservid, 0,
                                        start_addr, end_addr);
}

static inline void xen_unmap_io_section(xc_interface *xc, domid_t dom,
                                        ioservid_t ioservid,
                                        MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    trace_xen_unmap_portio_range(ioservid, start_addr, end_addr);
    xc_hvm_unmap_io_range_from_ioreq_server(xc, dom, ioservid, 0,
                                            start_addr, end_addr);
}

static inline void xen_map_pcidev(xc_interface *xc, domid_t dom,
                                  ioservid_t ioservid,
                                  PCIDevice *pci_dev)
{
    trace_xen_map_pcidev(ioservid, pci_bus_num(pci_dev->bus),
                         PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
    xc_hvm_map_pcidev_to_ioreq_server(xc, dom, ioservid,
                                      0, pci_bus_num(pci_dev->bus),
                                      PCI_SLOT(pci_dev->devfn),
                                      PCI_FUNC(pci_dev->devfn));
}

static inline void xen_unmap_pcidev(xc_interface *xc, domid_t dom,
                                    ioservid_t ioservid,
                                    PCIDevice *pci_dev)
{
    trace_xen_unmap_pcidev(ioservid, pci_bus_num(pci_dev->bus),
                           PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
    xc_hvm_unmap_pcidev_from_ioreq_server(xc, dom, ioservid,
                                          0, pci_bus_num(pci_dev->bus),
                                          PCI_SLOT(pci_dev->devfn),
                                          PCI_FUNC(pci_dev->devfn));
}

static inline int xen_create_ioreq_server(xc_interface *xc, domid_t dom,
                                          ioservid_t *ioservid)
{
    int rc = xc_hvm_create_ioreq_server(xc, dom, HVM_IOREQSRV_BUFIOREQ_ATOMIC,
                                        ioservid);

    if (rc == 0) {
        trace_xen_ioreq_server_create(*ioservid);
    }

    return rc;
}

static inline void xen_destroy_ioreq_server(xc_interface *xc, domid_t dom,
                                            ioservid_t ioservid)
{
    trace_xen_ioreq_server_destroy(ioservid);
    xc_hvm_destroy_ioreq_server(xc, dom, ioservid);
}

static inline int xen_get_ioreq_server_info(xc_interface *xc, domid_t dom,
                                            ioservid_t ioservid,
                                            xen_pfn_t *ioreq_pfn,
                                            xen_pfn_t *bufioreq_pfn,
                                            evtchn_port_t *bufioreq_evtchn)
{
    return xc_hvm_get_ioreq_server_info(xc, dom, ioservid,
                                        ioreq_pfn, bufioreq_pfn,
                                        bufioreq_evtchn);
}

static inline int xen_set_ioreq_server_state(xc_interface *xc, domid_t dom,
                                             ioservid_t ioservid,
                                             bool enable)
{
    trace_xen_ioreq_server_state(ioservid, enable);
    return xc_hvm_set_ioreq_server_state(xc, dom, ioservid, enable);
}

#endif

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 460
static inline int xen_xc_domain_add_to_physmap(xc_interface *xch, uint32_t domid,
                                               unsigned int space,
                                               unsigned long idx,
                                               xen_pfn_t gpfn)
{
    return xc_domain_add_to_physmap(xch, domid, space, idx, gpfn);
}
#else
static inline int xen_xc_domain_add_to_physmap(xc_interface *xch, uint32_t domid,
                                               unsigned int space,
                                               unsigned long idx,
                                               xen_pfn_t gpfn)
{
    /* In Xen 4.6 rc is -1 and errno contains the error value. */
    int rc = xc_domain_add_to_physmap(xch, domid, space, idx, gpfn);
    if (rc == -1)
        return errno;
    return rc;
}
#endif

#ifdef CONFIG_XEN_PV_DOMAIN_BUILD
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 470
static inline int xen_domain_create(xc_interface *xc, uint32_t ssidref,
                                    xen_domain_handle_t handle, uint32_t flags,
                                    uint32_t *pdomid)
{
    return xc_domain_create(xc, ssidref, handle, flags, pdomid);
}
#else
static inline int xen_domain_create(xc_interface *xc, uint32_t ssidref,
                                    xen_domain_handle_t handle, uint32_t flags,
                                    uint32_t *pdomid)
{
    return xc_domain_create(xc, ssidref, handle, flags, pdomid, NULL);
}
#endif
#endif

#endif /* QEMU_HW_XEN_COMMON_H */
