#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/target_page.h"
#include "trace.h"

#include "hw/pci/pci_host.h"
#include "hw/xen/xen-hvm-common.h"
#include "hw/xen/xen-bus.h"
#include "hw/boards.h"
#include "hw/xen/arch_hvm.h"

MemoryRegion xen_memory;

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size, MemoryRegion *mr,
                   Error **errp)
{
    unsigned target_page_bits = qemu_target_page_bits();
    unsigned long nr_pfn;
    xen_pfn_t *pfn_list;
    int i;

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        /* RAM already populated in Xen */
        warn_report("%s: do not alloc "RAM_ADDR_FMT
                " bytes of ram at "RAM_ADDR_FMT" when runstate is INMIGRATE",
                __func__, size, ram_addr);
        return;
    }

    if (mr == &xen_memory) {
        return;
    }

    trace_xen_ram_alloc(ram_addr, size);

    nr_pfn = size >> target_page_bits;
    pfn_list = g_new(xen_pfn_t, nr_pfn);

    for (i = 0; i < nr_pfn; i++) {
        pfn_list[i] = (ram_addr >> target_page_bits) + i;
    }

    if (xc_domain_populate_physmap_exact(xen_xc, xen_domid, nr_pfn, 0, 0, pfn_list)) {
        error_setg(errp, "xen: failed to populate ram at " RAM_ADDR_FMT,
                   ram_addr);
    }

    g_free(pfn_list);
}

static void xen_set_memory(struct MemoryListener *listener,
                           MemoryRegionSection *section,
                           bool add)
{
    XenIOState *state = container_of(listener, XenIOState, memory_listener);

    if (section->mr == &xen_memory) {
        return;
    } else {
        if (add) {
            xen_map_memory_section(xen_domid, state->ioservid,
                                   section);
        } else {
            xen_unmap_memory_section(xen_domid, state->ioservid,
                                     section);
        }
    }

    arch_xen_set_memory(state, section, add);
}

void xen_region_add(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    memory_region_ref(section->mr);
    xen_set_memory(listener, section, true);
}

void xen_region_del(MemoryListener *listener,
                           MemoryRegionSection *section)
{
    xen_set_memory(listener, section, false);
    memory_region_unref(section->mr);
}

void xen_io_add(MemoryListener *listener,
                       MemoryRegionSection *section)
{
    XenIOState *state = container_of(listener, XenIOState, io_listener);
    MemoryRegion *mr = section->mr;

    if (mr->ops == &unassigned_io_ops) {
        return;
    }

    memory_region_ref(mr);

    xen_map_io_section(xen_domid, state->ioservid, section);
}

void xen_io_del(MemoryListener *listener,
                       MemoryRegionSection *section)
{
    XenIOState *state = container_of(listener, XenIOState, io_listener);
    MemoryRegion *mr = section->mr;

    if (mr->ops == &unassigned_io_ops) {
        return;
    }

    xen_unmap_io_section(xen_domid, state->ioservid, section);

    memory_region_unref(mr);
}

void xen_device_realize(DeviceListener *listener,
                               DeviceState *dev)
{
    XenIOState *state = container_of(listener, XenIOState, device_listener);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        PCIDevice *pci_dev = PCI_DEVICE(dev);
        XenPciDevice *xendev = g_new(XenPciDevice, 1);

        xendev->pci_dev = pci_dev;
        xendev->sbdf = PCI_BUILD_BDF(pci_dev_bus_num(pci_dev),
                                     pci_dev->devfn);
        QLIST_INSERT_HEAD(&state->dev_list, xendev, entry);

        xen_map_pcidev(xen_domid, state->ioservid, pci_dev);
    }
}

