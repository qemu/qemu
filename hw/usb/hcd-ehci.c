/*
 * QEMU USB EHCI Emulation
 *
 * Copyright(c) 2008  Emutex Ltd. (address@hidden)
 * Copyright(c) 2011-2012 Red Hat, Inc.
 *
 * Red Hat Authors:
 * Gerd Hoffmann <kraxel@redhat.com>
 * Hans de Goede <hdegoede@redhat.com>
 *
 * EHCI project was started by Mark Burkley, with contributions by
 * Niels de Vos.  David S. Ahern continued working on it.  Kevin Wolf,
 * Jan Kiszka and Vincent Palatin contributed bugfixes.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/usb/ehci-regs.h"
#include "hw/usb/hcd-ehci.h"
#include "migration/vmstate.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "sysemu/runstate.h"

#define FRAME_TIMER_FREQ 1000
#define FRAME_TIMER_NS   (NANOSECONDS_PER_SECOND / FRAME_TIMER_FREQ)
#define UFRAME_TIMER_NS  (FRAME_TIMER_NS / 8)

#define NB_MAXINTRATE    8        // Max rate at which controller issues ints
#define BUFF_SIZE        5*4096   // Max bytes to transfer per transaction
#define MAX_QH           100      // Max allowable queue heads in a chain
#define MIN_UFR_PER_TICK 24       /* Min frames to process when catching up */
#define PERIODIC_ACTIVE  512      /* Micro-frames */

/*  Internal periodic / asynchronous schedule state machine states
 */
typedef enum {
    EST_INACTIVE = 1000,
    EST_ACTIVE,
    EST_EXECUTING,
    EST_SLEEPING,
    /*  The following states are internal to the state machine function
    */
    EST_WAITLISTHEAD,
    EST_FETCHENTRY,
    EST_FETCHQH,
    EST_FETCHITD,
    EST_FETCHSITD,
    EST_ADVANCEQUEUE,
    EST_FETCHQTD,
    EST_EXECUTE,
    EST_WRITEBACK,
    EST_HORIZONTALQH
} EHCI_STATES;

/* macros for accessing fields within next link pointer entry */
#define NLPTR_GET(x)             ((x) & 0xffffffe0)
#define NLPTR_TYPE_GET(x)        (((x) >> 1) & 3)
#define NLPTR_TBIT(x)            ((x) & 1)  // 1=invalid, 0=valid

/* link pointer types */
#define NLPTR_TYPE_ITD           0     // isoc xfer descriptor
#define NLPTR_TYPE_QH            1     // queue head
#define NLPTR_TYPE_STITD         2     // split xaction, isoc xfer descriptor
#define NLPTR_TYPE_FSTN          3     // frame span traversal node

#define SET_LAST_RUN_CLOCK(s) \
    (s)->last_run_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

