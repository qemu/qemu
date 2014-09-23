/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include <sys/mman.h>

#include "hw/pci/pci.h"
#include "hw/i386/pc.h"
#include "hw/xen/xen_common.h"
#include "hw/xen/xen_backend.h"
#include "qmp-commands.h"

#include "sysemu/char.h"
#include "qemu/range.h"
#include "sysemu/xen-mapcache.h"
#include "trace.h"
#include "exec/address-spaces.h"

#include <xen/hvm/ioreq.h>
#include <xen/hvm/params.h>
#include <xen/hvm/e820.h>

//#define DEBUG_XEN_HVM

#ifdef DEBUG_XEN_HVM
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "xen: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

static MemoryRegion ram_memory, ram_640k, ram_lo, ram_hi;
static MemoryRegion *framebuffer;
static bool xen_in_migration;

/* Compatibility with older version */
#if __XEN_LATEST_INTERFACE_VERSION__ < 0x0003020a
static inline uint32_t xen_vcpu_eport(shared_iopage_t *shared_page, int i)
{
    return shared_page->vcpu_iodata[i].vp_eport;
}
static inline ioreq_t *xen_vcpu_ioreq(shared_iopage_t *shared_page, int vcpu)
{
    return &shared_page->vcpu_iodata[vcpu].vp_ioreq;
}
#  define FMT_ioreq_size PRIx64
#else
static inline uint32_t xen_vcpu_eport(shared_iopage_t *shared_page, int i)
{
    return shared_page->vcpu_ioreq[i].vp_eport;
}
static inline ioreq_t *xen_vcpu_ioreq(shared_iopage_t *shared_page, int vcpu)
{
    return &shared_page->vcpu_ioreq[vcpu];
}
#  define FMT_ioreq_size "u"
#endif
#ifndef HVM_PARAM_BUFIOREQ_EVTCHN
#define HVM_PARAM_BUFIOREQ_EVTCHN 26
#endif

#define BUFFER_IO_MAX_DELAY  100

typedef struct XenPhysmap {
    hwaddr start_addr;
    ram_addr_t size;
    const char *name;
    hwaddr phys_offset;

    QLIST_ENTRY(XenPhysmap) list;
} XenPhysmap;

typedef struct XenIOState {
    shared_iopage_t *shared_page;
    buffered_iopage_t *buffered_io_page;
    QEMUTimer *buffered_io_timer;
    /* the evtchn port for polling the notification, */
    evtchn_port_t *ioreq_local_port;
    /* evtchn local port for buffered io */
    evtchn_port_t bufioreq_local_port;
    /* the evtchn fd for polling */
    XenEvtchn xce_handle;
    /* which vcpu we are serving */
    int send_vcpu;

    struct xs_handle *xenstore;
    MemoryListener memory_listener;
    QLIST_HEAD(, XenPhysmap) physmap;
    hwaddr free_phys_offset;
    const XenPhysmap *log_for_dirtybit;

    Notifier exit;
    Notifier suspend;
    Notifier wakeup;
} XenIOState;

/* Xen specific function for piix pci */

int xen_pci_slot_get_pirq(PCIDevice *pci_dev, int irq_num)
{
    return irq_num + ((pci_dev->devfn >> 3) << 2);
}

void xen_piix3_set_irq(void *opaque, int irq_num, int level)
{
    xc_hvm_set_pci_intx_level(xen_xc, xen_domid, 0, 0, irq_num >> 2,
                              irq_num & 3, level);
}

void xen_piix_pci_write_config_client(uint32_t address, uint32_t val, int len)
{
    int i;

    /* Scan for updates to PCI link routes (0x60-0x63). */
    for (i = 0; i < len; i++) {
        uint8_t v = (val >> (8 * i)) & 0xff;
        if (v & 0x80) {
            v = 0;
        }
        v &= 0xf;
        if (((address + i) >= 0x60) && ((address + i) <= 0x63)) {
            xc_hvm_set_pci_link_route(xen_xc, xen_domid, address + i - 0x60, v);
        }
    }
}

void xen_hvm_inject_msi(uint64_t addr, uint32_t data)
{
    xen_xc_hvm_inject_msi(xen_xc, xen_domid, addr, data);
}

static void xen_suspend_notifier(Notifier *notifier, void *data)
{
    xc_set_hvm_param(xen_xc, xen_domid, HVM_PARAM_ACPI_S_STATE, 3);
}

/* Xen Interrupt Controller */

static void xen_set_irq(void *opaque, int irq, int level)
{
    xc_hvm_set_isa_irq_level(xen_xc, xen_domid, irq, level);
}

qemu_irq *xen_interrupt_controller_init(void)
{
    return qemu_allocate_irqs(xen_set_irq, NULL, 16);
}

/* Memory Ops */

