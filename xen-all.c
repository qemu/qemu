/*
 * Copyright (C) 2010       Citrix Ltd.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include <sys/mman.h>

#include "hw/pci.h"
#include "hw/xen_common.h"
#include "hw/xen_backend.h"

#include "xen-mapcache.h"
#include "trace.h"

#include <xen/hvm/ioreq.h>
#include <xen/hvm/params.h>

//#define DEBUG_XEN

#ifdef DEBUG_XEN
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, "xen: " fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

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

#define BUFFER_IO_MAX_DELAY  100

typedef struct XenIOState {
    shared_iopage_t *shared_page;
    buffered_iopage_t *buffered_io_page;
    QEMUTimer *buffered_io_timer;
    /* the evtchn port for polling the notification, */
    evtchn_port_t *ioreq_local_port;
    /* the evtchn fd for polling */
    XenEvtchn xce_handle;
    /* which vcpu we are serving */
    int send_vcpu;

    Notifier exit;
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

static void xen_ram_init(ram_addr_t ram_size)
{
    RAMBlock *new_block;
    ram_addr_t below_4g_mem_size, above_4g_mem_size = 0;

    new_block = qemu_mallocz(sizeof (*new_block));
    pstrcpy(new_block->idstr, sizeof (new_block->idstr), "xen.ram");
    new_block->host = NULL;
    new_block->offset = 0;
    new_block->length = ram_size;

    QLIST_INSERT_HEAD(&ram_list.blocks, new_block, next);

    ram_list.phys_dirty = qemu_realloc(ram_list.phys_dirty,
                                       new_block->length >> TARGET_PAGE_BITS);
    memset(ram_list.phys_dirty + (new_block->offset >> TARGET_PAGE_BITS),
           0xff, new_block->length >> TARGET_PAGE_BITS);

    if (ram_size >= 0xe0000000 ) {
        above_4g_mem_size = ram_size - 0xe0000000;
        below_4g_mem_size = 0xe0000000;
    } else {
        below_4g_mem_size = ram_size;
    }

    cpu_register_physical_memory(0, below_4g_mem_size, new_block->offset);
#if TARGET_PHYS_ADDR_BITS > 32
    if (above_4g_mem_size > 0) {
        cpu_register_physical_memory(0x100000000ULL, above_4g_mem_size,
                                     new_block->offset + below_4g_mem_size);
    }
#endif
}

void xen_ram_alloc(ram_addr_t ram_addr, ram_addr_t size)
{
    unsigned long nr_pfn;
    xen_pfn_t *pfn_list;
    int i;

    trace_xen_ram_alloc(ram_addr, size);

    nr_pfn = size >> TARGET_PAGE_BITS;
    pfn_list = qemu_malloc(sizeof (*pfn_list) * nr_pfn);

    for (i = 0; i < nr_pfn; i++) {
        pfn_list[i] = (ram_addr >> TARGET_PAGE_BITS) + i;
    }

    if (xc_domain_populate_physmap_exact(xen_xc, xen_domid, nr_pfn, 0, 0, pfn_list)) {
        hw_error("xen: failed to populate ram at %lx", ram_addr);
    }

    qemu_free(pfn_list);
}


/* VCPU Operations, MMIO, IO ring ... */

static void xen_reset_vcpu(void *opaque)
{
    CPUState *env = opaque;

    env->halted = 1;
}