/* nifty macros from Arnon's EHCI version  */
#define get_field(data, field) \
    (((data) & field##_MASK) >> field##_SH)

#define set_field(data, newval, field) do { \
    uint32_t val = *data; \
    val &= ~ field##_MASK; \
    val |= ((newval) << field##_SH) & field##_MASK; \
    *data = val; \
    } while(0)

static const char *ehci_state_names[] = {
    [EST_INACTIVE]     = "INACTIVE",
    [EST_ACTIVE]       = "ACTIVE",
    [EST_EXECUTING]    = "EXECUTING",
    [EST_SLEEPING]     = "SLEEPING",
    [EST_WAITLISTHEAD] = "WAITLISTHEAD",
    [EST_FETCHENTRY]   = "FETCH ENTRY",
    [EST_FETCHQH]      = "FETCH QH",
    [EST_FETCHITD]     = "FETCH ITD",
    [EST_ADVANCEQUEUE] = "ADVANCEQUEUE",
    [EST_FETCHQTD]     = "FETCH QTD",
    [EST_EXECUTE]      = "EXECUTE",
    [EST_WRITEBACK]    = "WRITEBACK",
    [EST_HORIZONTALQH] = "HORIZONTALQH",
};

static const char *ehci_mmio_names[] = {
    [USBCMD]            = "USBCMD",
    [USBSTS]            = "USBSTS",
    [USBINTR]           = "USBINTR",
    [FRINDEX]           = "FRINDEX",
    [PERIODICLISTBASE]  = "P-LIST BASE",
    [ASYNCLISTADDR]     = "A-LIST ADDR",
    [CONFIGFLAG]        = "CONFIGFLAG",
};

static int ehci_state_executing(EHCIQueue *q);
static int ehci_state_writeback(EHCIQueue *q);
static int ehci_state_advqueue(EHCIQueue *q);
static int ehci_fill_queue(EHCIPacket *p);
static void ehci_free_packet(EHCIPacket *p);

static const char *nr2str(const char **n, size_t len, uint32_t nr)
{
    if (nr < len && n[nr] != NULL) {
        return n[nr];
    } else {
        return "unknown";
    }
}

static const char *state2str(uint32_t state)
{
    return nr2str(ehci_state_names, ARRAY_SIZE(ehci_state_names), state);
}

static const char *addr2str(hwaddr addr)
{
    return nr2str(ehci_mmio_names, ARRAY_SIZE(ehci_mmio_names), addr);
}

static void ehci_trace_usbsts(uint32_t mask, int state)
{
    /* interrupts */
    if (mask & USBSTS_INT) {
        trace_usb_ehci_usbsts("INT", state);
    }
    if (mask & USBSTS_ERRINT) {
        trace_usb_ehci_usbsts("ERRINT", state);
    }
    if (mask & USBSTS_PCD) {
        trace_usb_ehci_usbsts("PCD", state);
    }
    if (mask & USBSTS_FLR) {
        trace_usb_ehci_usbsts("FLR", state);
    }
    if (mask & USBSTS_HSE) {
        trace_usb_ehci_usbsts("HSE", state);
    }
    if (mask & USBSTS_IAA) {
        trace_usb_ehci_usbsts("IAA", state);
    }

    /* status */
    if (mask & USBSTS_HALT) {
        trace_usb_ehci_usbsts("HALT", state);
    }
    if (mask & USBSTS_REC) {
        trace_usb_ehci_usbsts("REC", state);
    }
    if (mask & USBSTS_PSS) {
        trace_usb_ehci_usbsts("PSS", state);
    }
    if (mask & USBSTS_ASS) {
        trace_usb_ehci_usbsts("ASS", state);
    }
}

static inline void ehci_set_usbsts(EHCIState *s, int mask)
{
    if ((s->usbsts & mask) == mask) {
        return;
    }
    ehci_trace_usbsts(mask, 1);
    s->usbsts |= mask;
}

static inline void ehci_clear_usbsts(EHCIState *s, int mask)
{
    if ((s->usbsts & mask) == 0) {
        return;
    }
    ehci_trace_usbsts(mask, 0);
    s->usbsts &= ~mask;
}

/* update irq line */
static inline void ehci_update_irq(EHCIState *s)
{
    int level = 0;

    if ((s->usbsts & USBINTR_MASK) & s->usbintr) {
        level = 1;
    }

    trace_usb_ehci_irq(level, s->frindex, s->usbsts, s->usbintr);
    qemu_set_irq(s->irq, level);
}

/* flag interrupt condition */
static inline void ehci_raise_irq(EHCIState *s, int intr)
{
    if (intr & (USBSTS_PCD | USBSTS_FLR | USBSTS_HSE)) {
        s->usbsts |= intr;
        ehci_update_irq(s);
    } else {
        s->usbsts_pending |= intr;
    }
}

/*
 * Commit pending interrupts (added via ehci_raise_irq),
 * at the rate allowed by "Interrupt Threshold Control".
 */
static inline void ehci_commit_irq(EHCIState *s)
{
    uint32_t itc;

    if (!s->usbsts_pending) {
        return;
    }
    if (s->usbsts_frindex > s->frindex) {
        return;
    }

    itc = (s->usbcmd >> 16) & 0xff;
    s->usbsts |= s->usbsts_pending;
    s->usbsts_pending = 0;
    s->usbsts_frindex = s->frindex + itc;
    ehci_update_irq(s);
}

static void ehci_update_halt(EHCIState *s)
{
    if (s->usbcmd & USBCMD_RUNSTOP) {
        ehci_clear_usbsts(s, USBSTS_HALT);
    } else {
        if (s->astate == EST_INACTIVE && s->pstate == EST_INACTIVE) {
            ehci_set_usbsts(s, USBSTS_HALT);
        }
    }
}

static void ehci_set_state(EHCIState *s, int async, int state)
{
    if (async) {
        trace_usb_ehci_state("async", state2str(state));
        s->astate = state;
        if (s->astate == EST_INACTIVE) {
            ehci_clear_usbsts(s, USBSTS_ASS);
            ehci_update_halt(s);
        } else {
            ehci_set_usbsts(s, USBSTS_ASS);
        }
    } else {
        trace_usb_ehci_state("periodic", state2str(state));
        s->pstate = state;
        if (s->pstate == EST_INACTIVE) {
            ehci_clear_usbsts(s, USBSTS_PSS);
            ehci_update_halt(s);
        } else {
            ehci_set_usbsts(s, USBSTS_PSS);
        }
    }
}

static int ehci_get_state(EHCIState *s, int async)
{
    return async ? s->astate : s->pstate;
}

static void ehci_set_fetch_addr(EHCIState *s, int async, uint32_t addr)
{
    if (async) {
        s->a_fetch_addr = addr;
    } else {
        s->p_fetch_addr = addr;
    }
}

static int ehci_get_fetch_addr(EHCIState *s, int async)
{
    return async ? s->a_fetch_addr : s->p_fetch_addr;
}

static void ehci_trace_qh(EHCIQueue *q, hwaddr addr, EHCIqh *qh)
{
    /* need three here due to argument count limits */
    trace_usb_ehci_qh_ptrs(q, addr, qh->next,
                           qh->current_qtd, qh->next_qtd, qh->altnext_qtd);
    trace_usb_ehci_qh_fields(addr,
                             get_field(qh->epchar, QH_EPCHAR_RL),
                             get_field(qh->epchar, QH_EPCHAR_MPLEN),
                             get_field(qh->epchar, QH_EPCHAR_EPS),
                             get_field(qh->epchar, QH_EPCHAR_EP),
                             get_field(qh->epchar, QH_EPCHAR_DEVADDR));
    trace_usb_ehci_qh_bits(addr,
                           (bool)(qh->epchar & QH_EPCHAR_C),
                           (bool)(qh->epchar & QH_EPCHAR_H),
                           (bool)(qh->epchar & QH_EPCHAR_DTC),
                           (bool)(qh->epchar & QH_EPCHAR_I));
}

static void ehci_trace_qtd(EHCIQueue *q, hwaddr addr, EHCIqtd *qtd)
{
    /* need three here due to argument count limits */
    trace_usb_ehci_qtd_ptrs(q, addr, qtd->next, qtd->altnext);
    trace_usb_ehci_qtd_fields(addr,
                              get_field(qtd->token, QTD_TOKEN_TBYTES),
                              get_field(qtd->token, QTD_TOKEN_CPAGE),
                              get_field(qtd->token, QTD_TOKEN_CERR),
                              get_field(qtd->token, QTD_TOKEN_PID));
    trace_usb_ehci_qtd_bits(addr,
                            (bool)(qtd->token & QTD_TOKEN_IOC),
                            (bool)(qtd->token & QTD_TOKEN_ACTIVE),
                            (bool)(qtd->token & QTD_TOKEN_HALT),
                            (bool)(qtd->token & QTD_TOKEN_BABBLE),
                            (bool)(qtd->token & QTD_TOKEN_XACTERR));
}

static void ehci_trace_itd(EHCIState *s, hwaddr addr, EHCIitd *itd)
{
    trace_usb_ehci_itd(addr, itd->next,
                       get_field(itd->bufptr[1], ITD_BUFPTR_MAXPKT),
                       get_field(itd->bufptr[2], ITD_BUFPTR_MULT),
                       get_field(itd->bufptr[0], ITD_BUFPTR_EP),
                       get_field(itd->bufptr[0], ITD_BUFPTR_DEVADDR));
}

static void ehci_trace_sitd(EHCIState *s, hwaddr addr,
                            EHCIsitd *sitd)
{
    trace_usb_ehci_sitd(addr, sitd->next,
                        (bool)(sitd->results & SITD_RESULTS_ACTIVE));
}

static void ehci_trace_guest_bug(EHCIState *s, const char *message)
{
    trace_usb_ehci_guest_bug(message);
}

static inline bool ehci_enabled(EHCIState *s)
{
    return s->usbcmd & USBCMD_RUNSTOP;
}

static inline bool ehci_async_enabled(EHCIState *s)
{
    return ehci_enabled(s) && (s->usbcmd & USBCMD_ASE);
}

static inline bool ehci_periodic_enabled(EHCIState *s)
{
    return ehci_enabled(s) && (s->usbcmd & USBCMD_PSE);
}

/* Get an array of dwords from main memory */
static inline int get_dwords(EHCIState *ehci, uint32_t addr,
                             uint32_t *buf, int num)
{
    int i;

    if (!ehci->as) {
        ehci_raise_irq(ehci, USBSTS_HSE);
        ehci->usbcmd &= ~USBCMD_RUNSTOP;
        trace_usb_ehci_dma_error();
        return -1;
    }

    for (i = 0; i < num; i++, buf++, addr += sizeof(*buf)) {
        dma_memory_read(ehci->as, addr, buf, sizeof(*buf),
                        MEMTXATTRS_UNSPECIFIED);
        *buf = le32_to_cpu(*buf);
    }

    return num;
}

/* Put an array of dwords in to main memory */
static inline int put_dwords(EHCIState *ehci, uint32_t addr,
                             uint32_t *buf, int num)
{
    int i;

    if (!ehci->as) {
        ehci_raise_irq(ehci, USBSTS_HSE);
        ehci->usbcmd &= ~USBCMD_RUNSTOP;
        trace_usb_ehci_dma_error();
        return -1;
    }

    for (i = 0; i < num; i++, buf++, addr += sizeof(*buf)) {
        uint32_t tmp = cpu_to_le32(*buf);
        dma_memory_write(ehci->as, addr, &tmp, sizeof(tmp),
                         MEMTXATTRS_UNSPECIFIED);
    }

    return num;
}

static int ehci_get_pid(EHCIqtd *qtd)
{
    switch (get_field(qtd->token, QTD_TOKEN_PID)) {
    case 0:
        return USB_TOKEN_OUT;
    case 1:
        return USB_TOKEN_IN;
    case 2:
        return USB_TOKEN_SETUP;
    default:
        fprintf(stderr, "bad token\n");
        return 0;
    }
}

static bool ehci_verify_qh(EHCIQueue *q, EHCIqh *qh)
{
    uint32_t devaddr = get_field(qh->epchar, QH_EPCHAR_DEVADDR);
    uint32_t endp    = get_field(qh->epchar, QH_EPCHAR_EP);
    if ((devaddr != get_field(q->qh.epchar, QH_EPCHAR_DEVADDR)) ||
        (endp    != get_field(q->qh.epchar, QH_EPCHAR_EP)) ||
        (qh->current_qtd != q->qh.current_qtd) ||
        (q->async && qh->next_qtd != q->qh.next_qtd) ||
        (memcmp(&qh->altnext_qtd, &q->qh.altnext_qtd,
                                 7 * sizeof(uint32_t)) != 0) ||
        (q->dev != NULL && q->dev->addr != devaddr)) {
        return false;
    } else {
        return true;
    }
}

static bool ehci_verify_qtd(EHCIPacket *p, EHCIqtd *qtd)
{
    if (p->qtdaddr != p->queue->qtdaddr ||
        (p->queue->async && !NLPTR_TBIT(p->qtd.next) &&
            (p->qtd.next != qtd->next)) ||
        (!NLPTR_TBIT(p->qtd.altnext) && (p->qtd.altnext != qtd->altnext)) ||
        p->qtd.token != qtd->token ||
        p->qtd.bufptr[0] != qtd->bufptr[0]) {
        return false;
    } else {
        return true;
    }
}

static bool ehci_verify_pid(EHCIQueue *q, EHCIqtd *qtd)
{
    int ep  = get_field(q->qh.epchar, QH_EPCHAR_EP);
    int pid = ehci_get_pid(qtd);

    /* Note the pid changing is normal for ep 0 (the control ep) */
    if (q->last_pid && ep != 0 && pid != q->last_pid) {
        return false;
    } else {
        return true;
    }
}

/* Finish executing and writeback a packet outside of the regular
   fetchqh -> fetchqtd -> execute -> writeback cycle */
static void ehci_writeback_async_complete_packet(EHCIPacket *p)
{
    EHCIQueue *q = p->queue;
    EHCIqtd qtd;
    EHCIqh qh;
    int state;

    /* Verify the qh + qtd, like we do when going through fetchqh & fetchqtd */
    get_dwords(q->ehci, NLPTR_GET(q->qhaddr),
               (uint32_t *) &qh, sizeof(EHCIqh) >> 2);
    get_dwords(q->ehci, NLPTR_GET(q->qtdaddr),
               (uint32_t *) &qtd, sizeof(EHCIqtd) >> 2);
    if (!ehci_verify_qh(q, &qh) || !ehci_verify_qtd(p, &qtd)) {
        p->async = EHCI_ASYNC_INITIALIZED;
        ehci_free_packet(p);
        return;
    }

    state = ehci_get_state(q->ehci, q->async);
    ehci_state_executing(q);
    ehci_state_writeback(q); /* Frees the packet! */
    if (!(q->qh.token & QTD_TOKEN_HALT)) {
        ehci_state_advqueue(q);
    }
    ehci_set_state(q->ehci, q->async, state);
}

/* packet management */

static EHCIPacket *ehci_alloc_packet(EHCIQueue *q)
{
    EHCIPacket *p;

    p = g_new0(EHCIPacket, 1);
    p->queue = q;
    usb_packet_init(&p->packet);
    QTAILQ_INSERT_TAIL(&q->packets, p, next);
    trace_usb_ehci_packet_action(p->queue, p, "alloc");
    return p;
}

static void ehci_free_packet(EHCIPacket *p)
{
    if (p->async == EHCI_ASYNC_FINISHED &&
            !(p->queue->qh.token & QTD_TOKEN_HALT)) {
        ehci_writeback_async_complete_packet(p);
        return;
    }
    trace_usb_ehci_packet_action(p->queue, p, "free");
    if (p->async == EHCI_ASYNC_INFLIGHT) {
        usb_cancel_packet(&p->packet);
    }
    if (p->async == EHCI_ASYNC_FINISHED &&
            p->packet.status == USB_RET_SUCCESS) {
        fprintf(stderr,
                "EHCI: Dropping completed packet from halted %s ep %02X\n",
                (p->pid == USB_TOKEN_IN) ? "in" : "out",
                get_field(p->queue->qh.epchar, QH_EPCHAR_EP));
    }
    if (p->async != EHCI_ASYNC_NONE) {
        usb_packet_unmap(&p->packet, &p->sgl);
        qemu_sglist_destroy(&p->sgl);
    }
    QTAILQ_REMOVE(&p->queue->packets, p, next);
    usb_packet_cleanup(&p->packet);
    g_free(p);
}

/* queue management */

static EHCIQueue *ehci_alloc_queue(EHCIState *ehci, uint32_t addr, int async)
{
    EHCIQueueHead *head = async ? &ehci->aqueues : &ehci->pqueues;
    EHCIQueue *q;

    q = g_malloc0(sizeof(*q));
    q->ehci = ehci;
    q->qhaddr = addr;
    q->async = async;
    QTAILQ_INIT(&q->packets);
    QTAILQ_INSERT_HEAD(head, q, next);
    trace_usb_ehci_queue_action(q, "alloc");
    return q;
}

static void ehci_queue_stopped(EHCIQueue *q)
{
    int endp  = get_field(q->qh.epchar, QH_EPCHAR_EP);

    if (!q->last_pid || !q->dev) {
        return;
    }

    usb_device_ep_stopped(q->dev, usb_ep_get(q->dev, q->last_pid, endp));
}

static int ehci_cancel_queue(EHCIQueue *q)
{
    EHCIPacket *p;
    int packets = 0;

    p = QTAILQ_FIRST(&q->packets);
    if (p == NULL) {
        goto leave;
    }

    trace_usb_ehci_queue_action(q, "cancel");
    do {
        ehci_free_packet(p);
        packets++;
    } while ((p = QTAILQ_FIRST(&q->packets)) != NULL);

leave:
    ehci_queue_stopped(q);
    return packets;
}

static int ehci_reset_queue(EHCIQueue *q)
{
    int packets;

    trace_usb_ehci_queue_action(q, "reset");
    packets = ehci_cancel_queue(q);
    q->dev = NULL;
    q->qtdaddr = 0;
    q->last_pid = 0;
    return packets;
}

static void ehci_free_queue(EHCIQueue *q, const char *warn)
{
    EHCIQueueHead *head = q->async ? &q->ehci->aqueues : &q->ehci->pqueues;
    int cancelled;

    trace_usb_ehci_queue_action(q, "free");
    cancelled = ehci_cancel_queue(q);
    if (warn && cancelled > 0) {
        ehci_trace_guest_bug(q->ehci, warn);
    }
    QTAILQ_REMOVE(head, q, next);
    g_free(q);
}

static EHCIQueue *ehci_find_queue_by_qh(EHCIState *ehci, uint32_t addr,
                                        int async)
{
    EHCIQueueHead *head = async ? &ehci->aqueues : &ehci->pqueues;
    EHCIQueue *q;

    QTAILQ_FOREACH(q, head, next) {
        if (addr == q->qhaddr) {
            return q;
        }
    }
    return NULL;
}

static void ehci_queues_rip_unused(EHCIState *ehci, int async)
{
    EHCIQueueHead *head = async ? &ehci->aqueues : &ehci->pqueues;
    const char *warn = async ? "guest unlinked busy QH" : NULL;
    uint64_t maxage = FRAME_TIMER_NS * ehci->maxframes * 4;
    EHCIQueue *q, *tmp;

    QTAILQ_FOREACH_SAFE(q, head, next, tmp) {
        if (q->seen) {
            q->seen = 0;
            q->ts = ehci->last_run_ns;
            continue;
        }
        if (ehci->last_run_ns < q->ts + maxage) {
            continue;
        }
        ehci_free_queue(q, warn);
    }
}

static void ehci_queues_rip_unseen(EHCIState *ehci, int async)
{
    EHCIQueueHead *head = async ? &ehci->aqueues : &ehci->pqueues;
    EHCIQueue *q, *tmp;

    QTAILQ_FOREACH_SAFE(q, head, next, tmp) {
        if (!q->seen) {
            ehci_free_queue(q, NULL);
        }
    }
}

static void ehci_queues_rip_device(EHCIState *ehci, USBDevice *dev, int async)
{
    EHCIQueueHead *head = async ? &ehci->aqueues : &ehci->pqueues;
    EHCIQueue *q, *tmp;

    QTAILQ_FOREACH_SAFE(q, head, next, tmp) {
        if (q->dev != dev) {
            continue;
        }
        ehci_free_queue(q, NULL);
    }
}

static void ehci_queues_rip_all(EHCIState *ehci, int async)
{
    EHCIQueueHead *head = async ? &ehci->aqueues : &ehci->pqueues;
    const char *warn = async ? "guest stopped busy async schedule" : NULL;
    EHCIQueue *q, *tmp;

    QTAILQ_FOREACH_SAFE(q, head, next, tmp) {
        ehci_free_queue(q, warn);
    }
}

/* Attach or detach a device on root hub */

static void ehci_attach(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];
    const char *owner = (*portsc & PORTSC_POWNER) ? "comp" : "ehci";

    trace_usb_ehci_port_attach(port->index, owner, port->dev->product_desc);

    if (*portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->dev = port->dev;
        companion->ops->attach(companion);
        return;
    }

    *portsc |= PORTSC_CONNECT;
    *portsc |= PORTSC_CSC;

    ehci_raise_irq(s, USBSTS_PCD);
}

static void ehci_detach(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];
    const char *owner = (*portsc & PORTSC_POWNER) ? "comp" : "ehci";

    trace_usb_ehci_port_detach(port->index, owner);

    if (*portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->ops->detach(companion);
        companion->dev = NULL;
        /*
         * EHCI spec 4.2.2: "When a disconnect occurs... On the event,
         * the port ownership is returned immediately to the EHCI controller."
         */
        *portsc &= ~PORTSC_POWNER;
        return;
    }

    ehci_queues_rip_device(s, port->dev, 0);
    ehci_queues_rip_device(s, port->dev, 1);

    *portsc &= ~(PORTSC_CONNECT|PORTSC_PED|PORTSC_SUSPEND);
    *portsc |= PORTSC_CSC;

    ehci_raise_irq(s, USBSTS_PCD);
}