void xen_device_unrealize(DeviceListener *listener,
                                 DeviceState *dev)
{
    XenIOState *state = container_of(listener, XenIOState, device_listener);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        PCIDevice *pci_dev = PCI_DEVICE(dev);
        XenPciDevice *xendev, *next;

        xen_unmap_pcidev(xen_domid, state->ioservid, pci_dev);

        QLIST_FOREACH_SAFE(xendev, &state->dev_list, entry, next) {
            if (xendev->pci_dev == pci_dev) {
                QLIST_REMOVE(xendev, entry);
                g_free(xendev);
                break;
            }
        }
    }
}

MemoryListener xen_io_listener = {
    .name = "xen-io",
    .region_add = xen_io_add,
    .region_del = xen_io_del,
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
};

DeviceListener xen_device_listener = {
    .realize = xen_device_realize,
    .unrealize = xen_device_unrealize,
};

/* get the ioreq packets from share mem */
static ioreq_t *cpu_get_ioreq_from_shared_memory(XenIOState *state, int vcpu)
{
    ioreq_t *req = xen_vcpu_ioreq(state->shared_page, vcpu);

    if (req->state != STATE_IOREQ_READY) {
        trace_cpu_get_ioreq_from_shared_memory_req_not_ready(req->state,
                                                             req->data_is_ptr,
                                                             req->addr,
                                                             req->data,
                                                             req->count,
                                                             req->size);
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
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int max_cpus = ms->smp.max_cpus;
    int i;
    evtchn_port_t port;

    port = qemu_xen_evtchn_pending(state->xce_handle);
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
        qemu_xen_evtchn_unmask(state->xce_handle, port);

        /* get the io packet from shared memory */
        state->send_vcpu = i;
        return cpu_get_ioreq_from_shared_memory(state, i);
    }

    /* read error or read nothing */
    return NULL;
}

static uint32_t do_inp(uint32_t addr, unsigned long size)
{
    switch (size) {
        case 1:
            return cpu_inb(addr);
        case 2:
            return cpu_inw(addr);
        case 4:
            return cpu_inl(addr);
        default:
            hw_error("inp: bad size: %04x %lx", addr, size);
    }
}

