#ifndef QEMU_HW_XEN_COMMON_H
#define QEMU_HW_XEN_COMMON_H

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
#include "hw/xen/interface/io/xenbus.h"

#include "hw/hw.h"
#include "hw/xen/xen.h"
#include "hw/pci/pci.h"
#include "qemu/queue.h"
#include "hw/xen/trace.h"

extern xc_interface *xen_xc;

/*
 * We don't support Xen prior to 4.2.0.
 */

/* Xen 4.2 through 4.6 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 40701

typedef xc_interface xenforeignmemory_handle;
typedef xc_evtchn xenevtchn_handle;
typedef xc_gnttab xengnttab_handle;
typedef evtchn_port_or_error_t xenevtchn_port_or_error_t;

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
#define xengnttab_map_domain_grant_refs(h, c, d, r, p) \
    xc_gnttab_map_domain_grant_refs(h, c, d, r, p)

#define xenforeignmemory_open(l, f) xen_xc
#define xenforeignmemory_close(h)

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

#else /* CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40701 */

#include <xenevtchn.h>
#include <xengnttab.h>
#include <xenforeignmemory.h>

#endif

extern xenforeignmemory_handle *xen_fmem;

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 40900

typedef xc_interface xendevicemodel_handle;

#else /* CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40900 */

#undef XC_WANT_COMPAT_DEVICEMODEL_API
#include <xendevicemodel.h>

#endif

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 41100

static inline int xendevicemodel_relocate_memory(
    xendevicemodel_handle *dmod, domid_t domid, uint32_t size, uint64_t src_gfn,
    uint64_t dst_gfn)
{
    uint32_t i;
    int rc;

    for (i = 0; i < size; i++) {
        unsigned long idx = src_gfn + i;
        xen_pfn_t gpfn = dst_gfn + i;

        rc = xc_domain_add_to_physmap(xen_xc, domid, XENMAPSPACE_gmfn, idx,
                                      gpfn);
        if (rc) {
            return rc;
        }
    }

    return 0;
}

static inline int xendevicemodel_pin_memory_cacheattr(
    xendevicemodel_handle *dmod, domid_t domid, uint64_t start, uint64_t end,
    uint32_t type)
{
    return xc_domain_pin_memory_cacheattr(xen_xc, domid, start, end, type);
}

typedef void xenforeignmemory_resource_handle;

#define XENMEM_resource_ioreq_server 0

#define XENMEM_resource_ioreq_server_frame_bufioreq 0
#define XENMEM_resource_ioreq_server_frame_ioreq(n) (1 + (n))

static inline xenforeignmemory_resource_handle *xenforeignmemory_map_resource(
    xenforeignmemory_handle *fmem, domid_t domid, unsigned int type,
    unsigned int id, unsigned long frame, unsigned long nr_frames,
    void **paddr, int prot, int flags)
{
    errno = EOPNOTSUPP;
    return NULL;
}

#endif /* CONFIG_XEN_CTRL_INTERFACE_VERSION < 41100 */

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 41000

#define XEN_COMPAT_PHYSMAP
static inline void *xenforeignmemory_map2(xenforeignmemory_handle *h,
                                          uint32_t dom, void *addr,
                                          int prot, int flags, size_t pages,
                                          const xen_pfn_t arr[/*pages*/],
                                          int err[/*pages*/])
{
    assert(addr == NULL && flags == 0);
    return xenforeignmemory_map(h, dom, prot, pages, arr, err);
}

static inline int xentoolcore_restrict_all(domid_t domid)
{
    errno = ENOTTY;
    return -1;
}

static inline int xendevicemodel_shutdown(xendevicemodel_handle *dmod,
                                          domid_t domid, unsigned int reason)
{
    errno = ENOTTY;
    return -1;
}

#else /* CONFIG_XEN_CTRL_INTERFACE_VERSION >= 41000 */

#include <xentoolcore.h>

#endif

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 40900

static inline xendevicemodel_handle *xendevicemodel_open(
    struct xentoollog_logger *logger, unsigned int open_flags)
{
    return xen_xc;
}

#if CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40500

static inline int xendevicemodel_create_ioreq_server(
    xendevicemodel_handle *dmod, domid_t domid, int handle_bufioreq,
    ioservid_t *id)
{
    return xc_hvm_create_ioreq_server(dmod, domid, handle_bufioreq,
                                      id);
}

static inline int xendevicemodel_get_ioreq_server_info(
    xendevicemodel_handle *dmod, domid_t domid, ioservid_t id,
    xen_pfn_t *ioreq_pfn, xen_pfn_t *bufioreq_pfn,
    evtchn_port_t *bufioreq_port)
{
    return xc_hvm_get_ioreq_server_info(dmod, domid, id, ioreq_pfn,
                                        bufioreq_pfn, bufioreq_port);
}