static void xen_ram_init(ram_addr_t *below_4g_mem_size,
                         ram_addr_t *above_4g_mem_size,
                         ram_addr_t ram_size, MemoryRegion **ram_memory_p)
{
    MemoryRegion *sysmem = get_system_memory();
    ram_addr_t block_len;
    uint64_t user_lowmem = object_property_get_int(qdev_get_machine(),
                                                   PC_MACHINE_MAX_RAM_BELOW_4G,
                                                   &error_abort);

    /* Handle the machine opt max-ram-below-4g.  It is basically doing
     * min(xen limit, user limit).
     */
    if (HVM_BELOW_4G_RAM_END <= user_lowmem) {
        user_lowmem = HVM_BELOW_4G_RAM_END;
    }

    if (ram_size >= user_lowmem) {
        *above_4g_mem_size = ram_size - user_lowmem;
        *below_4g_mem_size = user_lowmem;
    } else {
        *above_4g_mem_size = 0;
        *below_4g_mem_size = ram_size;
    }
    if (!*above_4g_mem_size) {
        block_len = ram_size;
    } else {
        /*
         * Xen does not allocate the memory continuously, it keeps a
         * hole of the size computed above or passed in.
         */
        block_len = (1ULL << 32) + *above_4g_mem_size;
    }
    memory_region_init_ram(&ram_memory, NULL, "xen.ram", block_len,
                           &error_abort);
    *ram_memory_p = &ram_memory;
    vmstate_register_ram_global(&ram_memory);

    memory_region_init_alias(&ram_640k, NULL, "xen.ram.640k",
                             &ram_memory, 0, 0xa0000);
    memory_region_add_subregion(sysmem, 0, &ram_640k);
    /* Skip of the VGA IO memory space, it will be registered later by the VGA
     * emulated device.
     *
     * The area between 0xc0000 and 0x100000 will be used by SeaBIOS to load
     * the Options ROM, so it is registered here as RAM.
     */
    memory_region_init_alias(&ram_lo, NULL, "xen.ram.lo",
                             &ram_memory, 0xc0000,
                             *below_4g_mem_size - 0xc0000);
    memory_region_add_subregion(sysmem, 0xc0000, &ram_lo);
    if (*above_4g_mem_size > 0) {
        memory_region_init_alias(&ram_hi, NULL, "xen.ram.hi",
                                 &ram_memory, 0x100000000ULL,
                                 *above_4g_mem_size);
        memory_region_add_subregion(sysmem, 0x100000000ULL, &ram_hi);
    }
}

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size, MemoryRegion *mr)
{
    unsigned long nr_pfn;
    xen_pfn_t *pfn_list;
    int i;

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        /* RAM already populated in Xen */
        fprintf(stderr, "%s: do not alloc "RAM_ADDR_FMT
                " bytes of ram at "RAM_ADDR_FMT" when runstate is INMIGRATE\n",
                __func__, size, ram_addr); 
        return;
    }

    if (mr == &ram_memory) {
        return;
    }

    trace_xen_ram_alloc(ram_addr, size);

    nr_pfn = size >> TARGET_PAGE_BITS;
    pfn_list = g_malloc(sizeof (*pfn_list) * nr_pfn);

    for (i = 0; i < nr_pfn; i++) {
        pfn_list[i] = (ram_addr >> TARGET_PAGE_BITS) + i;
    }

    if (xc_domain_populate_physmap_exact(xen_xc, xen_domid, nr_pfn, 0, 0, pfn_list)) {
        hw_error("xen: failed to populate ram at " RAM_ADDR_FMT, ram_addr);
    }

    g_free(pfn_list);
}

static XenPhysmap *get_physmapping(XenIOState *state,
                                   hwaddr start_addr, ram_addr_t size)
{
    XenPhysmap *physmap = NULL;

    start_addr &= TARGET_PAGE_MASK;

    QLIST_FOREACH(physmap, &state->physmap, list) {
        if (range_covers_byte(physmap->start_addr, physmap->size, start_addr)) {
            return physmap;
        }
    }
    return NULL;
}

static hwaddr xen_phys_offset_to_gaddr(hwaddr start_addr,
                                                   ram_addr_t size, void *opaque)
{
    hwaddr addr = start_addr & TARGET_PAGE_MASK;
    XenIOState *xen_io_state = opaque;
    XenPhysmap *physmap = NULL;

    QLIST_FOREACH(physmap, &xen_io_state->physmap, list) {
        if (range_covers_byte(physmap->phys_offset, physmap->size, addr)) {
            return physmap->start_addr;
        }
    }

    return start_addr;
}