static void ehci_child_detach(USBPort *port, USBDevice *child)
{
    EHCIState *s = port->opaque;
    uint32_t portsc = s->portsc[port->index];

    if (portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->ops->child_detach(companion, child);
        return;
    }

    ehci_queues_rip_device(s, child, 0);
    ehci_queues_rip_device(s, child, 1);
}

static void ehci_wakeup(USBPort *port)
{
    EHCIState *s = port->opaque;
    uint32_t *portsc = &s->portsc[port->index];

    if (*portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        if (companion->ops->wakeup) {
            companion->ops->wakeup(companion);
        }
        return;
    }

    if (*portsc & PORTSC_SUSPEND) {
        trace_usb_ehci_port_wakeup(port->index);
        *portsc |= PORTSC_FPRES;
        ehci_raise_irq(s, USBSTS_PCD);
    }

    qemu_bh_schedule(s->async_bh);
}

static void ehci_register_companion(USBBus *bus, USBPort *ports[],
                                    uint32_t portcount, uint32_t firstport,
                                    Error **errp)
{
    EHCIState *s = container_of(bus, EHCIState, bus);
    uint32_t i;

    if (firstport + portcount > NB_PORTS) {
        error_setg(errp, "firstport must be between 0 and %u",
                   NB_PORTS - portcount);
        return;
    }

    for (i = 0; i < portcount; i++) {
        if (s->companion_ports[firstport + i]) {
            error_setg(errp, "firstport %u asks for ports %u-%u,"
                       " but port %u has a companion assigned already",
                       firstport, firstport, firstport + portcount - 1,
                       firstport + i);
            return;
        }
    }

    for (i = 0; i < portcount; i++) {
        s->companion_ports[firstport + i] = ports[i];
        s->ports[firstport + i].speedmask |=
            USB_SPEED_MASK_LOW | USB_SPEED_MASK_FULL;
        /* Ensure devs attached before the initial reset go to the companion */
        s->portsc[firstport + i] = PORTSC_POWNER;
    }

    s->companion_count++;
    s->caps[0x05] = (s->companion_count << 4) | portcount;
}

static void ehci_wakeup_endpoint(USBBus *bus, USBEndpoint *ep,
                                 unsigned int stream)
{
    EHCIState *s = container_of(bus, EHCIState, bus);
    uint32_t portsc = s->portsc[ep->dev->port->index];

    if (portsc & PORTSC_POWNER) {
        return;
    }

    s->periodic_sched_active = PERIODIC_ACTIVE;
    qemu_bh_schedule(s->async_bh);
}

static USBDevice *ehci_find_device(EHCIState *ehci, uint8_t addr)
{
    USBDevice *dev;
    USBPort *port;
    int i;

    for (i = 0; i < NB_PORTS; i++) {
        port = &ehci->ports[i];
        if (!(ehci->portsc[i] & PORTSC_PED)) {
            DPRINTF("Port %d not enabled\n", i);
            continue;
        }
        dev = usb_find_device(port, addr);
        if (dev != NULL) {
            return dev;
        }
    }
    return NULL;
}

/* 4.1 host controller initialization */
void ehci_reset(void *opaque)
{
    EHCIState *s = opaque;
    int i;
    USBDevice *devs[NB_PORTS];

    trace_usb_ehci_reset();

    /*
     * Do the detach before touching portsc, so that it correctly gets send to
     * us or to our companion based on PORTSC_POWNER before the reset.
     */
    for(i = 0; i < NB_PORTS; i++) {
        devs[i] = s->ports[i].dev;
        if (devs[i] && devs[i]->attached) {
            usb_detach(&s->ports[i]);
        }
    }

    memset(&s->opreg, 0x00, sizeof(s->opreg));
    memset(&s->portsc, 0x00, sizeof(s->portsc));

    s->usbcmd = NB_MAXINTRATE << USBCMD_ITC_SH;
    s->usbsts = USBSTS_HALT;
    s->usbsts_pending = 0;
    s->usbsts_frindex = 0;
    ehci_update_irq(s);

    s->astate = EST_INACTIVE;
    s->pstate = EST_INACTIVE;

    for(i = 0; i < NB_PORTS; i++) {
        if (s->companion_ports[i]) {
            s->portsc[i] = PORTSC_POWNER | PORTSC_PPOWER;
        } else {
            s->portsc[i] = PORTSC_PPOWER;
        }
        if (devs[i] && devs[i]->attached) {
            usb_attach(&s->ports[i]);
            usb_device_reset(devs[i]);
        }
    }
    ehci_queues_rip_all(s, 0);
    ehci_queues_rip_all(s, 1);
    timer_del(s->frame_timer);
    qemu_bh_cancel(s->async_bh);
}

static uint64_t ehci_caps_read(void *ptr, hwaddr addr,
                               unsigned size)
{
    EHCIState *s = ptr;
    return s->caps[addr];
}

static void ehci_caps_write(void *ptr, hwaddr addr,
                             uint64_t val, unsigned size)
{
}

static uint64_t ehci_opreg_read(void *ptr, hwaddr addr,
                                unsigned size)
{
    EHCIState *s = ptr;
    uint32_t val;

    switch (addr) {
    case FRINDEX:
        /* Round down to mult of 8, else it can go backwards on migration */
        val = s->frindex & ~7;
        break;
    default:
        val = s->opreg[addr >> 2];
    }

    trace_usb_ehci_opreg_read(addr + s->opregbase, addr2str(addr), val);
    return val;
}

static uint64_t ehci_port_read(void *ptr, hwaddr addr,
                               unsigned size)
{
    EHCIState *s = ptr;
    uint32_t val;

    val = s->portsc[addr >> 2];
    trace_usb_ehci_portsc_read(addr + s->portscbase, addr >> 2, val);
    return val;
}

static void handle_port_owner_write(EHCIState *s, int port, uint32_t owner)
{
    USBDevice *dev = s->ports[port].dev;
    uint32_t *portsc = &s->portsc[port];
    uint32_t orig;

    if (s->companion_ports[port] == NULL)
        return;

    owner = owner & PORTSC_POWNER;
    orig  = *portsc & PORTSC_POWNER;

    if (!(owner ^ orig)) {
        return;
    }

    if (dev && dev->attached) {
        usb_detach(&s->ports[port]);
    }

    *portsc &= ~PORTSC_POWNER;
    *portsc |= owner;

    if (dev && dev->attached) {
        usb_attach(&s->ports[port]);
    }
}

static void ehci_port_write(void *ptr, hwaddr addr,
                            uint64_t val, unsigned size)
{
    EHCIState *s = ptr;
    int port = addr >> 2;
    uint32_t *portsc = &s->portsc[port];
    uint32_t old = *portsc;
    USBDevice *dev = s->ports[port].dev;

    trace_usb_ehci_portsc_write(addr + s->portscbase, addr >> 2, val);

    /* Clear rwc bits */
    *portsc &= ~(val & PORTSC_RWC_MASK);
    /* The guest may clear, but not set the PED bit */
    *portsc &= val | ~PORTSC_PED;
    /* POWNER is masked out by RO_MASK as it is RO when we've no companion */
    handle_port_owner_write(s, port, val);
    /* And finally apply RO_MASK */
    val &= PORTSC_RO_MASK;

    if ((val & PORTSC_PRESET) && !(*portsc & PORTSC_PRESET)) {
        trace_usb_ehci_port_reset(port, 1);
    }

    if (!(val & PORTSC_PRESET) &&(*portsc & PORTSC_PRESET)) {
        trace_usb_ehci_port_reset(port, 0);
        if (dev && dev->attached) {
            usb_port_reset(&s->ports[port]);
            *portsc &= ~PORTSC_CSC;
        }

        /*
         *  Table 2.16 Set the enable bit(and enable bit change) to indicate
         *  to SW that this port has a high speed device attached
         */
        if (dev && dev->attached && (dev->speedmask & USB_SPEED_MASK_HIGH)) {
            val |= PORTSC_PED;
        }
    }

    if ((val & PORTSC_SUSPEND) && !(*portsc & PORTSC_SUSPEND)) {
        trace_usb_ehci_port_suspend(port);
    }
    if (!(val & PORTSC_FPRES) && (*portsc & PORTSC_FPRES)) {
        trace_usb_ehci_port_resume(port);
        val &= ~PORTSC_SUSPEND;
    }

    *portsc &= ~PORTSC_RO_MASK;
    *portsc |= val;
    trace_usb_ehci_portsc_change(addr + s->portscbase, addr >> 2, *portsc, old);
}