static inline int xendevicemodel_map_io_range_to_ioreq_server(
    xendevicemodel_handle *dmod, domid_t domid, ioservid_t id, int is_mmio,
    uint64_t start, uint64_t end)
{
    return xc_hvm_map_io_range_to_ioreq_server(dmod, domid, id, is_mmio,
                                               start, end);
}

static inline int xendevicemodel_unmap_io_range_from_ioreq_server(
    xendevicemodel_handle *dmod, domid_t domid, ioservid_t id, int is_mmio,
    uint64_t start, uint64_t end)
{
    return xc_hvm_unmap_io_range_from_ioreq_server(dmod, domid, id, is_mmio,
                                                   start, end);
}

static inline int xendevicemodel_map_pcidev_to_ioreq_server(
    xendevicemodel_handle *dmod, domid_t domid, ioservid_t id,
    uint16_t segment, uint8_t bus, uint8_t device, uint8_t function)
{
    return xc_hvm_map_pcidev_to_ioreq_server(dmod, domid, id, segment,
                                             bus, device, function);
}

static inline int xendevicemodel_unmap_pcidev_from_ioreq_server(
    xendevicemodel_handle *dmod, domid_t domid, ioservid_t id,
    uint16_t segment, uint8_t bus, uint8_t device, uint8_t function)
{
    return xc_hvm_unmap_pcidev_from_ioreq_server(dmod, domid, id, segment,
                                                 bus, device, function);
}

static inline int xendevicemodel_destroy_ioreq_server(
    xendevicemodel_handle *dmod, domid_t domid, ioservid_t id)
{
    return xc_hvm_destroy_ioreq_server(dmod, domid, id);
}

static inline int xendevicemodel_set_ioreq_server_state(
    xendevicemodel_handle *dmod, domid_t domid, ioservid_t id, int enabled)
{
    return xc_hvm_set_ioreq_server_state(dmod, domid, id, enabled);
}

#endif /* CONFIG_XEN_CTRL_INTERFACE_VERSION >= 40500 */

static inline int xendevicemodel_set_pci_intx_level(
    xendevicemodel_handle *dmod, domid_t domid, uint16_t segment,
    uint8_t bus, uint8_t device, uint8_t intx, unsigned int level)
{
    return xc_hvm_set_pci_intx_level(dmod, domid, segment, bus, device,
                                     intx, level);
}

static inline int xendevicemodel_set_isa_irq_level(
    xendevicemodel_handle *dmod, domid_t domid, uint8_t irq,
    unsigned int level)
{
    return xc_hvm_set_isa_irq_level(dmod, domid, irq, level);
}

static inline int xendevicemodel_set_pci_link_route(
    xendevicemodel_handle *dmod, domid_t domid, uint8_t link, uint8_t irq)
{
    return xc_hvm_set_pci_link_route(dmod, domid, link, irq);
}

static inline int xendevicemodel_inject_msi(
    xendevicemodel_handle *dmod, domid_t domid, uint64_t msi_addr,
    uint32_t msi_data)
{
    return xc_hvm_inject_msi(dmod, domid, msi_addr, msi_data);
}

static inline int xendevicemodel_track_dirty_vram(
    xendevicemodel_handle *dmod, domid_t domid, uint64_t first_pfn,
    uint32_t nr, unsigned long *dirty_bitmap)
{
    return xc_hvm_track_dirty_vram(dmod, domid, first_pfn, nr,
                                   dirty_bitmap);
}

static inline int xendevicemodel_modified_memory(
    xendevicemodel_handle *dmod, domid_t domid, uint64_t first_pfn,
    uint32_t nr)
{
    return xc_hvm_modified_memory(dmod, domid, first_pfn, nr);
}

static inline int xendevicemodel_set_mem_type(
    xendevicemodel_handle *dmod, domid_t domid, hvmmem_type_t mem_type,
    uint64_t first_pfn, uint32_t nr)
{
    return xc_hvm_set_mem_type(dmod, domid, mem_type, first_pfn, nr);
}

#endif

extern xendevicemodel_handle *xen_dmod;

static inline int xen_set_mem_type(domid_t domid, hvmmem_type_t type,
                                   uint64_t first_pfn, uint32_t nr)
{
    return xendevicemodel_set_mem_type(xen_dmod, domid, type, first_pfn,
                                       nr);
}

static inline int xen_set_pci_intx_level(domid_t domid, uint16_t segment,
                                         uint8_t bus, uint8_t device,
                                         uint8_t intx, unsigned int level)
{
    return xendevicemodel_set_pci_intx_level(xen_dmod, domid, segment, bus,
                                             device, intx, level);
}

static inline int xen_set_pci_link_route(domid_t domid, uint8_t link,
                                         uint8_t irq)
{
    return xendevicemodel_set_pci_link_route(xen_dmod, domid, link, irq);
}