#if CONFIG_XEN_CTRL_INTERFACE_VERSION >= 340
static int xen_add_to_physmap(XenIOState *state,
                              hwaddr start_addr,
                              ram_addr_t size,
                              MemoryRegion *mr,
                              hwaddr offset_within_region)
{
    unsigned long i = 0;
    int rc = 0;
    XenPhysmap *physmap = NULL;
    hwaddr pfn, start_gpfn;
    hwaddr phys_offset = memory_region_get_ram_addr(mr);
    char path[80], value[17];
    const char *mr_name;

    if (get_physmapping(state, start_addr, size)) {
        return 0;
    }
    if (size <= 0) {
        return -1;
    }

    /* Xen can only handle a single dirty log region for now and we want
     * the linear framebuffer to be that region.
     * Avoid tracking any regions that is not videoram and avoid tracking
     * the legacy vga region. */
    if (mr == framebuffer && start_addr > 0xbffff) {
        goto go_physmap;
    }
    return -1;

go_physmap:
    DPRINTF("mapping vram to %"HWADDR_PRIx" - %"HWADDR_PRIx"\n",
            start_addr, start_addr + size);

    pfn = phys_offset >> TARGET_PAGE_BITS;
    start_gpfn = start_addr >> TARGET_PAGE_BITS;
    for (i = 0; i < size >> TARGET_PAGE_BITS; i++) {
        unsigned long idx = pfn + i;
        xen_pfn_t gpfn = start_gpfn + i;

        rc = xc_domain_add_to_physmap(xen_xc, xen_domid, XENMAPSPACE_gmfn, idx, gpfn);
        if (rc) {
            DPRINTF("add_to_physmap MFN %"PRI_xen_pfn" to PFN %"
                    PRI_xen_pfn" failed: %d\n", idx, gpfn, rc);
            return -rc;
        }
    }

    mr_name = memory_region_name(mr);

    physmap = g_malloc(sizeof (XenPhysmap));

    physmap->start_addr = start_addr;
    physmap->size = size;
    physmap->name = mr_name;
    physmap->phys_offset = phys_offset;

    QLIST_INSERT_HEAD(&state->physmap, physmap, list);

    xc_domain_pin_memory_cacheattr(xen_xc, xen_domid,
                                   start_addr >> TARGET_PAGE_BITS,
                                   (start_addr + size - 1) >> TARGET_PAGE_BITS,
                                   XEN_DOMCTL_MEM_CACHEATTR_WB);

    snprintf(path, sizeof(path),
            "/local/domain/0/device-model/%d/physmap/%"PRIx64"/start_addr",
            xen_domid, (uint64_t)phys_offset);
    snprintf(value, sizeof(value), "%"PRIx64, (uint64_t)start_addr);
    if (!xs_write(state->xenstore, 0, path, value, strlen(value))) {
        return -1;
    }
    snprintf(path, sizeof(path),
            "/local/domain/0/device-model/%d/physmap/%"PRIx64"/size",
            xen_domid, (uint64_t)phys_offset);
    snprintf(value, sizeof(value), "%"PRIx64, (uint64_t)size);
    if (!xs_write(state->xenstore, 0, path, value, strlen(value))) {
        return -1;
    }
    if (mr_name) {
        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%"PRIx64"/name",
                xen_domid, (uint64_t)phys_offset);
        if (!xs_write(state->xenstore, 0, path, mr_name, strlen(mr_name))) {
            return -1;
        }
    }

    return 0;
}

static int xen_remove_from_physmap(XenIOState *state,
                                   hwaddr start_addr,
                                   ram_addr_t size)
{
    unsigned long i = 0;
    int rc = 0;
    XenPhysmap *physmap = NULL;
    hwaddr phys_offset = 0;

    physmap = get_physmapping(state, start_addr, size);
    if (physmap == NULL) {
        return -1;
    }

    phys_offset = physmap->phys_offset;
    size = physmap->size;

    DPRINTF("unmapping vram to %"HWADDR_PRIx" - %"HWADDR_PRIx", at "
            "%"HWADDR_PRIx"\n", start_addr, start_addr + size, phys_offset);

    size >>= TARGET_PAGE_BITS;
    start_addr >>= TARGET_PAGE_BITS;
    phys_offset >>= TARGET_PAGE_BITS;
    for (i = 0; i < size; i++) {
        xen_pfn_t idx = start_addr + i;
        xen_pfn_t gpfn = phys_offset + i;

        rc = xc_domain_add_to_physmap(xen_xc, xen_domid, XENMAPSPACE_gmfn, idx, gpfn);
        if (rc) {
            fprintf(stderr, "add_to_physmap MFN %"PRI_xen_pfn" to PFN %"
                    PRI_xen_pfn" failed: %d\n", idx, gpfn, rc);
            return -rc;
        }
    }

    QLIST_REMOVE(physmap, list);
    if (state->log_for_dirtybit == physmap) {
        state->log_for_dirtybit = NULL;
    }
    g_free(physmap);

    return 0;
}