static void ehci_opreg_write(void *ptr, hwaddr addr,
                             uint64_t val, unsigned size)
{
    EHCIState *s = ptr;
    uint32_t *mmio = s->opreg + (addr >> 2);
    uint32_t old = *mmio;
    int i;

    trace_usb_ehci_opreg_write(addr + s->opregbase, addr2str(addr), val);

    switch (addr) {
    case USBCMD:
        if (val & USBCMD_HCRESET) {
            ehci_reset(s);
            val = s->usbcmd;
            break;
        }

        /* not supporting dynamic frame list size at the moment */
        if ((val & USBCMD_FLS) && !(s->usbcmd & USBCMD_FLS)) {
            fprintf(stderr, "attempt to set frame list size -- value %d\n",
                    (int)val & USBCMD_FLS);
            val &= ~USBCMD_FLS;
        }

        if (val & USBCMD_IAAD) {
            /*
             * Process IAAD immediately, otherwise the Linux IAAD watchdog may
             * trigger and re-use a qh without us seeing the unlink.
             */
            s->async_stepdown = 0;
            qemu_bh_schedule(s->async_bh);
            trace_usb_ehci_doorbell_ring();
        }

        if (((USBCMD_RUNSTOP | USBCMD_PSE | USBCMD_ASE) & val) !=
            ((USBCMD_RUNSTOP | USBCMD_PSE | USBCMD_ASE) & s->usbcmd)) {
            if (s->pstate == EST_INACTIVE) {
                SET_LAST_RUN_CLOCK(s);
            }
            s->usbcmd = val; /* Set usbcmd for ehci_update_halt() */
            ehci_update_halt(s);
            s->async_stepdown = 0;
            qemu_bh_schedule(s->async_bh);
        }
        break;

    case USBSTS:
        val &= USBSTS_RO_MASK;              // bits 6 through 31 are RO
        ehci_clear_usbsts(s, val);          // bits 0 through 5 are R/WC
        val = s->usbsts;
        ehci_update_irq(s);
        break;

    case USBINTR:
        val &= USBINTR_MASK;
        if (ehci_enabled(s) && (USBSTS_FLR & val)) {
            qemu_bh_schedule(s->async_bh);
        }
        break;

    case FRINDEX:
        val &= 0x00003fff; /* frindex is 14bits */
        s->usbsts_frindex = val;
        break;

    case CONFIGFLAG:
        val &= 0x1;
        if (val) {
            for(i = 0; i < NB_PORTS; i++)
                handle_port_owner_write(s, i, 0);
        }
        break;

    case PERIODICLISTBASE:
        if (ehci_periodic_enabled(s)) {
            fprintf(stderr,
              "ehci: PERIODIC list base register set while periodic schedule\n"
              "      is enabled and HC is enabled\n");
        }
        break;

    case ASYNCLISTADDR:
        if (ehci_async_enabled(s)) {
            fprintf(stderr,
              "ehci: ASYNC list address register set while async schedule\n"
              "      is enabled and HC is enabled\n");
        }
        break;
    }

    *mmio = val;
    trace_usb_ehci_opreg_change(addr + s->opregbase, addr2str(addr),
                                *mmio, old);
}

/*
 *  Write the qh back to guest physical memory.  This step isn't
 *  in the EHCI spec but we need to do it since we don't share
 *  physical memory with our guest VM.
 *
 *  The first three dwords are read-only for the EHCI, so skip them
 *  when writing back the qh.
 */
static void ehci_flush_qh(EHCIQueue *q)
{
    uint32_t *qh = (uint32_t *) &q->qh;
    uint32_t dwords = sizeof(EHCIqh) >> 2;
    uint32_t addr = NLPTR_GET(q->qhaddr);

    put_dwords(q->ehci, addr + 3 * sizeof(uint32_t), qh + 3, dwords - 3);
}

// 4.10.2

static int ehci_qh_do_overlay(EHCIQueue *q)
{
    EHCIPacket *p = QTAILQ_FIRST(&q->packets);
    int i;
    int dtoggle;
    int ping;
    int eps;
    int reload;

    assert(p != NULL);
    assert(p->qtdaddr == q->qtdaddr);

    // remember values in fields to preserve in qh after overlay

    dtoggle = q->qh.token & QTD_TOKEN_DTOGGLE;
    ping    = q->qh.token & QTD_TOKEN_PING;

    q->qh.current_qtd = p->qtdaddr;
    q->qh.next_qtd    = p->qtd.next;
    q->qh.altnext_qtd = p->qtd.altnext;
    q->qh.token       = p->qtd.token;


    eps = get_field(q->qh.epchar, QH_EPCHAR_EPS);
    if (eps == EHCI_QH_EPS_HIGH) {
        q->qh.token &= ~QTD_TOKEN_PING;
        q->qh.token |= ping;
    }

    reload = get_field(q->qh.epchar, QH_EPCHAR_RL);
    set_field(&q->qh.altnext_qtd, reload, QH_ALTNEXT_NAKCNT);

    for (i = 0; i < 5; i++) {
        q->qh.bufptr[i] = p->qtd.bufptr[i];
    }

    if (!(q->qh.epchar & QH_EPCHAR_DTC)) {
        // preserve QH DT bit
        q->qh.token &= ~QTD_TOKEN_DTOGGLE;
        q->qh.token |= dtoggle;
    }

    q->qh.bufptr[1] &= ~BUFPTR_CPROGMASK_MASK;
    q->qh.bufptr[2] &= ~BUFPTR_FRAMETAG_MASK;

    ehci_flush_qh(q);

    return 0;
}

static int ehci_init_transfer(EHCIPacket *p)
{
    uint32_t cpage, offset, bytes, plen;
    dma_addr_t page;

    cpage  = get_field(p->qtd.token, QTD_TOKEN_CPAGE);
    bytes  = get_field(p->qtd.token, QTD_TOKEN_TBYTES);
    offset = p->qtd.bufptr[0] & ~QTD_BUFPTR_MASK;
    qemu_sglist_init(&p->sgl, p->queue->ehci->device, 5, p->queue->ehci->as);

    while (bytes > 0) {
        if (cpage > 4) {
            fprintf(stderr, "cpage out of range (%u)\n", cpage);
            qemu_sglist_destroy(&p->sgl);
            return -1;
        }

        page  = p->qtd.bufptr[cpage] & QTD_BUFPTR_MASK;
        page += offset;
        plen  = bytes;
        if (plen > 4096 - offset) {
            plen = 4096 - offset;
            offset = 0;
            cpage++;
        }

        qemu_sglist_add(&p->sgl, page, plen);
        bytes -= plen;
    }
    return 0;
}

static void ehci_finish_transfer(EHCIQueue *q, int len)
{
    uint32_t cpage, offset;

    if (len > 0) {
        /* update cpage & offset */
        cpage  = get_field(q->qh.token, QTD_TOKEN_CPAGE);
        offset = q->qh.bufptr[0] & ~QTD_BUFPTR_MASK;

        offset += len;
        cpage  += offset >> QTD_BUFPTR_SH;
        offset &= ~QTD_BUFPTR_MASK;

        set_field(&q->qh.token, cpage, QTD_TOKEN_CPAGE);
        q->qh.bufptr[0] &= QTD_BUFPTR_MASK;
        q->qh.bufptr[0] |= offset;
    }
}

static void ehci_async_complete_packet(USBPort *port, USBPacket *packet)
{
    EHCIPacket *p;
    EHCIState *s = port->opaque;
    uint32_t portsc = s->portsc[port->index];

    if (portsc & PORTSC_POWNER) {
        USBPort *companion = s->companion_ports[port->index];
        companion->ops->complete(companion, packet);
        return;
    }

    p = container_of(packet, EHCIPacket, packet);
    assert(p->async == EHCI_ASYNC_INFLIGHT);

    if (packet->status == USB_RET_REMOVE_FROM_QUEUE) {
        trace_usb_ehci_packet_action(p->queue, p, "remove");
        ehci_free_packet(p);
        return;
    }

    trace_usb_ehci_packet_action(p->queue, p, "wakeup");
    p->async = EHCI_ASYNC_FINISHED;

    if (!p->queue->async) {
        s->periodic_sched_active = PERIODIC_ACTIVE;
    }
    qemu_bh_schedule(s->async_bh);
}

static void ehci_execute_complete(EHCIQueue *q)
{
    EHCIPacket *p = QTAILQ_FIRST(&q->packets);
    uint32_t tbytes;

    assert(p != NULL);
    assert(p->qtdaddr == q->qtdaddr);
    assert(p->async == EHCI_ASYNC_INITIALIZED ||
           p->async == EHCI_ASYNC_FINISHED);

    DPRINTF("execute_complete: qhaddr 0x%x, next 0x%x, qtdaddr 0x%x, "
            "status %d, actual_length %d\n",
            q->qhaddr, q->qh.next, q->qtdaddr,
            p->packet.status, p->packet.actual_length);

    switch (p->packet.status) {
    case USB_RET_SUCCESS:
        break;
    case USB_RET_IOERROR:
    case USB_RET_NODEV:
        q->qh.token |= (QTD_TOKEN_HALT | QTD_TOKEN_XACTERR);
        set_field(&q->qh.token, 0, QTD_TOKEN_CERR);
        ehci_raise_irq(q->ehci, USBSTS_ERRINT);
        break;
    case USB_RET_STALL:
        q->qh.token |= QTD_TOKEN_HALT;
        ehci_raise_irq(q->ehci, USBSTS_ERRINT);
        break;
    case USB_RET_NAK:
        set_field(&q->qh.altnext_qtd, 0, QH_ALTNEXT_NAKCNT);
        return; /* We're not done yet with this transaction */
    case USB_RET_BABBLE:
        q->qh.token |= (QTD_TOKEN_HALT | QTD_TOKEN_BABBLE);
        ehci_raise_irq(q->ehci, USBSTS_ERRINT);
        break;
    default:
        /* should not be triggerable */
        fprintf(stderr, "USB invalid response %d\n", p->packet.status);
        g_assert_not_reached();
    }

    /* TODO check 4.12 for splits */
    tbytes = get_field(q->qh.token, QTD_TOKEN_TBYTES);
    if (tbytes && p->pid == USB_TOKEN_IN) {
        tbytes -= p->packet.actual_length;
        if (tbytes) {
            /* 4.15.1.2 must raise int on a short input packet */
            ehci_raise_irq(q->ehci, USBSTS_INT);
            if (q->async) {
                q->ehci->int_req_by_async = true;
            }
        }
    } else {
        tbytes = 0;
    }
    DPRINTF("updating tbytes to %d\n", tbytes);
    set_field(&q->qh.token, tbytes, QTD_TOKEN_TBYTES);

    ehci_finish_transfer(q, p->packet.actual_length);
    usb_packet_unmap(&p->packet, &p->sgl);
    qemu_sglist_destroy(&p->sgl);
    p->async = EHCI_ASYNC_NONE;

    q->qh.token ^= QTD_TOKEN_DTOGGLE;
    q->qh.token &= ~QTD_TOKEN_ACTIVE;

    if (q->qh.token & QTD_TOKEN_IOC) {
        ehci_raise_irq(q->ehci, USBSTS_INT);
        if (q->async) {
            q->ehci->int_req_by_async = true;
        }
    }
}