static inline int xen_inject_msi(domid_t domid, uint64_t msi_addr,
                                 uint32_t msi_data)
{
    return xendevicemodel_inject_msi(xen_dmod, domid, msi_addr, msi_data);
}

static inline int xen_set_isa_irq_level(domid_t domid, uint8_t irq,
                                        unsigned int level)
{
    return xendevicemodel_set_isa_irq_level(xen_dmod, domid, irq, level);
}

static inline int xen_track_dirty_vram(domid_t domid, uint64_t first_pfn,
                                       uint32_t nr, unsigned long *bitmap)
{
    return xendevicemodel_track_dirty_vram(xen_dmod, domid, first_pfn, nr,
                                           bitmap);
}

static inline int xen_modified_memory(domid_t domid, uint64_t first_pfn,
                                      uint32_t nr)
{
    return xendevicemodel_modified_memory(xen_dmod, domid, first_pfn, nr);
}

static inline int xen_restrict(domid_t domid)
{
    int rc;
    rc = xentoolcore_restrict_all(domid);
    trace_xen_domid_restrict(rc ? errno : 0);
    return rc;
}

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
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 40600

#ifndef HVM_IOREQSRV_BUFIOREQ_ATOMIC
#define HVM_IOREQSRV_BUFIOREQ_ATOMIC 2
#endif

#endif

static inline int xen_get_default_ioreq_server_info(domid_t dom,
                                                    xen_pfn_t *ioreq_pfn,
                                                    xen_pfn_t *bufioreq_pfn,
                                                    evtchn_port_t
                                                        *bufioreq_evtchn)
{
    unsigned long param;
    int rc;

    rc = xc_get_hvm_param(xen_xc, dom, HVM_PARAM_IOREQ_PFN, &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_IOREQ_PFN\n");
        return -1;
    }

    *ioreq_pfn = param;

    rc = xc_get_hvm_param(xen_xc, dom, HVM_PARAM_BUFIOREQ_PFN, &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_BUFIOREQ_PFN\n");
        return -1;
    }

    *bufioreq_pfn = param;

    rc = xc_get_hvm_param(xen_xc, dom, HVM_PARAM_BUFIOREQ_EVTCHN,
                          &param);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_BUFIOREQ_EVTCHN\n");
        return -1;
    }

    *bufioreq_evtchn = param;

    return 0;
}

/* Xen before 4.5 */
#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 40500

#ifndef HVM_PARAM_BUFIOREQ_EVTCHN
#define HVM_PARAM_BUFIOREQ_EVTCHN 26
#endif

#define IOREQ_TYPE_PCI_CONFIG 2

typedef uint16_t ioservid_t;

static inline void xen_map_memory_section(domid_t dom,
                                          ioservid_t ioservid,
                                          MemoryRegionSection *section)
{
}

static inline void xen_unmap_memory_section(domid_t dom,
                                            ioservid_t ioservid,
                                            MemoryRegionSection *section)
{
}

static inline void xen_map_io_section(domid_t dom,
                                      ioservid_t ioservid,
                                      MemoryRegionSection *section)
{
}

static inline void xen_unmap_io_section(domid_t dom,
                                        ioservid_t ioservid,
                                        MemoryRegionSection *section)
{
}

static inline void xen_map_pcidev(domid_t dom,
                                  ioservid_t ioservid,
                                  PCIDevice *pci_dev)
{
}

static inline void xen_unmap_pcidev(domid_t dom,
                                    ioservid_t ioservid,
                                    PCIDevice *pci_dev)
{
}

static inline void xen_create_ioreq_server(domid_t dom,
                                           ioservid_t *ioservid)
{
}

static inline void xen_destroy_ioreq_server(domid_t dom,
                                            ioservid_t ioservid)
{
}

static inline int xen_get_ioreq_server_info(domid_t dom,
                                            ioservid_t ioservid,
                                            xen_pfn_t *ioreq_pfn,
                                            xen_pfn_t *bufioreq_pfn,
                                            evtchn_port_t *bufioreq_evtchn)
{
    return xen_get_default_ioreq_server_info(dom, ioreq_pfn,
                                             bufioreq_pfn,
                                             bufioreq_evtchn);
}

static inline int xen_set_ioreq_server_state(domid_t dom,
                                             ioservid_t ioservid,
                                             bool enable)
{
    return 0;
}

/* Xen 4.5 */
#else

static bool use_default_ioreq_server;

static inline void xen_map_memory_section(domid_t dom,
                                          ioservid_t ioservid,
                                          MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_map_mmio_range(ioservid, start_addr, end_addr);
    xendevicemodel_map_io_range_to_ioreq_server(xen_dmod, dom, ioservid, 1,
                                                start_addr, end_addr);
}