#else
static int xen_add_to_physmap(XenIOState *state,
                              hwaddr start_addr,
                              ram_addr_t size,
                              MemoryRegion *mr,
                              hwaddr offset_within_region)
{
    return -ENOSYS;
}

static int xen_remove_from_physmap(XenIOState *state,
                                   hwaddr start_addr,
                                   ram_addr_t size)
{
    return -ENOSYS;
}
#endif

static void xen_set_memory(struct MemoryListener *listener,
                           MemoryRegionSection *section,
                           bool add)
{
    XenIOState *state = container_of(listener, XenIOState, memory_listener);
    hwaddr start_addr = section->offset_within_address_space;
    ram_addr_t size = int128_get64(section->size);
    bool log_dirty = memory_region_is_logging(section->mr);
    hvmmem_type_t mem_type;

    if (!memory_region_is_ram(section->mr)) {
        return;
    }

    if (!(section->mr != &ram_memory
          && ( (log_dirty && add) || (!log_dirty && !add)))) {
        return;
    }

    trace_xen_client_set_memory(start_addr, size, log_dirty);

    start_addr &= TARGET_PAGE_MASK;
    size = TARGET_PAGE_ALIGN(size);

    if (add) {
        if (!memory_region_is_rom(section->mr)) {
            xen_add_to_physmap(state, start_addr, size,
                               section->mr, section->offset_within_region);
        } else {
            mem_type = HVMMEM_ram_ro;
            if (xc_hvm_set_mem_type(xen_xc, xen_domid, mem_type,
                                    start_addr >> TARGET_PAGE_BITS,
                                    size >> TARGET_PAGE_BITS)) {
                DPRINTF("xc_hvm_set_mem_type error, addr: "TARGET_FMT_plx"\n",
                        start_addr);
            }
        }
    } else {
        if (xen_remove_from_physmap(state, start_addr, size) < 0) {
            DPRINTF("physmapping does not exist at "TARGET_FMT_plx"\n", start_addr);
        }
    }
}

static void xen_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    memory_region_ref(section->mr);
    xen_set_memory(listener, section, true);
}

static void xen_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    xen_set_memory(listener, section, false);
    memory_region_unref(section->mr);
}

static void xen_sync_dirty_bitmap(XenIOState *state,
                                  hwaddr start_addr,
                                  ram_addr_t size)
{
    hwaddr npages = size >> TARGET_PAGE_BITS;
    const int width = sizeof(unsigned long) * 8;
    unsigned long bitmap[(npages + width - 1) / width];
    int rc, i, j;
    const XenPhysmap *physmap = NULL;

    physmap = get_physmapping(state, start_addr, size);
    if (physmap == NULL) {
        /* not handled */
        return;
    }

    if (state->log_for_dirtybit == NULL) {
        state->log_for_dirtybit = physmap;
    } else if (state->log_for_dirtybit != physmap) {
        /* Only one range for dirty bitmap can be tracked. */
        return;
    }

    rc = xc_hvm_track_dirty_vram(xen_xc, xen_domid,
                                 start_addr >> TARGET_PAGE_BITS, npages,
                                 bitmap);
    if (rc < 0) {
#ifndef ENODATA
#define ENODATA  ENOENT
#endif
        if (errno == ENODATA) {
            memory_region_set_dirty(framebuffer, 0, size);
            DPRINTF("xen: track_dirty_vram failed (0x" TARGET_FMT_plx
                    ", 0x" TARGET_FMT_plx "): %s\n",
                    start_addr, start_addr + size, strerror(errno));
        }
        return;
    }

    for (i = 0; i < ARRAY_SIZE(bitmap); i++) {
        unsigned long map = bitmap[i];
        while (map != 0) {
            j = ctzl(map);
            map &= ~(1ul << j);
            memory_region_set_dirty(framebuffer,
                                    (i * width + j) * TARGET_PAGE_SIZE,
                                    TARGET_PAGE_SIZE);
        };
    }
}

static void xen_log_start(MemoryListener *listener,
                          MemoryRegionSection *section)
{
    XenIOState *state = container_of(listener, XenIOState, memory_listener);

    xen_sync_dirty_bitmap(state, section->offset_within_address_space,
                          int128_get64(section->size));
}

static void xen_log_stop(MemoryListener *listener, MemoryRegionSection *section)
{
    XenIOState *state = container_of(listener, XenIOState, memory_listener);

    state->log_for_dirtybit = NULL;
    /* Disable dirty bit tracking */
    xc_hvm_track_dirty_vram(xen_xc, xen_domid, 0, 0, NULL);
}

static void xen_log_sync(MemoryListener *listener, MemoryRegionSection *section)
{
    XenIOState *state = container_of(listener, XenIOState, memory_listener);

    xen_sync_dirty_bitmap(state, section->offset_within_address_space,
                          int128_get64(section->size));
}