/* 4.10.3 returns "again" */
static int ehci_execute(EHCIPacket *p, const char *action)
{
    USBEndpoint *ep;
    int endp;
    bool spd;

    assert(p->async == EHCI_ASYNC_NONE ||
           p->async == EHCI_ASYNC_INITIALIZED);

    if (!(p->qtd.token & QTD_TOKEN_ACTIVE)) {
        fprintf(stderr, "Attempting to execute inactive qtd\n");
        return -1;
    }

    if (get_field(p->qtd.token, QTD_TOKEN_TBYTES) > BUFF_SIZE) {
        ehci_trace_guest_bug(p->queue->ehci,
                             "guest requested more bytes than allowed");
        return -1;
    }

    if (!ehci_verify_pid(p->queue, &p->qtd)) {
        ehci_queue_stopped(p->queue); /* Mark the ep in the prev dir stopped */
    }
    p->pid = ehci_get_pid(&p->qtd);
    p->queue->last_pid = p->pid;
    endp = get_field(p->queue->qh.epchar, QH_EPCHAR_EP);
    ep = usb_ep_get(p->queue->dev, p->pid, endp);

    if (p->async == EHCI_ASYNC_NONE) {
        if (ehci_init_transfer(p) != 0) {
            return -1;
        }

        spd = (p->pid == USB_TOKEN_IN && NLPTR_TBIT(p->qtd.altnext) == 0);
        usb_packet_setup(&p->packet, p->pid, ep, 0, p->qtdaddr, spd,
                         (p->qtd.token & QTD_TOKEN_IOC) != 0);
        if (usb_packet_map(&p->packet, &p->sgl)) {
            qemu_sglist_destroy(&p->sgl);
            return -1;
        }
        p->async = EHCI_ASYNC_INITIALIZED;
    }

    trace_usb_ehci_packet_action(p->queue, p, action);
    usb_handle_packet(p->queue->dev, &p->packet);
    DPRINTF("submit: qh 0x%x next 0x%x qtd 0x%x pid 0x%x len %zd endp 0x%x "
            "status %d actual_length %d\n", p->queue->qhaddr, p->qtd.next,
            p->qtdaddr, p->pid, p->packet.iov.size, endp, p->packet.status,
            p->packet.actual_length);

    if (p->packet.actual_length > BUFF_SIZE) {
        fprintf(stderr, "ret from usb_handle_packet > BUFF_SIZE\n");
        return -1;
    }

    return 1;
}

/*  4.7.2
 */

static int ehci_process_itd(EHCIState *ehci,
                            EHCIitd *itd,
                            uint32_t addr)
{
    USBDevice *dev;
    USBEndpoint *ep;
    uint32_t i, len, pid, dir, devaddr, endp;
    uint32_t pg, off, ptr1, ptr2, max, mult;

    ehci->periodic_sched_active = PERIODIC_ACTIVE;

    dir =(itd->bufptr[1] & ITD_BUFPTR_DIRECTION);
    devaddr = get_field(itd->bufptr[0], ITD_BUFPTR_DEVADDR);
    endp = get_field(itd->bufptr[0], ITD_BUFPTR_EP);
    max = get_field(itd->bufptr[1], ITD_BUFPTR_MAXPKT);
    mult = get_field(itd->bufptr[2], ITD_BUFPTR_MULT);

    for(i = 0; i < 8; i++) {
        if (itd->transact[i] & ITD_XACT_ACTIVE) {
            pg   = get_field(itd->transact[i], ITD_XACT_PGSEL);
            off  = itd->transact[i] & ITD_XACT_OFFSET_MASK;
            len  = get_field(itd->transact[i], ITD_XACT_LENGTH);

            if (len > max * mult) {
                len = max * mult;
            }
            if (len > BUFF_SIZE || pg > 6) {
                return -1;
            }

            ptr1 = (itd->bufptr[pg] & ITD_BUFPTR_MASK);
            qemu_sglist_init(&ehci->isgl, ehci->device, 2, ehci->as);
            if (off + len > 4096) {
                /* transfer crosses page border */
                if (pg == 6) {
                    qemu_sglist_destroy(&ehci->isgl);
                    return -1;  /* avoid page pg + 1 */
                }
                ptr2 = (itd->bufptr[pg + 1] & ITD_BUFPTR_MASK);
                uint32_t len2 = off + len - 4096;
                uint32_t len1 = len - len2;
                qemu_sglist_add(&ehci->isgl, ptr1 + off, len1);
                qemu_sglist_add(&ehci->isgl, ptr2, len2);
            } else {
                qemu_sglist_add(&ehci->isgl, ptr1 + off, len);
            }

            dev = ehci_find_device(ehci, devaddr);
            if (dev == NULL) {
                ehci_trace_guest_bug(ehci, "no device found");
                ehci->ipacket.status = USB_RET_NODEV;
                ehci->ipacket.actual_length = 0;
            } else {
                pid = dir ? USB_TOKEN_IN : USB_TOKEN_OUT;
                ep = usb_ep_get(dev, pid, endp);
                if (ep && ep->type == USB_ENDPOINT_XFER_ISOC) {
                    usb_packet_setup(&ehci->ipacket, pid, ep, 0, addr, false,
                                     (itd->transact[i] & ITD_XACT_IOC) != 0);
                    if (usb_packet_map(&ehci->ipacket, &ehci->isgl)) {
                        qemu_sglist_destroy(&ehci->isgl);
                        return -1;
                    }
                    usb_handle_packet(dev, &ehci->ipacket);
                    usb_packet_unmap(&ehci->ipacket, &ehci->isgl);
                } else {
                    DPRINTF("ISOCH: attempt to addess non-iso endpoint\n");
                    ehci->ipacket.status = USB_RET_NAK;
                    ehci->ipacket.actual_length = 0;
                }
            }
            qemu_sglist_destroy(&ehci->isgl);

            switch (ehci->ipacket.status) {
            case USB_RET_SUCCESS:
                break;
            default:
                fprintf(stderr, "Unexpected iso usb result: %d\n",
                        ehci->ipacket.status);
                /* Fall through */
            case USB_RET_IOERROR:
            case USB_RET_NODEV:
                /* 3.3.2: XACTERR is only allowed on IN transactions */
                if (dir) {
                    itd->transact[i] |= ITD_XACT_XACTERR;
                    ehci_raise_irq(ehci, USBSTS_ERRINT);
                }
                break;
            case USB_RET_BABBLE:
                itd->transact[i] |= ITD_XACT_BABBLE;
                ehci_raise_irq(ehci, USBSTS_ERRINT);
                break;
            case USB_RET_NAK:
                /* no data for us, so do a zero-length transfer */
                ehci->ipacket.actual_length = 0;
                break;
            }
            if (!dir) {
                set_field(&itd->transact[i], len - ehci->ipacket.actual_length,
                          ITD_XACT_LENGTH); /* OUT */
            } else {
                set_field(&itd->transact[i], ehci->ipacket.actual_length,
                          ITD_XACT_LENGTH); /* IN */
            }
            if (itd->transact[i] & ITD_XACT_IOC) {
                ehci_raise_irq(ehci, USBSTS_INT);
            }
            itd->transact[i] &= ~ITD_XACT_ACTIVE;
        }
    }
    return 0;
}


/*  This state is the entry point for asynchronous schedule
 *  processing.  Entry here consitutes a EHCI start event state (4.8.5)
 */
static int ehci_state_waitlisthead(EHCIState *ehci,  int async)
{
    EHCIqh qh;
    int i = 0;
    int again = 0;
    uint32_t entry = ehci->asynclistaddr;

    /* set reclamation flag at start event (4.8.6) */
    if (async) {
        ehci_set_usbsts(ehci, USBSTS_REC);
    }

    ehci_queues_rip_unused(ehci, async);

    /*  Find the head of the list (4.9.1.1) */
    for(i = 0; i < MAX_QH; i++) {
        if (get_dwords(ehci, NLPTR_GET(entry), (uint32_t *) &qh,
                       sizeof(EHCIqh) >> 2) < 0) {
            return 0;
        }
        ehci_trace_qh(NULL, NLPTR_GET(entry), &qh);

        if (qh.epchar & QH_EPCHAR_H) {
            if (async) {
                entry |= (NLPTR_TYPE_QH << 1);
            }

            ehci_set_fetch_addr(ehci, async, entry);
            ehci_set_state(ehci, async, EST_FETCHENTRY);
            again = 1;
            goto out;
        }

        entry = qh.next;
        if (entry == ehci->asynclistaddr) {
            break;
        }
    }

    /* no head found for list. */

    ehci_set_state(ehci, async, EST_ACTIVE);

out:
    return again;
}


/*  This state is the entry point for periodic schedule processing as
 *  well as being a continuation state for async processing.
 */
static int ehci_state_fetchentry(EHCIState *ehci, int async)
{
    int again = 0;
    uint32_t entry = ehci_get_fetch_addr(ehci, async);

    if (NLPTR_TBIT(entry)) {
        ehci_set_state(ehci, async, EST_ACTIVE);
        goto out;
    }

    /* section 4.8, only QH in async schedule */
    if (async && (NLPTR_TYPE_GET(entry) != NLPTR_TYPE_QH)) {
        fprintf(stderr, "non queue head request in async schedule\n");
        return -1;
    }

    switch (NLPTR_TYPE_GET(entry)) {
    case NLPTR_TYPE_QH:
        ehci_set_state(ehci, async, EST_FETCHQH);
        again = 1;
        break;

    case NLPTR_TYPE_ITD:
        ehci_set_state(ehci, async, EST_FETCHITD);
        again = 1;
        break;

    case NLPTR_TYPE_STITD:
        ehci_set_state(ehci, async, EST_FETCHSITD);
        again = 1;
        break;

    default:
        /* TODO: handle FSTN type */
        fprintf(stderr, "FETCHENTRY: entry at %X is of type %u "
                "which is not supported yet\n", entry, NLPTR_TYPE_GET(entry));
        return -1;
    }

out:
    return again;
}