static inline void xen_unmap_memory_section(domid_t dom,
                                            ioservid_t ioservid,
                                            MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_unmap_mmio_range(ioservid, start_addr, end_addr);
    xendevicemodel_unmap_io_range_from_ioreq_server(xen_dmod, dom, ioservid,
                                                    1, start_addr, end_addr);
}

static inline void xen_map_io_section(domid_t dom,
                                      ioservid_t ioservid,
                                      MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_map_portio_range(ioservid, start_addr, end_addr);
    xendevicemodel_map_io_range_to_ioreq_server(xen_dmod, dom, ioservid, 0,
                                                start_addr, end_addr);
}

static inline void xen_unmap_io_section(domid_t dom,
                                        ioservid_t ioservid,
                                        MemoryRegionSection *section)
{
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    hwaddr end_addr = start_addr + size - 1;

    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_unmap_portio_range(ioservid, start_addr, end_addr);
    xendevicemodel_unmap_io_range_from_ioreq_server(xen_dmod, dom, ioservid,
                                                    0, start_addr, end_addr);
}

static inline void xen_map_pcidev(domid_t dom,
                                  ioservid_t ioservid,
                                  PCIDevice *pci_dev)
{
    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_map_pcidev(ioservid, pci_dev_bus_num(pci_dev),
                         PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
    xendevicemodel_map_pcidev_to_ioreq_server(xen_dmod, dom, ioservid, 0,
                                              pci_dev_bus_num(pci_dev),
                                              PCI_SLOT(pci_dev->devfn),
                                              PCI_FUNC(pci_dev->devfn));
}

static inline void xen_unmap_pcidev(domid_t dom,
                                    ioservid_t ioservid,
                                    PCIDevice *pci_dev)
{
    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_unmap_pcidev(ioservid, pci_dev_bus_num(pci_dev),
                           PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn));
    xendevicemodel_unmap_pcidev_from_ioreq_server(xen_dmod, dom, ioservid, 0,
                                                  pci_dev_bus_num(pci_dev),
                                                  PCI_SLOT(pci_dev->devfn),
                                                  PCI_FUNC(pci_dev->devfn));
}

static inline void xen_create_ioreq_server(domid_t dom,
                                           ioservid_t *ioservid)
{
    int rc = xendevicemodel_create_ioreq_server(xen_dmod, dom,
                                                HVM_IOREQSRV_BUFIOREQ_ATOMIC,
                                                ioservid);

    if (rc == 0) {
        trace_xen_ioreq_server_create(*ioservid);
        return;
    }

    *ioservid = 0;
    use_default_ioreq_server = true;
    trace_xen_default_ioreq_server();
}

static inline void xen_destroy_ioreq_server(domid_t dom,
                                            ioservid_t ioservid)
{
    if (use_default_ioreq_server) {
        return;
    }

    trace_xen_ioreq_server_destroy(ioservid);
    xendevicemodel_destroy_ioreq_server(xen_dmod, dom, ioservid);
}

static inline int xen_get_ioreq_server_info(domid_t dom,
                                            ioservid_t ioservid,
                                            xen_pfn_t *ioreq_pfn,
                                            xen_pfn_t *bufioreq_pfn,
                                            evtchn_port_t *bufioreq_evtchn)
{
    if (use_default_ioreq_server) {
        return xen_get_default_ioreq_server_info(dom, ioreq_pfn,
                                                 bufioreq_pfn,
                                                 bufioreq_evtchn);
    }

    return xendevicemodel_get_ioreq_server_info(xen_dmod, dom, ioservid,
                                                ioreq_pfn, bufioreq_pfn,
                                                bufioreq_evtchn);
}

static inline int xen_set_ioreq_server_state(domid_t dom,
                                             ioservid_t ioservid,
                                             bool enable)
{
    if (use_default_ioreq_server) {
        return 0;
    }

    trace_xen_ioreq_server_state(ioservid, enable);
    return xendevicemodel_set_ioreq_server_state(xen_dmod, dom, ioservid,
                                                 enable);
}

#endif

/* Xen before 4.8 */

#if CONFIG_XEN_CTRL_INTERFACE_VERSION < 40800

struct xengnttab_grant_copy_segment {
    union xengnttab_copy_ptr {
        void *virt;
        struct {
            uint32_t ref;
            uint16_t offset;
            uint16_t domid;
        } foreign;
    } source, dest;
    uint16_t len;
    uint16_t flags;
    int16_t status;
};

typedef struct xengnttab_grant_copy_segment xengnttab_grant_copy_segment_t;

static inline int xengnttab_grant_copy(xengnttab_handle *xgt, uint32_t count,
                                       xengnttab_grant_copy_segment_t *segs)
{
    return -ENOSYS;
}
#endif

#endif /* QEMU_HW_XEN_COMMON_H */