static void xen_log_global_start(MemoryListener *listener)
{
    if (xen_enabled()) {
        xen_in_migration = true;
    }
}

static void xen_log_global_stop(MemoryListener *listener)
{
    xen_in_migration = false;
}

static MemoryListener xen_memory_listener = {
    .region_add = xen_region_add,
    .region_del = xen_region_del,
    .log_start = xen_log_start,
    .log_stop = xen_log_stop,
    .log_sync = xen_log_sync,
    .log_global_start = xen_log_global_start,
    .log_global_stop = xen_log_global_stop,
    .priority = 10,
};

/* get the ioreq packets from share mem */
static ioreq_t *cpu_get_ioreq_from_shared_memory(XenIOState *state, int vcpu)
{
    ioreq_t *req = xen_vcpu_ioreq(state->shared_page, vcpu);

    if (req->state != STATE_IOREQ_READY) {
        DPRINTF("I/O request not ready: "
                "%x, ptr: %x, port: %"PRIx64", "
                "data: %"PRIx64", count: %" FMT_ioreq_size ", size: %" FMT_ioreq_size "\n",
                req->state, req->data_is_ptr, req->addr,
                req->data, req->count, req->size);
        return NULL;
    }

    xen_rmb(); /* see IOREQ_READY /then/ read contents of ioreq */

    req->state = STATE_IOREQ_INPROCESS;
    return req;
}