void xen_vcpu_init(void)
{
    CPUState *first_cpu;

    if ((first_cpu = qemu_get_cpu(0))) {
        qemu_register_reset(xen_reset_vcpu, first_cpu);
        xen_reset_vcpu(first_cpu);
    }
}

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
    if (port != -1) {
        for (i = 0; i < smp_cpus; i++) {
            if (state->ioreq_local_port[i] == port) {
                break;
            }
        }

        if (i == smp_cpus) {
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

static void cpu_ioreq_pio(ioreq_t *req)
{
    int i, sign;

    sign = req->df ? -1 : 1;

    if (req->dir == IOREQ_READ) {
        if (!req->data_is_ptr) {
            req->data = do_inp(req->addr, req->size);
        } else {
            uint32_t tmp;

            for (i = 0; i < req->count; i++) {
                tmp = do_inp(req->addr, req->size);
                cpu_physical_memory_write(req->data + (sign * i * req->size),
                        (uint8_t *) &tmp, req->size);
            }
        }
    } else if (req->dir == IOREQ_WRITE) {
        if (!req->data_is_ptr) {
            do_outp(req->addr, req->size, req->data);
        } else {
            for (i = 0; i < req->count; i++) {
                uint32_t tmp = 0;

                cpu_physical_memory_read(req->data + (sign * i * req->size),
                        (uint8_t*) &tmp, req->size);
                do_outp(req->addr, req->size, tmp);
            }
        }
    }
}

static void cpu_ioreq_move(ioreq_t *req)
{
    int i, sign;

    sign = req->df ? -1 : 1;

    if (!req->data_is_ptr) {
        if (req->dir == IOREQ_READ) {
            for (i = 0; i < req->count; i++) {
                cpu_physical_memory_read(req->addr + (sign * i * req->size),
                        (uint8_t *) &req->data, req->size);
            }
        } else if (req->dir == IOREQ_WRITE) {
            for (i = 0; i < req->count; i++) {
                cpu_physical_memory_write(req->addr + (sign * i * req->size),
                        (uint8_t *) &req->data, req->size);
            }
        }
    } else {
        target_ulong tmp;

        if (req->dir == IOREQ_READ) {
            for (i = 0; i < req->count; i++) {
                cpu_physical_memory_read(req->addr + (sign * i * req->size),
                        (uint8_t*) &tmp, req->size);
                cpu_physical_memory_write(req->data + (sign * i * req->size),
                        (uint8_t*) &tmp, req->size);
            }
        } else if (req->dir == IOREQ_WRITE) {
            for (i = 0; i < req->count; i++) {
                cpu_physical_memory_read(req->data + (sign * i * req->size),
                        (uint8_t*) &tmp, req->size);
                cpu_physical_memory_write(req->addr + (sign * i * req->size),
                        (uint8_t*) &tmp, req->size);
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
            qemu_invalidate_map_cache();
            break;
        default:
            hw_error("Invalid ioreq type 0x%x\n", req->type);
    }
}

static void handle_buffered_iopage(XenIOState *state)
{
    buf_ioreq_t *buf_req = NULL;
    ioreq_t req;
    int qw;

    if (!state->buffered_io_page) {
        return;
    }

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
}

static void handle_buffered_io(void *opaque)
{
    XenIOState *state = opaque;

    handle_buffered_iopage(state);
    qemu_mod_timer(state->buffered_io_timer,
                   BUFFER_IO_MAX_DELAY + qemu_get_clock_ms(rt_clock));
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
            destroy_hvm_domain();
            return;
        }

        xen_wmb(); /* Update ioreq contents /then/ update state. */

        /*
         * We do this before we send the response so that the tools
         * have the opportunity to pick up on the reset before the
         * guest resumes and does a hlt with interrupts disabled which
         * causes Xen to powerdown the domain.
         */
        if (vm_running) {
            if (qemu_shutdown_requested_get()) {
                destroy_hvm_domain();
            }
            if (qemu_reset_requested_get()) {
                qemu_system_reset();
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

    state->buffered_io_timer = qemu_new_timer_ms(rt_clock, handle_buffered_io,
                                                 state);
    qemu_mod_timer(state->buffered_io_timer, qemu_get_clock_ms(rt_clock));

    if (evtchn_fd != -1) {
        qemu_set_fd_handler(evtchn_fd, cpu_handle_ioreq, NULL, state);
    }
}


/* Initialise Xen */

static void xen_vm_change_state_handler(void *opaque, int running, int reason)
{
    XenIOState *state = opaque;
    if (running) {
        xen_main_loop_prepare(state);
    }
}

static void xen_exit_notifier(Notifier *n)
{
    XenIOState *state = container_of(n, XenIOState, exit);

    xc_evtchn_close(state->xce_handle);
}

int xen_init(void)
{
    xen_xc = xen_xc_interface_open(0, 0, 0);
    if (xen_xc == XC_HANDLER_INITIAL_VALUE) {
        xen_be_printf(NULL, 0, "can't open xen interface\n");
        return -1;
    }

    return 0;
}

int xen_hvm_init(void)
{
    int i, rc;
    unsigned long ioreq_pfn;
    XenIOState *state;

    state = qemu_mallocz(sizeof (XenIOState));

    state->xce_handle = xen_xc_evtchn_open(NULL, 0);
    if (state->xce_handle == XC_HANDLER_INITIAL_VALUE) {
        perror("xen: event channel open");
        return -errno;
    }

    state->exit.notify = xen_exit_notifier;
    qemu_add_exit_notifier(&state->exit);

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

    state->ioreq_local_port = qemu_mallocz(smp_cpus * sizeof (evtchn_port_t));

    /* FIXME: how about if we overflow the page here? */
    for (i = 0; i < smp_cpus; i++) {
        rc = xc_evtchn_bind_interdomain(state->xce_handle, xen_domid,
                                        xen_vcpu_eport(state->shared_page, i));
        if (rc == -1) {
            fprintf(stderr, "bind interdomain ioctl error %d\n", errno);
            return -1;
        }
        state->ioreq_local_port[i] = rc;
    }

    /* Init RAM management */
    qemu_map_cache_init();
    xen_ram_init(ram_size);

    qemu_add_vm_change_state_handler(xen_vm_change_state_handler, state);

    return 0;
}

void destroy_hvm_domain(void)
{
    XenXC xc_handle;
    int sts;

    xc_handle = xen_xc_interface_open(0, 0, 0);
    if (xc_handle == XC_HANDLER_INITIAL_VALUE) {
        fprintf(stderr, "Cannot acquire xenctrl handle\n");
    } else {
        sts = xc_domain_shutdown(xc_handle, xen_domid, SHUTDOWN_poweroff);
        if (sts != 0) {
            fprintf(stderr, "? xc_domain_shutdown failed to issue poweroff, "
                    "sts %d, %s\n", sts, strerror(errno));
        } else {
            fprintf(stderr, "Issued domain %d poweroff\n", xen_domid);
        }
        xc_interface_close(xc_handle);
    }
}