static EHCIQueue *ehci_state_fetchqh(EHCIState *ehci, int async)
{
    uint32_t entry;
    EHCIQueue *q;
    EHCIqh qh;

    entry = ehci_get_fetch_addr(ehci, async);
    q = ehci_find_queue_by_qh(ehci, entry, async);
    if (q == NULL) {
        q = ehci_alloc_queue(ehci, entry, async);
    }

    q->seen++;
    if (q->seen > 1) {
        /* we are going in circles -- stop processing */
        ehci_set_state(ehci, async, EST_ACTIVE);
        q = NULL;
        goto out;
    }

    if (get_dwords(ehci, NLPTR_GET(q->qhaddr),
                   (uint32_t *) &qh, sizeof(EHCIqh) >> 2) < 0) {
        q = NULL;
        goto out;
    }
    ehci_trace_qh(q, NLPTR_GET(q->qhaddr), &qh);

    /*
     * The overlay area of the qh should never be changed by the guest,
     * except when idle, in which case the reset is a nop.
     */
    if (!ehci_verify_qh(q, &qh)) {
        if (ehci_reset_queue(q) > 0) {
            ehci_trace_guest_bug(ehci, "guest updated active QH");
        }
    }
    q->qh = qh;

    q->transact_ctr = get_field(q->qh.epcap, QH_EPCAP_MULT);
    if (q->transact_ctr == 0) { /* Guest bug in some versions of windows */
        q->transact_ctr = 4;
    }

    if (q->dev == NULL) {
        q->dev = ehci_find_device(q->ehci,
                                  get_field(q->qh.epchar, QH_EPCHAR_DEVADDR));
    }

    if (async && (q->qh.epchar & QH_EPCHAR_H)) {

        /*  EHCI spec version 1.0 Section 4.8.3 & 4.10.1 */
        if (ehci->usbsts & USBSTS_REC) {
            ehci_clear_usbsts(ehci, USBSTS_REC);
        } else {
            DPRINTF("FETCHQH:  QH 0x%08x. H-bit set, reclamation status reset"
                       " - done processing\n", q->qhaddr);
            ehci_set_state(ehci, async, EST_ACTIVE);
            q = NULL;
            goto out;
        }
    }

#if EHCI_DEBUG
    if (q->qhaddr != q->qh.next) {
    DPRINTF("FETCHQH:  QH 0x%08x (h %x halt %x active %x) next 0x%08x\n",
               q->qhaddr,
               q->qh.epchar & QH_EPCHAR_H,
               q->qh.token & QTD_TOKEN_HALT,
               q->qh.token & QTD_TOKEN_ACTIVE,
               q->qh.next);
    }
#endif

    if (q->qh.token & QTD_TOKEN_HALT) {
        ehci_set_state(ehci, async, EST_HORIZONTALQH);

    } else if ((q->qh.token & QTD_TOKEN_ACTIVE) &&
               (NLPTR_TBIT(q->qh.current_qtd) == 0) &&
               (q->qh.current_qtd != 0)) {
        q->qtdaddr = q->qh.current_qtd;
        ehci_set_state(ehci, async, EST_FETCHQTD);

    } else {
        /*  EHCI spec version 1.0 Section 4.10.2 */
        ehci_set_state(ehci, async, EST_ADVANCEQUEUE);
    }

out:
    return q;
}

static int ehci_state_fetchitd(EHCIState *ehci, int async)
{
    uint32_t entry;
    EHCIitd itd;

    assert(!async);
    entry = ehci_get_fetch_addr(ehci, async);

    if (get_dwords(ehci, NLPTR_GET(entry), (uint32_t *) &itd,
                   sizeof(EHCIitd) >> 2) < 0) {
        return -1;
    }
    ehci_trace_itd(ehci, entry, &itd);

    if (ehci_process_itd(ehci, &itd, entry) != 0) {
        return -1;
    }

    put_dwords(ehci, NLPTR_GET(entry), (uint32_t *) &itd,
               sizeof(EHCIitd) >> 2);
    ehci_set_fetch_addr(ehci, async, itd.next);
    ehci_set_state(ehci, async, EST_FETCHENTRY);

    return 1;
}

static int ehci_state_fetchsitd(EHCIState *ehci, int async)
{
    uint32_t entry;
    EHCIsitd sitd;

    assert(!async);
    entry = ehci_get_fetch_addr(ehci, async);

    if (get_dwords(ehci, NLPTR_GET(entry), (uint32_t *)&sitd,
                   sizeof(EHCIsitd) >> 2) < 0) {
        return 0;
    }
    ehci_trace_sitd(ehci, entry, &sitd);

    if (!(sitd.results & SITD_RESULTS_ACTIVE)) {
        /* siTD is not active, nothing to do */;
    } else {
        /* TODO: split transfers are not implemented */
        warn_report("Skipping active siTD");
    }

    ehci_set_fetch_addr(ehci, async, sitd.next);
    ehci_set_state(ehci, async, EST_FETCHENTRY);
    return 1;
}

/* Section 4.10.2 - paragraph 3 */
static int ehci_state_advqueue(EHCIQueue *q)
{
#if 0
    /* TO-DO: 4.10.2 - paragraph 2
     * if I-bit is set to 1 and QH is not active
     * go to horizontal QH
     */
    if (I-bit set) {
        ehci_set_state(ehci, async, EST_HORIZONTALQH);
        goto out;
    }
#endif

    /*
     * want data and alt-next qTD is valid
     */
    if (((q->qh.token & QTD_TOKEN_TBYTES_MASK) != 0) &&
        (NLPTR_TBIT(q->qh.altnext_qtd) == 0)) {
        q->qtdaddr = q->qh.altnext_qtd;
        ehci_set_state(q->ehci, q->async, EST_FETCHQTD);

    /*
     *  next qTD is valid
     */
    } else if (NLPTR_TBIT(q->qh.next_qtd) == 0) {
        q->qtdaddr = q->qh.next_qtd;
        ehci_set_state(q->ehci, q->async, EST_FETCHQTD);

    /*
     *  no valid qTD, try next QH
     */
    } else {
        ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
    }

    return 1;
}

/* Section 4.10.2 - paragraph 4 */
static int ehci_state_fetchqtd(EHCIQueue *q)
{
    EHCIqtd qtd;
    EHCIPacket *p;
    int again = 1;
    uint32_t addr;

    addr = NLPTR_GET(q->qtdaddr);
    if (get_dwords(q->ehci, addr +  8, &qtd.token,   1) < 0) {
        return 0;
    }
    barrier();
    if (get_dwords(q->ehci, addr +  0, &qtd.next,    1) < 0 ||
        get_dwords(q->ehci, addr +  4, &qtd.altnext, 1) < 0 ||
        get_dwords(q->ehci, addr + 12, qtd.bufptr,
                   ARRAY_SIZE(qtd.bufptr)) < 0) {
        return 0;
    }
    ehci_trace_qtd(q, NLPTR_GET(q->qtdaddr), &qtd);

    p = QTAILQ_FIRST(&q->packets);
    if (p != NULL) {
        if (!ehci_verify_qtd(p, &qtd)) {
            ehci_cancel_queue(q);
            if (qtd.token & QTD_TOKEN_ACTIVE) {
                ehci_trace_guest_bug(q->ehci, "guest updated active qTD");
            }
            p = NULL;
        } else {
            p->qtd = qtd;
            ehci_qh_do_overlay(q);
        }
    }

    if (!(qtd.token & QTD_TOKEN_ACTIVE)) {
        ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
    } else if (p != NULL) {
        switch (p->async) {
        case EHCI_ASYNC_NONE:
        case EHCI_ASYNC_INITIALIZED:
            /* Not yet executed (MULT), or previously nacked (int) packet */
            ehci_set_state(q->ehci, q->async, EST_EXECUTE);
            break;
        case EHCI_ASYNC_INFLIGHT:
            /* Check if the guest has added new tds to the queue */
            again = ehci_fill_queue(QTAILQ_LAST(&q->packets));
            /* Unfinished async handled packet, go horizontal */
            ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
            break;
        case EHCI_ASYNC_FINISHED:
            /* Complete executing of the packet */
            ehci_set_state(q->ehci, q->async, EST_EXECUTING);
            break;
        }
    } else if (q->dev == NULL) {
        ehci_trace_guest_bug(q->ehci, "no device attached to queue");
        ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
    } else {
        p = ehci_alloc_packet(q);
        p->qtdaddr = q->qtdaddr;
        p->qtd = qtd;
        ehci_set_state(q->ehci, q->async, EST_EXECUTE);
    }

    return again;
}

static int ehci_state_horizqh(EHCIQueue *q)
{
    int again = 0;

    if (ehci_get_fetch_addr(q->ehci, q->async) != q->qh.next) {
        ehci_set_fetch_addr(q->ehci, q->async, q->qh.next);
        ehci_set_state(q->ehci, q->async, EST_FETCHENTRY);
        again = 1;
    } else {
        ehci_set_state(q->ehci, q->async, EST_ACTIVE);
    }

    return again;
}

/* Returns "again" */
static int ehci_fill_queue(EHCIPacket *p)
{
    USBEndpoint *ep = p->packet.ep;
    EHCIQueue *q = p->queue;
    EHCIqtd qtd = p->qtd;
    uint32_t qtdaddr;

    for (;;) {
        if (NLPTR_TBIT(qtd.next) != 0) {
            break;
        }
        qtdaddr = qtd.next;
        /*
         * Detect circular td lists, Windows creates these, counting on the
         * active bit going low after execution to make the queue stop.
         */
        QTAILQ_FOREACH(p, &q->packets, next) {
            if (p->qtdaddr == qtdaddr) {
                goto leave;
            }
        }
        if (get_dwords(q->ehci, NLPTR_GET(qtdaddr),
                       (uint32_t *) &qtd, sizeof(EHCIqtd) >> 2) < 0) {
            return -1;
        }
        ehci_trace_qtd(q, NLPTR_GET(qtdaddr), &qtd);
        if (!(qtd.token & QTD_TOKEN_ACTIVE)) {
            break;
        }
        if (!ehci_verify_pid(q, &qtd)) {
            ehci_trace_guest_bug(q->ehci, "guest queued token with wrong pid");
            break;
        }
        p = ehci_alloc_packet(q);
        p->qtdaddr = qtdaddr;
        p->qtd = qtd;
        if (ehci_execute(p, "queue") == -1) {
            return -1;
        }
        assert(p->packet.status == USB_RET_ASYNC);
        p->async = EHCI_ASYNC_INFLIGHT;
    }
leave:
    usb_device_flush_ep_queue(ep->dev, ep);
    return 1;
}