/* use poll to get the port notification */
/* ioreq_vec--out,the */
/* retval--the number of ioreq packet */
static ioreq_t *cpu_get_ioreq(XenIOState *state)
{
    int i;
    evtchn_port_t port;

    port = xc_evtchn_pending(state->xce_handle);
    if (port == state->bufioreq_local_port) {
        timer_mod(state->buffered_io_timer,
                BUFFER_IO_MAX_DELAY + qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
        return NULL;
    }

    if (port != -1) {
        for (i = 0; i < max_cpus; i++) {
            if (state->ioreq_local_port[i] == port) {
                break;
            }
        }

        if (i == max_cpus) {
            hw_error("Fatal error while trying to get io event!\n");
        }

        /* unmask the wanted port again */
        xc_evtchn_unmask(state->xce_handle, port);

        /* get the io packet from shared memory */
        state->send_vcpu = i;
        return cpu_get_ioreq_from_shared_memory(state, i);
    }

    /* read error or read nothing */
    return NULL;
}

static uint32_t do_inp(pio_addr_t addr, unsigned long size)
{
    switch (size) {
        case 1:
            return cpu_inb(addr);
        case 2:
            return cpu_inw(addr);
        case 4:
            return cpu_inl(addr);
        default:
            hw_error("inp: bad size: %04"FMT_pioaddr" %lx", addr, size);
    }
}

static void do_outp(pio_addr_t addr,
        unsigned long size, uint32_t val)
{
    switch (size) {
        case 1:
            return cpu_outb(addr, val);
        case 2:
            return cpu_outw(addr, val);
        case 4:
            return cpu_outl(addr, val);
        default:
            hw_error("outp: bad size: %04"FMT_pioaddr" %lx", addr, size);
    }
}

/*
 * Helper functions which read/write an object from/to physical guest
 * memory, as part of the implementation of an ioreq.
 *
 * Equivalent to
 *   cpu_physical_memory_rw(addr + (req->df ? -1 : +1) * req->size * i,
 *                          val, req->size, 0/1)
 * except without the integer overflow problems.
 */
static void rw_phys_req_item(hwaddr addr,
                             ioreq_t *req, uint32_t i, void *val, int rw)
{
    /* Do everything unsigned so overflow just results in a truncated result
     * and accesses to undesired parts of guest memory, which is up
     * to the guest */
    hwaddr offset = (hwaddr)req->size * i;
    if (req->df) {
        addr -= offset;
    } else {
        addr += offset;
    }
    cpu_physical_memory_rw(addr, val, req->size, rw);
}

static inline void read_phys_req_item(hwaddr addr,
                                      ioreq_t *req, uint32_t i, void *val)
{
    rw_phys_req_item(addr, req, i, val, 0);
}
static inline void write_phys_req_item(hwaddr addr,
                                       ioreq_t *req, uint32_t i, void *val)
{
    rw_phys_req_item(addr, req, i, val, 1);
}


static void cpu_ioreq_pio(ioreq_t *req)
{
    uint32_t i;

    if (req->dir == IOREQ_READ) {
        if (!req->data_is_ptr) {
            req->data = do_inp(req->addr, req->size);
        } else {
            uint32_t tmp;

            for (i = 0; i < req->count; i++) {
                tmp = do_inp(req->addr, req->size);
                write_phys_req_item(req->data, req, i, &tmp);
            }
        }
    } else if (req->dir == IOREQ_WRITE) {
        if (!req->data_is_ptr) {
            do_outp(req->addr, req->size, req->data);
        } else {
            for (i = 0; i < req->count; i++) {
                uint32_t tmp = 0;

                read_phys_req_item(req->data, req, i, &tmp);
                do_outp(req->addr, req->size, tmp);
            }
        }
    }
}

static void cpu_ioreq_move(ioreq_t *req)
{
    uint32_t i;

    if (!req->data_is_ptr) {
        if (req->dir == IOREQ_READ) {
            for (i = 0; i < req->count; i++) {
                read_phys_req_item(req->addr, req, i, &req->data);
            }
        } else if (req->dir == IOREQ_WRITE) {
            for (i = 0; i < req->count; i++) {
                write_phys_req_item(req->addr, req, i, &req->data);
            }
        }
    } else {
        uint64_t tmp;

        if (req->dir == IOREQ_READ) {
            for (i = 0; i < req->count; i++) {
                read_phys_req_item(req->addr, req, i, &tmp);
                write_phys_req_item(req->data, req, i, &tmp);
            }
        } else if (req->dir == IOREQ_WRITE) {
            for (i = 0; i < req->count; i++) {
                read_phys_req_item(req->data, req, i, &tmp);
                write_phys_req_item(req->addr, req, i, &tmp);
            }
        }
    }
}

static void handle_ioreq(ioreq_t *req)
{
    if (!req->data_is_ptr && (req->dir == IOREQ_WRITE) &&
            (req->size < sizeof (target_ulong))) {
        req->data &= ((target_ulong) 1 << (8 * req->size)) - 1;
    }

    switch (req->type) {
        case IOREQ_TYPE_PIO:
            cpu_ioreq_pio(req);
            break;
        case IOREQ_TYPE_COPY:
            cpu_ioreq_move(req);
            break;
        case IOREQ_TYPE_TIMEOFFSET:
            break;
        case IOREQ_TYPE_INVALIDATE:
            xen_invalidate_map_cache();
            break;
        default:
            hw_error("Invalid ioreq type 0x%x\n", req->type);
    }
}

static int handle_buffered_iopage(XenIOState *state)
{
    buf_ioreq_t *buf_req = NULL;
    ioreq_t req;
    int qw;

    if (!state->buffered_io_page) {
        return 0;
    }

    memset(&req, 0x00, sizeof(req));

    while (state->buffered_io_page->read_pointer != state->buffered_io_page->write_pointer) {
        buf_req = &state->buffered_io_page->buf_ioreq[
            state->buffered_io_page->read_pointer % IOREQ_BUFFER_SLOT_NUM];
        req.size = 1UL << buf_req->size;
        req.count = 1;
        req.addr = buf_req->addr;
        req.data = buf_req->data;
        req.state = STATE_IOREQ_READY;
        req.dir = buf_req->dir;
        req.df = 1;
        req.type = buf_req->type;
        req.data_is_ptr = 0;
        qw = (req.size == 8);
        if (qw) {
            buf_req = &state->buffered_io_page->buf_ioreq[
                (state->buffered_io_page->read_pointer + 1) % IOREQ_BUFFER_SLOT_NUM];
            req.data |= ((uint64_t)buf_req->data) << 32;
        }

        handle_ioreq(&req);

        xen_mb();
        state->buffered_io_page->read_pointer += qw ? 2 : 1;
    }

    return req.count;
}

static void handle_buffered_io(void *opaque)
{
    XenIOState *state = opaque;

    if (handle_buffered_iopage(state)) {
        timer_mod(state->buffered_io_timer,
                BUFFER_IO_MAX_DELAY + qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    } else {
        timer_del(state->buffered_io_timer);
        xc_evtchn_unmask(state->xce_handle, state->bufioreq_local_port);
    }
}

static void cpu_handle_ioreq(void *opaque)
{
    XenIOState *state = opaque;
    ioreq_t *req = cpu_get_ioreq(state);

    handle_buffered_iopage(state);
    if (req) {
        handle_ioreq(req);

        if (req->state != STATE_IOREQ_INPROCESS) {
            fprintf(stderr, "Badness in I/O request ... not in service?!: "
                    "%x, ptr: %x, port: %"PRIx64", "
                    "data: %"PRIx64", count: %" FMT_ioreq_size ", size: %" FMT_ioreq_size "\n",
                    req->state, req->data_is_ptr, req->addr,
                    req->data, req->count, req->size);
            destroy_hvm_domain(false);
            return;
        }

        xen_wmb(); /* Update ioreq contents /then/ update state. */

        /*
         * We do this before we send the response so that the tools
         * have the opportunity to pick up on the reset before the
         * guest resumes and does a hlt with interrupts disabled which
         * causes Xen to powerdown the domain.
         */
        if (runstate_is_running()) {
            if (qemu_shutdown_requested_get()) {
                destroy_hvm_domain(false);
            }
            if (qemu_reset_requested_get()) {
                qemu_system_reset(VMRESET_REPORT);
                destroy_hvm_domain(true);
            }
        }

        req->state = STATE_IORESP_READY;
        xc_evtchn_notify(state->xce_handle, state->ioreq_local_port[state->send_vcpu]);
    }
}

static void xen_main_loop_prepare(XenIOState *state)
{
    int evtchn_fd = -1;

    if (state->xce_handle != XC_HANDLER_INITIAL_VALUE) {
        evtchn_fd = xc_evtchn_fd(state->xce_handle);
    }

    state->buffered_io_timer = timer_new_ms(QEMU_CLOCK_REALTIME, handle_buffered_io,
                                                 state);

    if (evtchn_fd != -1) {
        qemu_set_fd_handler(evtchn_fd, cpu_handle_ioreq, NULL, state);
    }
}


static void xen_hvm_change_state_handler(void *opaque, int running,
                                         RunState rstate)
{
    XenIOState *xstate = opaque;
    if (running) {
        xen_main_loop_prepare(xstate);
    }
}

static void xen_exit_notifier(Notifier *n, void *data)
{
    XenIOState *state = container_of(n, XenIOState, exit);

    xc_evtchn_close(state->xce_handle);
    xs_daemon_close(state->xenstore);
}

static void xen_read_physmap(XenIOState *state)
{
    XenPhysmap *physmap = NULL;
    unsigned int len, num, i;
    char path[80], *value = NULL;
    char **entries = NULL;

    snprintf(path, sizeof(path),
            "/local/domain/0/device-model/%d/physmap", xen_domid);
    entries = xs_directory(state->xenstore, 0, path, &num);
    if (entries == NULL)
        return;

    for (i = 0; i < num; i++) {
        physmap = g_malloc(sizeof (XenPhysmap));
        physmap->phys_offset = strtoull(entries[i], NULL, 16);
        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%s/start_addr",
                xen_domid, entries[i]);
        value = xs_read(state->xenstore, 0, path, &len);
        if (value == NULL) {
            g_free(physmap);
            continue;
        }
        physmap->start_addr = strtoull(value, NULL, 16);
        free(value);

        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%s/size",
                xen_domid, entries[i]);
        value = xs_read(state->xenstore, 0, path, &len);
        if (value == NULL) {
            g_free(physmap);
            continue;
        }
        physmap->size = strtoull(value, NULL, 16);
        free(value);

        snprintf(path, sizeof(path),
                "/local/domain/0/device-model/%d/physmap/%s/name",
                xen_domid, entries[i]);
        physmap->name = xs_read(state->xenstore, 0, path, &len);

        QLIST_INSERT_HEAD(&state->physmap, physmap, list);
    }
    free(entries);
}