static void do_outp(uint32_t addr,
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
            hw_error("outp: bad size: %04x %lx", addr, size);
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


void cpu_ioreq_pio(ioreq_t *req)
{
    uint32_t i;

    trace_cpu_ioreq_pio(req, req->dir, req->df, req->data_is_ptr, req->addr,
                         req->data, req->count, req->size);

    if (req->size > sizeof(uint32_t)) {
        hw_error("PIO: bad size (%u)", req->size);
    }

    if (req->dir == IOREQ_READ) {
        if (!req->data_is_ptr) {
            req->data = do_inp(req->addr, req->size);
            trace_cpu_ioreq_pio_read_reg(req, req->data, req->addr,
                                         req->size);
        } else {
            uint32_t tmp;

            for (i = 0; i < req->count; i++) {
                tmp = do_inp(req->addr, req->size);
                write_phys_req_item(req->data, req, i, &tmp);
            }
        }
    } else if (req->dir == IOREQ_WRITE) {
        if (!req->data_is_ptr) {
            trace_cpu_ioreq_pio_write_reg(req, req->data, req->addr,
                                          req->size);
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

    trace_cpu_ioreq_move(req, req->dir, req->df, req->data_is_ptr, req->addr,
                         req->data, req->count, req->size);

    if (req->size > sizeof(req->data)) {
        hw_error("MMIO: bad size (%u)", req->size);
    }

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

static void cpu_ioreq_config(XenIOState *state, ioreq_t *req)
{
    uint32_t sbdf = req->addr >> 32;
    uint32_t reg = req->addr;
    XenPciDevice *xendev;

    if (req->size != sizeof(uint8_t) && req->size != sizeof(uint16_t) &&
        req->size != sizeof(uint32_t)) {
        hw_error("PCI config access: bad size (%u)", req->size);
    }

    if (req->count != 1) {
        hw_error("PCI config access: bad count (%u)", req->count);
    }

    QLIST_FOREACH(xendev, &state->dev_list, entry) {
        if (xendev->sbdf != sbdf) {
            continue;
        }

        if (!req->data_is_ptr) {
            if (req->dir == IOREQ_READ) {
                req->data = pci_host_config_read_common(
                    xendev->pci_dev, reg, PCI_CONFIG_SPACE_SIZE,
                    req->size);
                trace_cpu_ioreq_config_read(req, xendev->sbdf, reg,
                                            req->size, req->data);
            } else if (req->dir == IOREQ_WRITE) {
                trace_cpu_ioreq_config_write(req, xendev->sbdf, reg,
                                             req->size, req->data);
                pci_host_config_write_common(
                    xendev->pci_dev, reg, PCI_CONFIG_SPACE_SIZE,
                    req->data, req->size);
            }
        } else {
            uint32_t tmp;

            if (req->dir == IOREQ_READ) {
                tmp = pci_host_config_read_common(
                    xendev->pci_dev, reg, PCI_CONFIG_SPACE_SIZE,
                    req->size);
                trace_cpu_ioreq_config_read(req, xendev->sbdf, reg,
                                            req->size, tmp);
                write_phys_req_item(req->data, req, 0, &tmp);
            } else if (req->dir == IOREQ_WRITE) {
                read_phys_req_item(req->data, req, 0, &tmp);
                trace_cpu_ioreq_config_write(req, xendev->sbdf, reg,
                                             req->size, tmp);
                pci_host_config_write_common(
                    xendev->pci_dev, reg, PCI_CONFIG_SPACE_SIZE,
                    tmp, req->size);
            }
        }
    }
}

static void handle_ioreq(XenIOState *state, ioreq_t *req)
{
    trace_handle_ioreq(req, req->type, req->dir, req->df, req->data_is_ptr,
                       req->addr, req->data, req->count, req->size);

    if (!req->data_is_ptr && (req->dir == IOREQ_WRITE) &&
            (req->size < sizeof (target_ulong))) {
        req->data &= ((target_ulong) 1 << (8 * req->size)) - 1;
    }

    if (req->dir == IOREQ_WRITE)
        trace_handle_ioreq_write(req, req->type, req->df, req->data_is_ptr,
                                 req->addr, req->data, req->count, req->size);

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
        case IOREQ_TYPE_PCI_CONFIG:
            cpu_ioreq_config(state, req);
            break;
        default:
            arch_handle_ioreq(state, req);
    }
    if (req->dir == IOREQ_READ) {
        trace_handle_ioreq_read(req, req->type, req->df, req->data_is_ptr,
                                req->addr, req->data, req->count, req->size);
    }
}

static bool handle_buffered_iopage(XenIOState *state)
{
    buffered_iopage_t *buf_page = state->buffered_io_page;
    buf_ioreq_t *buf_req = NULL;
    bool handled_ioreq = false;
    ioreq_t req;
    int qw;

    if (!buf_page) {
        return 0;
    }

    memset(&req, 0x00, sizeof(req));
    req.state = STATE_IOREQ_READY;
    req.count = 1;
    req.dir = IOREQ_WRITE;

    for (;;) {
        uint32_t rdptr = buf_page->read_pointer, wrptr;

        xen_rmb();
        wrptr = buf_page->write_pointer;
        xen_rmb();
        if (rdptr != buf_page->read_pointer) {
            continue;
        }
        if (rdptr == wrptr) {
            break;
        }
        buf_req = &buf_page->buf_ioreq[rdptr % IOREQ_BUFFER_SLOT_NUM];
        req.size = 1U << buf_req->size;
        req.addr = buf_req->addr;
        req.data = buf_req->data;
        req.type = buf_req->type;
        xen_rmb();
        qw = (req.size == 8);
        if (qw) {
            if (rdptr + 1 == wrptr) {
                hw_error("Incomplete quad word buffered ioreq");
            }
            buf_req = &buf_page->buf_ioreq[(rdptr + 1) %
                                           IOREQ_BUFFER_SLOT_NUM];
            req.data |= ((uint64_t)buf_req->data) << 32;
            xen_rmb();
        }

        handle_ioreq(state, &req);

        /* Only req.data may get updated by handle_ioreq(), albeit even that
         * should not happen as such data would never make it to the guest (we
         * can only usefully see writes here after all).
         */
        assert(req.state == STATE_IOREQ_READY);
        assert(req.count == 1);
        assert(req.dir == IOREQ_WRITE);
        assert(!req.data_is_ptr);

        qatomic_add(&buf_page->read_pointer, qw + 1);
        handled_ioreq = true;
    }

    return handled_ioreq;
}

static void handle_buffered_io(void *opaque)
{
    XenIOState *state = opaque;

    if (handle_buffered_iopage(state)) {
        timer_mod(state->buffered_io_timer,
                BUFFER_IO_MAX_DELAY + qemu_clock_get_ms(QEMU_CLOCK_REALTIME));
    } else {
        timer_del(state->buffered_io_timer);
        qemu_xen_evtchn_unmask(state->xce_handle, state->bufioreq_local_port);
    }
}

static void cpu_handle_ioreq(void *opaque)
{
    XenIOState *state = opaque;
    ioreq_t *req = cpu_get_ioreq(state);

    handle_buffered_iopage(state);
    if (req) {
        ioreq_t copy = *req;

        xen_rmb();
        handle_ioreq(state, &copy);
        req->data = copy.data;

        if (req->state != STATE_IOREQ_INPROCESS) {
            warn_report("Badness in I/O request ... not in service?!: "
                    "%x, ptr: %x, port: %"PRIx64", "
                    "data: %"PRIx64", count: %u, size: %u, type: %u",
                    req->state, req->data_is_ptr, req->addr,
                    req->data, req->count, req->size, req->type);
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
            ShutdownCause request;

            if (qemu_shutdown_requested_get()) {
                destroy_hvm_domain(false);
            }
            request = qemu_reset_requested_get();
            if (request) {
                qemu_system_reset(request);
                destroy_hvm_domain(true);
            }
        }

        req->state = STATE_IORESP_READY;
        qemu_xen_evtchn_notify(state->xce_handle,
                               state->ioreq_local_port[state->send_vcpu]);
    }
}

static void xen_main_loop_prepare(XenIOState *state)
{
    int evtchn_fd = -1;

    if (state->xce_handle != NULL) {
        evtchn_fd = qemu_xen_evtchn_fd(state->xce_handle);
    }

    state->buffered_io_timer = timer_new_ms(QEMU_CLOCK_REALTIME, handle_buffered_io,
                                                 state);

    if (evtchn_fd != -1) {
        CPUState *cpu_state;

        CPU_FOREACH(cpu_state) {
            trace_xen_main_loop_prepare_init_cpu(cpu_state->cpu_index,
                                                 cpu_state);
            state->cpu_by_vcpu_id[cpu_state->cpu_index] = cpu_state;
        }
        qemu_set_fd_handler(evtchn_fd, cpu_handle_ioreq, NULL, state);
    }
}


void xen_hvm_change_state_handler(void *opaque, bool running,
                                         RunState rstate)
{
    XenIOState *state = opaque;

    if (running) {
        xen_main_loop_prepare(state);
    }

    xen_set_ioreq_server_state(xen_domid,
                               state->ioservid,
                               running);
}

void xen_exit_notifier(Notifier *n, void *data)
{
    XenIOState *state = container_of(n, XenIOState, exit);

    xen_destroy_ioreq_server(xen_domid, state->ioservid);
    if (state->fres != NULL) {
        xenforeignmemory_unmap_resource(xen_fmem, state->fres);
    }

    qemu_xen_evtchn_close(state->xce_handle);
    xs_daemon_close(state->xenstore);
}

static int xen_map_ioreq_server(XenIOState *state)
{
    void *addr = NULL;
    xen_pfn_t ioreq_pfn;
    xen_pfn_t bufioreq_pfn;
    evtchn_port_t bufioreq_evtchn;
    int rc;

    /*
     * Attempt to map using the resource API and fall back to normal
     * foreign mapping if this is not supported.
     */
    QEMU_BUILD_BUG_ON(XENMEM_resource_ioreq_server_frame_bufioreq != 0);
    QEMU_BUILD_BUG_ON(XENMEM_resource_ioreq_server_frame_ioreq(0) != 1);
    state->fres = xenforeignmemory_map_resource(xen_fmem, xen_domid,
                                         XENMEM_resource_ioreq_server,
                                         state->ioservid, 0, 2,
                                         &addr,
                                         PROT_READ | PROT_WRITE, 0);
    if (state->fres != NULL) {
        trace_xen_map_resource_ioreq(state->ioservid, addr);
        state->buffered_io_page = addr;
        state->shared_page = addr + XC_PAGE_SIZE;
    } else if (errno != EOPNOTSUPP) {
        error_report("failed to map ioreq server resources: error %d handle=%p",
                     errno, xen_xc);
        return -1;
    }

    rc = xen_get_ioreq_server_info(xen_domid, state->ioservid,
                                   (state->shared_page == NULL) ?
                                   &ioreq_pfn : NULL,
                                   (state->buffered_io_page == NULL) ?
                                   &bufioreq_pfn : NULL,
                                   &bufioreq_evtchn);
    if (rc < 0) {
        error_report("failed to get ioreq server info: error %d handle=%p",
                     errno, xen_xc);
        return rc;
    }

    if (state->shared_page == NULL) {
        trace_xen_map_ioreq_server_shared_page(ioreq_pfn);

        state->shared_page = xenforeignmemory_map(xen_fmem, xen_domid,
                                                  PROT_READ | PROT_WRITE,
                                                  1, &ioreq_pfn, NULL);
        if (state->shared_page == NULL) {
            error_report("map shared IO page returned error %d handle=%p",
                         errno, xen_xc);
        }
    }

    if (state->buffered_io_page == NULL) {
        trace_xen_map_ioreq_server_buffered_io_page(bufioreq_pfn);

        state->buffered_io_page = xenforeignmemory_map(xen_fmem, xen_domid,
                                                       PROT_READ | PROT_WRITE,
                                                       1, &bufioreq_pfn,
                                                       NULL);
        if (state->buffered_io_page == NULL) {
            error_report("map buffered IO page returned error %d", errno);
            return -1;
        }
    }

    if (state->shared_page == NULL || state->buffered_io_page == NULL) {
        return -1;
    }

    trace_xen_map_ioreq_server_buffered_io_evtchn(bufioreq_evtchn);

    state->bufioreq_remote_port = bufioreq_evtchn;

    return 0;
}

void destroy_hvm_domain(bool reboot)
{
    xc_interface *xc_handle;
    int sts;
    int rc;

    unsigned int reason = reboot ? SHUTDOWN_reboot : SHUTDOWN_poweroff;

    if (xen_dmod) {
        rc = xendevicemodel_shutdown(xen_dmod, xen_domid, reason);
        if (!rc) {
            return;
        }
        if (errno != ENOTTY /* old Xen */) {
            error_report("xendevicemodel_shutdown failed with error %d", errno);
        }
        /* well, try the old thing then */
    }

    xc_handle = xc_interface_open(0, 0, 0);
    if (xc_handle == NULL) {
        trace_destroy_hvm_domain_cannot_acquire_handle();
    } else {
        sts = xc_domain_shutdown(xc_handle, xen_domid, reason);
        if (sts != 0) {
            trace_destroy_hvm_domain_failed_action(
                reboot ? "reboot" : "poweroff", sts, strerror(errno)
            );
        } else {
            trace_destroy_hvm_domain_action(
                xen_domid, reboot ? "reboot" : "poweroff"
            );
        }
        xc_interface_close(xc_handle);
    }
}

void xen_shutdown_fatal_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vreport(fmt, ap);
    va_end(ap);
    error_report("Will destroy the domain.");
    /* destroy the domain */
    qemu_system_shutdown_request(SHUTDOWN_CAUSE_HOST_ERROR);
}

static void xen_do_ioreq_register(XenIOState *state,
                                  unsigned int max_cpus,
                                  const MemoryListener *xen_memory_listener)
{
    int i, rc;

    state->exit.notify = xen_exit_notifier;
    qemu_add_exit_notifier(&state->exit);

    /*
     * Register wake-up support in QMP query-current-machine API
     */
    qemu_register_wakeup_support();

    rc = xen_map_ioreq_server(state);
    if (rc < 0) {
        goto err;
    }

    /* Note: cpus is empty at this point in init */
    state->cpu_by_vcpu_id = g_new0(CPUState *, max_cpus);

    rc = xen_set_ioreq_server_state(xen_domid, state->ioservid, true);
    if (rc < 0) {
        error_report("failed to enable ioreq server info: error %d handle=%p",
                     errno, xen_xc);
        goto err;
    }

    state->ioreq_local_port = g_new0(evtchn_port_t, max_cpus);

    /* FIXME: how about if we overflow the page here? */
    for (i = 0; i < max_cpus; i++) {
        rc = qemu_xen_evtchn_bind_interdomain(state->xce_handle, xen_domid,
                                              xen_vcpu_eport(state->shared_page,
                                                             i));
        if (rc == -1) {
            error_report("shared evtchn %d bind error %d", i, errno);
            goto err;
        }
        state->ioreq_local_port[i] = rc;
    }

    rc = qemu_xen_evtchn_bind_interdomain(state->xce_handle, xen_domid,
                                          state->bufioreq_remote_port);
    if (rc == -1) {
        error_report("buffered evtchn bind error %d", errno);
        goto err;
    }
    state->bufioreq_local_port = rc;

    /* Init RAM management */
#ifdef XEN_COMPAT_PHYSMAP
    xen_map_cache_init(xen_phys_offset_to_gaddr, state);
#else
    xen_map_cache_init(NULL, state);
#endif

    qemu_add_vm_change_state_handler(xen_hvm_change_state_handler, state);

    state->memory_listener = *xen_memory_listener;
    memory_listener_register(&state->memory_listener, &address_space_memory);

    state->io_listener = xen_io_listener;
    memory_listener_register(&state->io_listener, &address_space_io);

    state->device_listener = xen_device_listener;
    QLIST_INIT(&state->dev_list);
    device_listener_register(&state->device_listener);

    return;

err:
    error_report("xen hardware virtual machine initialisation failed");
    exit(1);
}

void xen_register_ioreq(XenIOState *state, unsigned int max_cpus,
                        const MemoryListener *xen_memory_listener)
{
    int rc;

    setup_xen_backend_ops();

    state->xce_handle = qemu_xen_evtchn_open();
    if (state->xce_handle == NULL) {
        error_report("xen: event channel open failed with error %d", errno);
        goto err;
    }

    state->xenstore = xs_daemon_open();
    if (state->xenstore == NULL) {
        error_report("xen: xenstore open failed with error %d", errno);
        goto err;
    }

    rc = xen_create_ioreq_server(xen_domid, &state->ioservid);
    if (!rc) {
        xen_do_ioreq_register(state, max_cpus, xen_memory_listener);
    } else {
        warn_report("xen: failed to create ioreq server");
    }

    xen_bus_init();

    xen_be_init();

    return;

err:
    error_report("xen hardware virtual machine backend registration failed");
    exit(1);
}
