#ifndef QEMU_HW_XEN_COMMON_H
#define QEMU_HW_XEN_COMMON_H 1

#include "config-host.h"

#include <stddef.h>
#include <inttypes.h>

#include <xenctrl.h>
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 420
#  include <xs.h>
#else
#  include <xenstore.h>
#endif
#include <xen/io/xenbus.h>

#include "hw/hw.h"
#include "hw/xen/xen.h"
#include "hw/pci/pci.h"
#include "qemu/queue.h"
#include "trace.h"

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

static inline struct xs_handle *xs_open(unsigned long flags)
{
    return xs_daemon_open();
}

static inline void xs_close(struct xs_handle *xsh)
{
    if (xsh != NULL) {
        xs_daemon_close(xsh);
    }
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

/* Xen before 4.2 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 420
static inline int xen_xc_hvm_inject_msi(XenXC xen_xc, domid_t dom,
        uint64_t addr, uint32_t data)
{
    return -ENOSYS;
}
/* The followings are only to compile op_discard related code on older
 * Xen releases. */
#define BLKIF_OP_DISCARD 5
struct blkif_request_discard {
    uint64_t nr_sectors;
    uint64_t sector_number;
};
#else
static inline int xen_xc_hvm_inject_msi(XenXC xen_xc, domid_t dom,
        uint64_t addr, uint32_t data)
{
    return xc_hvm_inject_msi(xen_xc, dom, addr, data);
}
#endif

void destroy_hvm_domain(bool reboot);

/* shutdown/destroy current domain because of an error */
void xen_shutdown_fatal_error(const char *fmt, ...) GCC_FMT_ATTR(1, 2);

#ifdef HVM_PARAM_VMPORT_REGS_PFN
static inline int xen_get_vmport_regs_pfn(XenXC xc, domid_t dom,
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
static inline int xen_get_vmport_regs_pfn(XenXC xc, domid_t dom,
                                          xen_pfn_t *vmport_regs_pfn)
{
    return -ENOSYS;
}
#endif

/* Xen before 4.5 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 450

#ifndef HVM_PARAM_BUFIOREQ_EVTCHN
#define HVM_PARAM_BUFIOREQ_EVTCHN 26
#endif

#define IOREQ_TYPE_PCI_CONFIG 2

typedef uint16_t ioservid_t;

static inline void xen_map_memory_section(XenXC xc, domid_t dom,
                                          ioservid_t ioservid,
                                          MemoryRegionSection *section)
{
}

static inline void xen_unmap_memory_section(XenXC xc, domid_t dom,
                                            ioservid_t ioservid,
                                            MemoryRegionSection *section)
{
}

static inline void xen_map_io_section(XenXC xc, domid_t dom,
                                      ioservid_t ioservid,
                                      MemoryRegionSection *section)
{
}

static inline void xen_unmap_io_section(XenXC xc, domid_t dom,
                                        ioservid_t ioservid,
                                        MemoryRegionSection *section)
{
}

static inline void xen_map_pcidev(XenXC xc, domid_t dom,
                                  ioservid_t ioservid,
                                  PCIDevice *pci_dev)
{
}

static inline void xen_unmap_pcidev(XenXC xc, domid_t dom,
                                    ioservid_t ioservid,
                                    PCIDevice *pci_dev)
{
}

static inline int xen_create_ioreq_server(XenXC xc, domid_t dom,
                                          ioservid_t *ioservid)
{
    return 0;
}

static inline void xen_destroy_ioreq_server(XenXC xc, domid_t dom,
                                            ioservid_t ioservid)
{
}

static inline int xen_get_ioreq_server_info(XenXC xc, domid_t dom,
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

static inline int xen_set_ioreq_server_state(XenXC xc, domid_t dom,
                                             ioservid_t ioservid,
                                             bool enable)
{
    return 0;
}

/* Xen 4.5 */
#else

static inline void xen_map_memory_section(XenXC xc, domid_t dom,
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

static inline void xen_unmap_memory_section(XenXC xc, domid_t dom,
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

static inline void xen_map_io_section(XenXC xc, domid_t dom,
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

static inline void xen_unmap_io_section(XenXC xc, domid_t dom,
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

static inline void xen_map_pcidev(XenXC xc, domid_t dom,
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

static inline void xen_unmap_pcidev(XenXC xc, domid_t dom,
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

static inline int xen_create_ioreq_server(XenXC xc, domid_t dom,
                                          ioservid_t *ioservid)
{
    int rc = xc_hvm_create_ioreq_server(xc, dom, 1, ioservid);

    if (rc == 0) {
        trace_xen_ioreq_server_create(*ioservid);
    }

    return rc;
}

static inline void xen_destroy_ioreq_server(XenXC xc, domid_t dom,
                                            ioservid_t ioservid)
{
    trace_xen_ioreq_server_destroy(ioservid);
    xc_hvm_destroy_ioreq_server(xc, dom, ioservid);
}

static inline int xen_get_ioreq_server_info(XenXC xc, domid_t dom,
                                            ioservid_t ioservid,
                                            xen_pfn_t *ioreq_pfn,
                                            xen_pfn_t *bufioreq_pfn,
                                            evtchn_port_t *bufioreq_evtchn)
{
    return xc_hvm_get_ioreq_server_info(xc, dom, ioservid,
                                        ioreq_pfn, bufioreq_pfn,
                                        bufioreq_evtchn);
}

static inline int xen_set_ioreq_server_state(XenXC xc, domid_t dom,
                                             ioservid_t ioservid,
                                             bool enable)
{
    trace_xen_ioreq_server_state(ioservid, enable);
    return xc_hvm_set_ioreq_server_state(xc, dom, ioservid, enable);
}

#endif

#endif /* QEMU_HW_XEN_COMMON_H */