static int ehci_state_execute(EHCIQueue *q)
{
    EHCIPacket *p = QTAILQ_FIRST(&q->packets);
    int again = 0;

    assert(p != NULL);
    assert(p->qtdaddr == q->qtdaddr);

    if (ehci_qh_do_overlay(q) != 0) {
        return -1;
    }

    // TODO verify enough time remains in the uframe as in 4.4.1.1
    // TODO write back ptr to async list when done or out of time

    /* 4.10.3, bottom of page 82, go horizontal on transaction counter == 0 */
    if (!q->async && q->transact_ctr == 0) {
        ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
        again = 1;
        goto out;
    }

    if (q->async) {
        ehci_set_usbsts(q->ehci, USBSTS_REC);
    }

    again = ehci_execute(p, "process");
    if (again == -1) {
        goto out;
    }
    if (p->packet.status == USB_RET_ASYNC) {
        ehci_flush_qh(q);
        trace_usb_ehci_packet_action(p->queue, p, "async");
        p->async = EHCI_ASYNC_INFLIGHT;
        ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
        if (q->async) {
            again = ehci_fill_queue(p);
        } else {
            again = 1;
        }
        goto out;
    }

    ehci_set_state(q->ehci, q->async, EST_EXECUTING);
    again = 1;

out:
    return again;
}

static int ehci_state_executing(EHCIQueue *q)
{
    EHCIPacket *p = QTAILQ_FIRST(&q->packets);

    assert(p != NULL);
    assert(p->qtdaddr == q->qtdaddr);

    ehci_execute_complete(q);

    /* 4.10.3 */
    if (!q->async && q->transact_ctr > 0) {
        q->transact_ctr--;
    }

    /* 4.10.5 */
    if (p->packet.status == USB_RET_NAK) {
        ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
    } else {
        ehci_set_state(q->ehci, q->async, EST_WRITEBACK);
    }

    ehci_flush_qh(q);
    return 1;
}


static int ehci_state_writeback(EHCIQueue *q)
{
    EHCIPacket *p = QTAILQ_FIRST(&q->packets);
    uint32_t *qtd, addr;
    int again = 0;

    /*  Write back the QTD from the QH area */
    assert(p != NULL);
    assert(p->qtdaddr == q->qtdaddr);

    ehci_trace_qtd(q, NLPTR_GET(p->qtdaddr), (EHCIqtd *) &q->qh.next_qtd);
    qtd = (uint32_t *) &q->qh.next_qtd;
    addr = NLPTR_GET(p->qtdaddr);
    put_dwords(q->ehci, addr + 2 * sizeof(uint32_t), qtd + 2, 2);
    ehci_free_packet(p);

    /*
     * EHCI specs say go horizontal here.
     *
     * We can also advance the queue here for performance reasons.  We
     * need to take care to only take that shortcut in case we've
     * processed the qtd just written back without errors, i.e. halt
     * bit is clear.
     */
    if (q->qh.token & QTD_TOKEN_HALT) {
        ehci_set_state(q->ehci, q->async, EST_HORIZONTALQH);
        again = 1;
    } else {
        ehci_set_state(q->ehci, q->async, EST_ADVANCEQUEUE);
        again = 1;
    }
    return again;
}

/*
 * This is the state machine that is common to both async and periodic
 */

static void ehci_advance_state(EHCIState *ehci, int async)
{
    EHCIQueue *q = NULL;
    int itd_count = 0;
    int again;

    do {
        switch(ehci_get_state(ehci, async)) {
        case EST_WAITLISTHEAD:
            again = ehci_state_waitlisthead(ehci, async);
            break;

        case EST_FETCHENTRY:
            again = ehci_state_fetchentry(ehci, async);
            break;

        case EST_FETCHQH:
            q = ehci_state_fetchqh(ehci, async);
            if (q != NULL) {
                assert(q->async == async);
                again = 1;
            } else {
                again = 0;
            }
            break;

        case EST_FETCHITD:
            again = ehci_state_fetchitd(ehci, async);
            itd_count++;
            break;

        case EST_FETCHSITD:
            again = ehci_state_fetchsitd(ehci, async);
            itd_count++;
            break;

        case EST_ADVANCEQUEUE:
            assert(q != NULL);
            again = ehci_state_advqueue(q);
            break;

        case EST_FETCHQTD:
            assert(q != NULL);
            again = ehci_state_fetchqtd(q);
            break;

        case EST_HORIZONTALQH:
            assert(q != NULL);
            again = ehci_state_horizqh(q);
            break;

        case EST_EXECUTE:
            assert(q != NULL);
            again = ehci_state_execute(q);
            if (async) {
                ehci->async_stepdown = 0;
            }
            break;

        case EST_EXECUTING:
            assert(q != NULL);
            if (async) {
                ehci->async_stepdown = 0;
            }
            again = ehci_state_executing(q);
            break;

        case EST_WRITEBACK:
            assert(q != NULL);
            again = ehci_state_writeback(q);
            if (!async) {
                ehci->periodic_sched_active = PERIODIC_ACTIVE;
            }
            break;

        default:
            fprintf(stderr, "Bad state!\n");
            g_assert_not_reached();
        }

        if (again < 0 || itd_count > 16) {
            /* TODO: notify guest (raise HSE irq?) */
            fprintf(stderr, "processing error - resetting ehci HC\n");
            ehci_reset(ehci);
            again = 0;
        }
    }
    while (again);
}

static void ehci_advance_async_state(EHCIState *ehci)
{
    const int async = 1;

    switch(ehci_get_state(ehci, async)) {
    case EST_INACTIVE:
        if (!ehci_async_enabled(ehci)) {
            break;
        }
        ehci_set_state(ehci, async, EST_ACTIVE);
        // No break, fall through to ACTIVE

    case EST_ACTIVE:
        if (!ehci_async_enabled(ehci)) {
            ehci_queues_rip_all(ehci, async);
            ehci_set_state(ehci, async, EST_INACTIVE);
            break;
        }

        /* make sure guest has acknowledged the doorbell interrupt */
        /* TO-DO: is this really needed? */
        if (ehci->usbsts & USBSTS_IAA) {
            DPRINTF("IAA status bit still set.\n");
            break;
        }

        /* check that address register has been set */
        if (ehci->asynclistaddr == 0) {
            break;
        }

        ehci_set_state(ehci, async, EST_WAITLISTHEAD);
        ehci_advance_state(ehci, async);

        /* If the doorbell is set, the guest wants to make a change to the
         * schedule. The host controller needs to release cached data.
         * (section 4.8.2)
         */
        if (ehci->usbcmd & USBCMD_IAAD) {
            /* Remove all unseen qhs from the async qhs queue */
            ehci_queues_rip_unseen(ehci, async);
            trace_usb_ehci_doorbell_ack();
            ehci->usbcmd &= ~USBCMD_IAAD;
            ehci_raise_irq(ehci, USBSTS_IAA);
        }
        break;

    default:
        /* this should only be due to a developer mistake */
        fprintf(stderr, "ehci: Bad asynchronous state %d. "
                "Resetting to active\n", ehci->astate);
        g_assert_not_reached();
    }
}

static void ehci_advance_periodic_state(EHCIState *ehci)
{
    uint32_t entry;
    uint32_t list;
    const int async = 0;

    // 4.6

    switch(ehci_get_state(ehci, async)) {
    case EST_INACTIVE:
        if (!(ehci->frindex & 7) && ehci_periodic_enabled(ehci)) {
            ehci_set_state(ehci, async, EST_ACTIVE);
            // No break, fall through to ACTIVE
        } else
            break;

    case EST_ACTIVE:
        if (!(ehci->frindex & 7) && !ehci_periodic_enabled(ehci)) {
            ehci_queues_rip_all(ehci, async);
            ehci_set_state(ehci, async, EST_INACTIVE);
            break;
        }

        list = ehci->periodiclistbase & 0xfffff000;
        /* check that register has been set */
        if (list == 0) {
            break;
        }
        list |= ((ehci->frindex & 0x1ff8) >> 1);

        if (get_dwords(ehci, list, &entry, 1) < 0) {
            break;
        }

        DPRINTF("PERIODIC state adv fr=%d.  [%08X] -> %08X\n",
                ehci->frindex / 8, list, entry);
        ehci_set_fetch_addr(ehci, async,entry);
        ehci_set_state(ehci, async, EST_FETCHENTRY);
        ehci_advance_state(ehci, async);
        ehci_queues_rip_unused(ehci, async);
        break;

    default:
        /* this should only be due to a developer mistake */
        fprintf(stderr, "ehci: Bad periodic state %d. "
                "Resetting to active\n", ehci->pstate);
        g_assert_not_reached();
    }
}

static void ehci_update_frindex(EHCIState *ehci, int uframes)
{
    if (!ehci_enabled(ehci) && ehci->pstate == EST_INACTIVE) {
        return;
    }

    /* Generate FLR interrupt if frame index rolls over 0x2000 */
    if ((ehci->frindex % 0x2000) + uframes >= 0x2000) {
        ehci_raise_irq(ehci, USBSTS_FLR);
    }

    /* How many times will frindex roll over 0x4000 with this frame count?
     * usbsts_frindex is decremented by 0x4000 on rollover until it reaches 0
     */
    int rollovers = (ehci->frindex + uframes) / 0x4000;
    if (rollovers > 0) {
        if (ehci->usbsts_frindex >= (rollovers * 0x4000)) {
            ehci->usbsts_frindex -= 0x4000 * rollovers;
        } else {
            ehci->usbsts_frindex = 0;
        }
    }

    ehci->frindex = (ehci->frindex + uframes) % 0x4000;
}