static void xen_wakeup_notifier(Notifier *notifier, void *data)
{
    xc_set_hvm_param(xen_xc, xen_domid, HVM_PARAM_ACPI_S_STATE, 0);
}

/* return 0 means OK, or -1 means critical issue -- will exit(1) */
int xen_hvm_init(ram_addr_t *below_4g_mem_size, ram_addr_t *above_4g_mem_size,
                 MemoryRegion **ram_memory)
{
    int i, rc;
    unsigned long ioreq_pfn;
    unsigned long bufioreq_evtchn;
    XenIOState *state;

    state = g_malloc0(sizeof (XenIOState));

    state->xce_handle = xen_xc_evtchn_open(NULL, 0);
    if (state->xce_handle == XC_HANDLER_INITIAL_VALUE) {
        perror("xen: event channel open");
        return -1;
    }

    state->xenstore = xs_daemon_open();
    if (state->xenstore == NULL) {
        perror("xen: xenstore open");
        return -1;
    }

    state->exit.notify = xen_exit_notifier;
    qemu_add_exit_notifier(&state->exit);

    state->suspend.notify = xen_suspend_notifier;
    qemu_register_suspend_notifier(&state->suspend);

    state->wakeup.notify = xen_wakeup_notifier;
    qemu_register_wakeup_notifier(&state->wakeup);

    xc_get_hvm_param(xen_xc, xen_domid, HVM_PARAM_IOREQ_PFN, &ioreq_pfn);
    DPRINTF("shared page at pfn %lx\n", ioreq_pfn);
    state->shared_page = xc_map_foreign_range(xen_xc, xen_domid, XC_PAGE_SIZE,
                                              PROT_READ|PROT_WRITE, ioreq_pfn);
    if (state->shared_page == NULL) {
        hw_error("map shared IO page returned error %d handle=" XC_INTERFACE_FMT,
                 errno, xen_xc);
    }

    xc_get_hvm_param(xen_xc, xen_domid, HVM_PARAM_BUFIOREQ_PFN, &ioreq_pfn);
    DPRINTF("buffered io page at pfn %lx\n", ioreq_pfn);
    state->buffered_io_page = xc_map_foreign_range(xen_xc, xen_domid, XC_PAGE_SIZE,
                                                   PROT_READ|PROT_WRITE, ioreq_pfn);
    if (state->buffered_io_page == NULL) {
        hw_error("map buffered IO page returned error %d", errno);
    }

    state->ioreq_local_port = g_malloc0(max_cpus * sizeof (evtchn_port_t));

    /* FIXME: how about if we overflow the page here? */
    for (i = 0; i < max_cpus; i++) {
        rc = xc_evtchn_bind_interdomain(state->xce_handle, xen_domid,
                                        xen_vcpu_eport(state->shared_page, i));
        if (rc == -1) {
            fprintf(stderr, "bind interdomain ioctl error %d\n", errno);
            return -1;
        }
        state->ioreq_local_port[i] = rc;
    }

    rc = xc_get_hvm_param(xen_xc, xen_domid, HVM_PARAM_BUFIOREQ_EVTCHN,
            &bufioreq_evtchn);
    if (rc < 0) {
        fprintf(stderr, "failed to get HVM_PARAM_BUFIOREQ_EVTCHN\n");
        return -1;
    }
    rc = xc_evtchn_bind_interdomain(state->xce_handle, xen_domid,
            (uint32_t)bufioreq_evtchn);
    if (rc == -1) {
        fprintf(stderr, "bind interdomain ioctl error %d\n", errno);
        return -1;
    }
    state->bufioreq_local_port = rc;

    /* Init RAM management */
    xen_map_cache_init(xen_phys_offset_to_gaddr, state);
    xen_ram_init(below_4g_mem_size, above_4g_mem_size, ram_size, ram_memory);

    qemu_add_vm_change_state_handler(xen_hvm_change_state_handler, state);

    state->memory_listener = xen_memory_listener;
    QLIST_INIT(&state->physmap);
    memory_listener_register(&state->memory_listener, &address_space_memory);
    state->log_for_dirtybit = NULL;

    /* Initialize backend core & drivers */
    if (xen_be_init() != 0) {
        fprintf(stderr, "%s: xen backend core setup failed\n", __FUNCTION__);
        return -1;
    }
    xen_be_register("console", &xen_console_ops);
    xen_be_register("vkbd", &xen_kbdmouse_ops);
    xen_be_register("qdisk", &xen_blkdev_ops);
    xen_read_physmap(state);

    return 0;
}

