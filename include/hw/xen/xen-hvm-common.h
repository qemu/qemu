#ifndef HW_XEN_HVM_COMMON_H
#define HW_XEN_HVM_COMMON_H

#include "qemu/units.h"

#include "cpu.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/xen/xen_native.h"
#include "hw/xen/xen-legacy-backend.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "sysemu/xen.h"
#include "sysemu/xen-mapcache.h"
#include "qemu/error-report.h"
#include <xen/hvm/ioreq.h>

extern MemoryRegion ram_memory;
extern MemoryListener xen_io_listener;
extern DeviceListener xen_device_listener;

//#define DEBUG_XEN_HVM

#ifdef DEBUG_XEN_HVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "xen: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static inline uint32_t xen_vcpu_eport(shared_iopage_t *shared_page, int i)
{
    return shared_page->vcpu_ioreq[i].vp_eport;
}
static inline ioreq_t *xen_vcpu_ioreq(shared_iopage_t *shared_page, int vcpu)
{
    return &shared_page->vcpu_ioreq[vcpu];
}

#define BUFFER_IO_MAX_DELAY  100

typedef struct XenPhysmap {
    hwaddr start_addr;
    ram_addr_t size;
    const char *name;
    hwaddr phys_offset;

    QLIST_ENTRY(XenPhysmap) list;
} XenPhysmap;

typedef struct XenPciDevice {
    PCIDevice *pci_dev;
    uint32_t sbdf;
    QLIST_ENTRY(XenPciDevice) entry;
} XenPciDevice;

typedef struct XenIOState {
    ioservid_t ioservid;
    shared_iopage_t *shared_page;
    buffered_iopage_t *buffered_io_page;
    xenforeignmemory_resource_handle *fres;
    QEMUTimer *buffered_io_timer;
    CPUState **cpu_by_vcpu_id;
    /* the evtchn port for polling the notification, */
    evtchn_port_t *ioreq_local_port;
    /* evtchn remote and local ports for buffered io */
    evtchn_port_t bufioreq_remote_port;
    evtchn_port_t bufioreq_local_port;
    /* the evtchn fd for polling */
    xenevtchn_handle *xce_handle;
    /* which vcpu we are serving */
    int send_vcpu;

    struct xs_handle *xenstore;
    MemoryListener memory_listener;
    MemoryListener io_listener;
    QLIST_HEAD(, XenPciDevice) dev_list;
    DeviceListener device_listener;

    Notifier exit;
} XenIOState;

void xen_exit_notifier(Notifier *n, void *data);

void xen_region_add(MemoryListener *listener, MemoryRegionSection *section);
void xen_region_del(MemoryListener *listener, MemoryRegionSection *section);
void xen_io_add(MemoryListener *listener, MemoryRegionSection *section);
void xen_io_del(MemoryListener *listener, MemoryRegionSection *section);
void xen_device_realize(DeviceListener *listener, DeviceState *dev);
void xen_device_unrealize(DeviceListener *listener, DeviceState *dev);

void xen_hvm_change_state_handler(void *opaque, bool running, RunState rstate);
void xen_register_ioreq(XenIOState *state, unsigned int max_cpus,
                        const MemoryListener *xen_memory_listener);

void cpu_ioreq_pio(ioreq_t *req);
#endif /* HW_XEN_HVM_COMMON_H */