static void ehci_work_bh(void *opaque)
{
    EHCIState *ehci = opaque;
    int need_timer = 0;
    int64_t expire_time, t_now;
    uint64_t ns_elapsed;
    uint64_t uframes, skipped_uframes;
    int i;

    if (ehci->working) {
        return;
    }
    ehci->working = true;

    t_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    ns_elapsed = t_now - ehci->last_run_ns;
    uframes = ns_elapsed / UFRAME_TIMER_NS;

    if (ehci_periodic_enabled(ehci) || ehci->pstate != EST_INACTIVE) {
        need_timer++;

        if (uframes > (ehci->maxframes * 8)) {
            skipped_uframes = uframes - (ehci->maxframes * 8);
            ehci_update_frindex(ehci, skipped_uframes);
            ehci->last_run_ns += UFRAME_TIMER_NS * skipped_uframes;
            uframes -= skipped_uframes;
            DPRINTF("WARNING - EHCI skipped %d uframes\n", skipped_uframes);
        }

        for (i = 0; i < uframes; i++) {
            /*
             * If we're running behind schedule, we should not catch up
             * too fast, as that will make some guests unhappy:
             * 1) We must process a minimum of MIN_UFR_PER_TICK frames,
             *    otherwise we will never catch up
             * 2) Process frames until the guest has requested an irq (IOC)
             */
            if (i >= MIN_UFR_PER_TICK) {
                ehci_commit_irq(ehci);
                if ((ehci->usbsts & USBINTR_MASK) & ehci->usbintr) {
                    break;
                }
            }
            if (ehci->periodic_sched_active) {
                ehci->periodic_sched_active--;
            }
            ehci_update_frindex(ehci, 1);
            if ((ehci->frindex & 7) == 0) {
                ehci_advance_periodic_state(ehci);
            }
            ehci->last_run_ns += UFRAME_TIMER_NS;
        }
    } else {
        ehci->periodic_sched_active = 0;
        ehci_update_frindex(ehci, uframes);
        ehci->last_run_ns += UFRAME_TIMER_NS * uframes;
    }

    if (ehci->periodic_sched_active) {
        ehci->async_stepdown = 0;
    } else if (ehci->async_stepdown < ehci->maxframes / 2) {
        ehci->async_stepdown++;
    }

    /*  Async is not inside loop since it executes everything it can once
     *  called
     */
    if (ehci_async_enabled(ehci) || ehci->astate != EST_INACTIVE) {
        need_timer++;
        ehci_advance_async_state(ehci);
    }

    ehci_commit_irq(ehci);
    if (ehci->usbsts_pending) {
        need_timer++;
        ehci->async_stepdown = 0;
    }

    if (ehci_enabled(ehci) && (ehci->usbintr & USBSTS_FLR)) {
        need_timer++;
    }

    if (need_timer) {
        /* If we've raised int, we speed up the timer, so that we quickly
         * notice any new packets queued up in response */
        if (ehci->int_req_by_async && (ehci->usbsts & USBSTS_INT)) {
            expire_time = t_now +
                NANOSECONDS_PER_SECOND / (FRAME_TIMER_FREQ * 4);
            ehci->int_req_by_async = false;
        } else {
            expire_time = t_now + (NANOSECONDS_PER_SECOND
                               * (ehci->async_stepdown+1) / FRAME_TIMER_FREQ);
        }
        timer_mod(ehci->frame_timer, expire_time);
    }

    ehci->working = false;
}

static void ehci_work_timer(void *opaque)
{
    EHCIState *ehci = opaque;

    qemu_bh_schedule(ehci->async_bh);
}

static const MemoryRegionOps ehci_mmio_caps_ops = {
    .read = ehci_caps_read,
    .write = ehci_caps_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps ehci_mmio_opreg_ops = {
    .read = ehci_opreg_read,
    .write = ehci_opreg_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps ehci_mmio_port_ops = {
    .read = ehci_port_read,
    .write = ehci_port_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static USBPortOps ehci_port_ops = {
    .attach = ehci_attach,
    .detach = ehci_detach,
    .child_detach = ehci_child_detach,
    .wakeup = ehci_wakeup,
    .complete = ehci_async_complete_packet,
};

static USBBusOps ehci_bus_ops_companion = {
    .register_companion = ehci_register_companion,
    .wakeup_endpoint = ehci_wakeup_endpoint,
};
static USBBusOps ehci_bus_ops_standalone = {
    .wakeup_endpoint = ehci_wakeup_endpoint,
};

static int usb_ehci_pre_save(void *opaque)
{
    EHCIState *ehci = opaque;
    uint32_t new_frindex;

    /* Round down frindex to a multiple of 8 for migration compatibility */
    new_frindex = ehci->frindex & ~7;
    ehci->last_run_ns -= (ehci->frindex - new_frindex) * UFRAME_TIMER_NS;
    ehci->frindex = new_frindex;

    return 0;
}

static int usb_ehci_post_load(void *opaque, int version_id)
{
    EHCIState *s = opaque;
    int i;

    for (i = 0; i < NB_PORTS; i++) {
        USBPort *companion = s->companion_ports[i];
        if (companion == NULL) {
            continue;
        }
        if (s->portsc[i] & PORTSC_POWNER) {
            companion->dev = s->ports[i].dev;
        } else {
            companion->dev = NULL;
        }
    }

    return 0;
}

static void usb_ehci_vm_state_change(void *opaque, bool running, RunState state)
{
    EHCIState *ehci = opaque;

    /*
     * We don't migrate the EHCIQueue-s, instead we rebuild them for the
     * schedule in guest memory. We must do the rebuilt ASAP, so that
     * USB-devices which have async handled packages have a packet in the
     * ep queue to match the completion with.
     */
    if (state == RUN_STATE_RUNNING) {
        ehci_advance_async_state(ehci);
    }

    /*
     * The schedule rebuilt from guest memory could cause the migration dest
     * to miss a QH unlink, and fail to cancel packets, since the unlinked QH
     * will never have existed on the destination. Therefor we must flush the
     * async schedule on savevm to catch any not yet noticed unlinks.
     */
    if (state == RUN_STATE_SAVE_VM) {
        ehci_advance_async_state(ehci);
        ehci_queues_rip_unseen(ehci, 1);
    }
}

const VMStateDescription vmstate_ehci = {
    .name        = "ehci-core",
    .version_id  = 2,
    .minimum_version_id  = 1,
    .pre_save    = usb_ehci_pre_save,
    .post_load   = usb_ehci_post_load,
    .fields = (VMStateField[]) {
        /* mmio registers */
        VMSTATE_UINT32(usbcmd, EHCIState),
        VMSTATE_UINT32(usbsts, EHCIState),
        VMSTATE_UINT32_V(usbsts_pending, EHCIState, 2),
        VMSTATE_UINT32_V(usbsts_frindex, EHCIState, 2),
        VMSTATE_UINT32(usbintr, EHCIState),
        VMSTATE_UINT32(frindex, EHCIState),
        VMSTATE_UINT32(ctrldssegment, EHCIState),
        VMSTATE_UINT32(periodiclistbase, EHCIState),
        VMSTATE_UINT32(asynclistaddr, EHCIState),
        VMSTATE_UINT32(configflag, EHCIState),
        VMSTATE_UINT32(portsc[0], EHCIState),
        VMSTATE_UINT32(portsc[1], EHCIState),
        VMSTATE_UINT32(portsc[2], EHCIState),
        VMSTATE_UINT32(portsc[3], EHCIState),
        VMSTATE_UINT32(portsc[4], EHCIState),
        VMSTATE_UINT32(portsc[5], EHCIState),
        /* frame timer */
        VMSTATE_TIMER_PTR(frame_timer, EHCIState),
        VMSTATE_UINT64(last_run_ns, EHCIState),
        VMSTATE_UINT32(async_stepdown, EHCIState),
        /* schedule state */
        VMSTATE_UINT32(astate, EHCIState),
        VMSTATE_UINT32(pstate, EHCIState),
        VMSTATE_UINT32(a_fetch_addr, EHCIState),
        VMSTATE_UINT32(p_fetch_addr, EHCIState),
        VMSTATE_END_OF_LIST()
    }
};

void usb_ehci_realize(EHCIState *s, DeviceState *dev, Error **errp)
{
    int i;

    if (s->portnr > NB_PORTS) {
        error_setg(errp, "Too many ports! Max. port number is %d.",
                   NB_PORTS);
        return;
    }
    if (s->maxframes < 8 || s->maxframes > 512)  {
        error_setg(errp, "maxframes %d out if range (8 .. 512)",
                   s->maxframes);
        return;
    }

    memory_region_add_subregion(&s->mem, s->capsbase, &s->mem_caps);
    memory_region_add_subregion(&s->mem, s->opregbase, &s->mem_opreg);
    memory_region_add_subregion(&s->mem, s->opregbase + s->portscbase,
                                &s->mem_ports);

    usb_bus_new(&s->bus, sizeof(s->bus), s->companion_enable ?
                &ehci_bus_ops_companion : &ehci_bus_ops_standalone, dev);
    for (i = 0; i < s->portnr; i++) {
        usb_register_port(&s->bus, &s->ports[i], s, i, &ehci_port_ops,
                          USB_SPEED_MASK_HIGH);
        s->ports[i].dev = 0;
    }

    s->frame_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, ehci_work_timer, s);
    s->async_bh = qemu_bh_new(ehci_work_bh, s);
    s->device = dev;

    s->vmstate = qemu_add_vm_change_state_handler(usb_ehci_vm_state_change, s);
}

void usb_ehci_unrealize(EHCIState *s, DeviceState *dev)
{
    trace_usb_ehci_unrealize();

    if (s->frame_timer) {
        timer_free(s->frame_timer);
        s->frame_timer = NULL;
    }
    if (s->async_bh) {
        qemu_bh_delete(s->async_bh);
    }

    ehci_queues_rip_all(s, 0);
    ehci_queues_rip_all(s, 1);

    memory_region_del_subregion(&s->mem, &s->mem_caps);
    memory_region_del_subregion(&s->mem, &s->mem_opreg);
    memory_region_del_subregion(&s->mem, &s->mem_ports);

    usb_bus_release(&s->bus);

    if (s->vmstate) {
        qemu_del_vm_change_state_handler(s->vmstate);
    }
}

void usb_ehci_init(EHCIState *s, DeviceState *dev)
{
    /* 2.2 host controller interface version */
    s->caps[0x00] = (uint8_t)(s->opregbase - s->capsbase);
    s->caps[0x01] = 0x00;
    s->caps[0x02] = 0x00;
    s->caps[0x03] = 0x01;        /* HC version */
    s->caps[0x04] = s->portnr;   /* Number of downstream ports */
    s->caps[0x05] = 0x00;        /* No companion ports at present */
    s->caps[0x06] = 0x00;
    s->caps[0x07] = 0x00;
    s->caps[0x08] = 0x80;        /* We can cache whole frame, no 64-bit */
    s->caps[0x0a] = 0x00;
    s->caps[0x0b] = 0x00;

    QTAILQ_INIT(&s->aqueues);
    QTAILQ_INIT(&s->pqueues);
    usb_packet_init(&s->ipacket);

    memory_region_init(&s->mem, OBJECT(dev), "ehci", MMIO_SIZE);
    memory_region_init_io(&s->mem_caps, OBJECT(dev), &ehci_mmio_caps_ops, s,
                          "capabilities", CAPA_SIZE);
    memory_region_init_io(&s->mem_opreg, OBJECT(dev), &ehci_mmio_opreg_ops, s,
                          "operational", s->portscbase);
    memory_region_init_io(&s->mem_ports, OBJECT(dev), &ehci_mmio_port_ops, s,
                          "ports", 4 * s->portnr);
}

void usb_ehci_finalize(EHCIState *s)
{
    usb_packet_cleanup(&s->ipacket);
}

/*
 * vim: expandtab ts=4
 */