void destroy_hvm_domain(bool reboot)
{
    XenXC xc_handle;
    int sts;

    xc_handle = xen_xc_interface_open(0, 0, 0);
    if (xc_handle == XC_HANDLER_INITIAL_VALUE) {
        fprintf(stderr, "Cannot acquire xenctrl handle\n");
    } else {
        sts = xc_domain_shutdown(xc_handle, xen_domid,
                                 reboot ? SHUTDOWN_reboot : SHUTDOWN_poweroff);
        if (sts != 0) {
            fprintf(stderr, "xc_domain_shutdown failed to issue %s, "
                    "sts %d, %s\n", reboot ? "reboot" : "poweroff",
                    sts, strerror(errno));
        } else {
            fprintf(stderr, "Issued domain %d %s\n", xen_domid,
                    reboot ? "reboot" : "poweroff");
        }
        xc_interface_close(xc_handle);
    }
}

void xen_register_framebuffer(MemoryRegion *mr)
{
    framebuffer = mr;
}

void xen_shutdown_fatal_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "Will destroy the domain.\n");
    /* destroy the domain */
    qemu_system_shutdown_request();
}

void xen_modified_memory(ram_addr_t start, ram_addr_t length)
{
    if (unlikely(xen_in_migration)) {
        int rc;
        ram_addr_t start_pfn, nb_pages;

        if (length == 0) {
            length = TARGET_PAGE_SIZE;
        }
        start_pfn = start >> TARGET_PAGE_BITS;
        nb_pages = ((start + length + TARGET_PAGE_SIZE - 1) >> TARGET_PAGE_BITS)
            - start_pfn;
        rc = xc_hvm_modified_memory(xen_xc, xen_domid, start_pfn, nb_pages);
        if (rc) {
            fprintf(stderr,
                    "%s failed for "RAM_ADDR_FMT" ("RAM_ADDR_FMT"): %i, %s\n",
                    __func__, start, nb_pages, rc, strerror(-rc));
        }
    }
}

void qmp_xen_set_global_dirty_log(bool enable, Error **errp)
{
    if (enable) {
        memory_global_dirty_log_start();
    } else {
        memory_global_dirty_log_stop();
    }
}
