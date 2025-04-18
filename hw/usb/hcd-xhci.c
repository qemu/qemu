/*
 * USB xHCI controller emulation
 *
 * Copyright (c) 2011 Securiforest
 * Date: 2011-05-11 ;  Author: Hector Martin <hector@marcansoft.com>
 * Based on usb-ohci.c, emulates Renesas NEC USB 3.0
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/queue.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "trace.h"
#include "qapi/error.h"

#include "hcd-xhci.h"

//#define DEBUG_XHCI
//#define DEBUG_DATA

#ifdef DEBUG_XHCI
#define DPRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINTF(...) do {} while (0)
#endif
#define FIXME(_msg) do { fprintf(stderr, "FIXME %s:%d %s\n", \
                                 __func__, __LINE__, _msg); abort(); } while (0)

#define TRB_LINK_LIMIT  32
#define COMMAND_LIMIT   256
#define TRANSFER_LIMIT  256

#define LEN_CAP         0x40
#define LEN_OPER        (0x400 + 0x10 * XHCI_MAXPORTS)
#define LEN_RUNTIME     ((XHCI_MAXINTRS + 1) * 0x20)
#define LEN_DOORBELL    ((XHCI_MAXSLOTS + 1) * 0x20)

#define OFF_OPER        LEN_CAP
#define OFF_RUNTIME     0x1000
#define OFF_DOORBELL    0x2000

#if (OFF_OPER + LEN_OPER) > OFF_RUNTIME
#error Increase OFF_RUNTIME
#endif
#if (OFF_RUNTIME + LEN_RUNTIME) > OFF_DOORBELL
#error Increase OFF_DOORBELL
#endif
#if (OFF_DOORBELL + LEN_DOORBELL) > XHCI_LEN_REGS
# error Increase XHCI_LEN_REGS
#endif

/* bit definitions */
#define USBCMD_RS       (1<<0)
#define USBCMD_HCRST    (1<<1)
#define USBCMD_INTE     (1<<2)
#define USBCMD_HSEE     (1<<3)
#define USBCMD_LHCRST   (1<<7)
#define USBCMD_CSS      (1<<8)
#define USBCMD_CRS      (1<<9)
#define USBCMD_EWE      (1<<10)
#define USBCMD_EU3S     (1<<11)

#define USBSTS_HCH      (1<<0)
#define USBSTS_HSE      (1<<2)
#define USBSTS_EINT     (1<<3)
#define USBSTS_PCD      (1<<4)
#define USBSTS_SSS      (1<<8)
#define USBSTS_RSS      (1<<9)
#define USBSTS_SRE      (1<<10)
#define USBSTS_CNR      (1<<11)
#define USBSTS_HCE      (1<<12)


#define PORTSC_CCS          (1<<0)
#define PORTSC_PED          (1<<1)
#define PORTSC_OCA          (1<<3)
#define PORTSC_PR           (1<<4)
#define PORTSC_PLS_SHIFT        5
#define PORTSC_PLS_MASK     0xf
#define PORTSC_PP           (1<<9)
#define PORTSC_SPEED_SHIFT      10
#define PORTSC_SPEED_MASK   0xf
#define PORTSC_SPEED_FULL   (1<<10)
#define PORTSC_SPEED_LOW    (2<<10)
#define PORTSC_SPEED_HIGH   (3<<10)
#define PORTSC_SPEED_SUPER  (4<<10)
#define PORTSC_PIC_SHIFT        14
#define PORTSC_PIC_MASK     0x3
#define PORTSC_LWS          (1<<16)
#define PORTSC_CSC          (1<<17)
#define PORTSC_PEC          (1<<18)
#define PORTSC_WRC          (1<<19)
#define PORTSC_OCC          (1<<20)
#define PORTSC_PRC          (1<<21)
#define PORTSC_PLC          (1<<22)
#define PORTSC_CEC          (1<<23)
#define PORTSC_CAS          (1<<24)
#define PORTSC_WCE          (1<<25)
#define PORTSC_WDE          (1<<26)
#define PORTSC_WOE          (1<<27)
#define PORTSC_DR           (1<<30)
#define PORTSC_WPR          (1<<31)

#define CRCR_RCS        (1<<0)
#define CRCR_CS         (1<<1)
#define CRCR_CA         (1<<2)
#define CRCR_CRR        (1<<3)

#define IMAN_IP         (1<<0)
#define IMAN_IE         (1<<1)

#define ERDP_EHB        (1<<3)

#define TRB_SIZE 16
typedef struct XHCITRB {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
    dma_addr_t addr;
    bool ccs;
} XHCITRB;

enum {
    PLS_U0              =  0,
    PLS_U1              =  1,
    PLS_U2              =  2,
    PLS_U3              =  3,
    PLS_DISABLED        =  4,
    PLS_RX_DETECT       =  5,
    PLS_INACTIVE        =  6,
    PLS_POLLING         =  7,
    PLS_RECOVERY        =  8,
    PLS_HOT_RESET       =  9,
    PLS_COMPILANCE_MODE = 10,
    PLS_TEST_MODE       = 11,
    PLS_RESUME          = 15,
};

#define CR_LINK TR_LINK

#define TRB_C               (1<<0)
#define TRB_TYPE_SHIFT          10
#define TRB_TYPE_MASK       0x3f
#define TRB_TYPE(t)         (((t).control >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK)

#define TRB_EV_ED           (1<<2)

#define TRB_TR_ENT          (1<<1)
#define TRB_TR_ISP          (1<<2)
#define TRB_TR_NS           (1<<3)
#define TRB_TR_CH           (1<<4)
#define TRB_TR_IOC          (1<<5)
#define TRB_TR_IDT          (1<<6)
#define TRB_TR_TBC_SHIFT        7
#define TRB_TR_TBC_MASK     0x3
#define TRB_TR_BEI          (1<<9)
#define TRB_TR_TLBPC_SHIFT      16
#define TRB_TR_TLBPC_MASK   0xf
#define TRB_TR_FRAMEID_SHIFT    20
#define TRB_TR_FRAMEID_MASK 0x7ff
#define TRB_TR_SIA          (1<<31)

#define TRB_TR_DIR          (1<<16)

#define TRB_CR_SLOTID_SHIFT     24
#define TRB_CR_SLOTID_MASK  0xff
#define TRB_CR_EPID_SHIFT       16
#define TRB_CR_EPID_MASK    0x1f

#define TRB_CR_BSR          (1<<9)
#define TRB_CR_DC           (1<<9)

#define TRB_LK_TC           (1<<1)

#define TRB_INTR_SHIFT          22
#define TRB_INTR_MASK       0x3ff
#define TRB_INTR(t)         (((t).status >> TRB_INTR_SHIFT) & TRB_INTR_MASK)

#define EP_TYPE_MASK        0x7
#define EP_TYPE_SHIFT           3

#define EP_STATE_MASK       0x7
#define EP_DISABLED         (0<<0)
#define EP_RUNNING          (1<<0)
#define EP_HALTED           (2<<0)
#define EP_STOPPED          (3<<0)
#define EP_ERROR            (4<<0)

#define SLOT_STATE_MASK     0x1f
#define SLOT_STATE_SHIFT        27
#define SLOT_STATE(s)       (((s)>>SLOT_STATE_SHIFT)&SLOT_STATE_MASK)
#define SLOT_ENABLED        0
#define SLOT_DEFAULT        1
#define SLOT_ADDRESSED      2
#define SLOT_CONFIGURED     3

#define SLOT_CONTEXT_ENTRIES_MASK 0x1f
#define SLOT_CONTEXT_ENTRIES_SHIFT 27

#define get_field(data, field)                  \
    (((data) >> field##_SHIFT) & field##_MASK)

#define set_field(data, newval, field) do {                     \
        uint32_t val_ = *data;                                  \
        val_ &= ~(field##_MASK << field##_SHIFT);               \
        val_ |= ((newval) & field##_MASK) << field##_SHIFT;     \
        *data = val_;                                           \
    } while (0)

typedef enum EPType {
    ET_INVALID = 0,
    ET_ISO_OUT,
    ET_BULK_OUT,
    ET_INTR_OUT,
    ET_CONTROL,
    ET_ISO_IN,
    ET_BULK_IN,
    ET_INTR_IN,
} EPType;

typedef struct XHCITransfer {
    XHCIEPContext *epctx;
    USBPacket packet;
    QEMUSGList sgl;
    bool running_async;
    bool running_retry;
    bool complete;
    bool int_req;
    unsigned int iso_pkts;
    unsigned int streamid;
    bool in_xfer;
    bool iso_xfer;
    bool timed_xfer;

    unsigned int trb_count;
    XHCITRB *trbs;

    TRBCCode status;

    unsigned int pkts;
    unsigned int pktsize;
    unsigned int cur_pkt;

    uint64_t mfindex_kick;

    QTAILQ_ENTRY(XHCITransfer) next;
} XHCITransfer;

struct XHCIStreamContext {
    dma_addr_t pctx;
    unsigned int sct;
    XHCIRing ring;
};

struct XHCIEPContext {
    XHCIState *xhci;
    unsigned int slotid;
    unsigned int epid;

    XHCIRing ring;
    uint32_t xfer_count;
    QTAILQ_HEAD(, XHCITransfer) transfers;
    XHCITransfer *retry;
    EPType type;
    dma_addr_t pctx;
    unsigned int max_psize;
    uint32_t state;
    uint32_t kick_active;

    /* streams */
    unsigned int max_pstreams;
    bool         lsa;
    unsigned int nr_pstreams;
    XHCIStreamContext *pstreams;

    /* iso xfer scheduling */
    unsigned int interval;
    int64_t mfindex_last;
    QEMUTimer *kick_timer;
};

typedef struct XHCIEvRingSeg {
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t size;
    uint32_t rsvd;
} XHCIEvRingSeg;

static void xhci_kick_ep(XHCIState *xhci, unsigned int slotid,
                         unsigned int epid, unsigned int streamid);
static void xhci_kick_epctx(XHCIEPContext *epctx, unsigned int streamid);
static TRBCCode xhci_disable_ep(XHCIState *xhci, unsigned int slotid,
                                unsigned int epid);
static void xhci_xfer_report(XHCITransfer *xfer);
static void xhci_event(XHCIState *xhci, XHCIEvent *event, int v);
static void xhci_write_event(XHCIState *xhci, XHCIEvent *event, int v);
static USBEndpoint *xhci_epid_to_usbep(XHCIEPContext *epctx);

static const char *TRBType_names[] = {
    [TRB_RESERVED]                     = "TRB_RESERVED",
    [TR_NORMAL]                        = "TR_NORMAL",
    [TR_SETUP]                         = "TR_SETUP",
    [TR_DATA]                          = "TR_DATA",
    [TR_STATUS]                        = "TR_STATUS",
    [TR_ISOCH]                         = "TR_ISOCH",
    [TR_LINK]                          = "TR_LINK",
    [TR_EVDATA]                        = "TR_EVDATA",
    [TR_NOOP]                          = "TR_NOOP",
    [CR_ENABLE_SLOT]                   = "CR_ENABLE_SLOT",
    [CR_DISABLE_SLOT]                  = "CR_DISABLE_SLOT",
    [CR_ADDRESS_DEVICE]                = "CR_ADDRESS_DEVICE",
    [CR_CONFIGURE_ENDPOINT]            = "CR_CONFIGURE_ENDPOINT",
    [CR_EVALUATE_CONTEXT]              = "CR_EVALUATE_CONTEXT",
    [CR_RESET_ENDPOINT]                = "CR_RESET_ENDPOINT",
    [CR_STOP_ENDPOINT]                 = "CR_STOP_ENDPOINT",
    [CR_SET_TR_DEQUEUE]                = "CR_SET_TR_DEQUEUE",
    [CR_RESET_DEVICE]                  = "CR_RESET_DEVICE",
    [CR_FORCE_EVENT]                   = "CR_FORCE_EVENT",
    [CR_NEGOTIATE_BW]                  = "CR_NEGOTIATE_BW",
    [CR_SET_LATENCY_TOLERANCE]         = "CR_SET_LATENCY_TOLERANCE",
    [CR_GET_PORT_BANDWIDTH]            = "CR_GET_PORT_BANDWIDTH",
    [CR_FORCE_HEADER]                  = "CR_FORCE_HEADER",
    [CR_NOOP]                          = "CR_NOOP",
    [ER_TRANSFER]                      = "ER_TRANSFER",
    [ER_COMMAND_COMPLETE]              = "ER_COMMAND_COMPLETE",
    [ER_PORT_STATUS_CHANGE]            = "ER_PORT_STATUS_CHANGE",
    [ER_BANDWIDTH_REQUEST]             = "ER_BANDWIDTH_REQUEST",
    [ER_DOORBELL]                      = "ER_DOORBELL",
    [ER_HOST_CONTROLLER]               = "ER_HOST_CONTROLLER",
    [ER_DEVICE_NOTIFICATION]           = "ER_DEVICE_NOTIFICATION",
    [ER_MFINDEX_WRAP]                  = "ER_MFINDEX_WRAP",
    [CR_VENDOR_NEC_FIRMWARE_REVISION]  = "CR_VENDOR_NEC_FIRMWARE_REVISION",
    [CR_VENDOR_NEC_CHALLENGE_RESPONSE] = "CR_VENDOR_NEC_CHALLENGE_RESPONSE",
};

static const char *TRBCCode_names[] = {
    [CC_INVALID]                       = "CC_INVALID",
    [CC_SUCCESS]                       = "CC_SUCCESS",
    [CC_DATA_BUFFER_ERROR]             = "CC_DATA_BUFFER_ERROR",
    [CC_BABBLE_DETECTED]               = "CC_BABBLE_DETECTED",
    [CC_USB_TRANSACTION_ERROR]         = "CC_USB_TRANSACTION_ERROR",
    [CC_TRB_ERROR]                     = "CC_TRB_ERROR",
    [CC_STALL_ERROR]                   = "CC_STALL_ERROR",
    [CC_RESOURCE_ERROR]                = "CC_RESOURCE_ERROR",
    [CC_BANDWIDTH_ERROR]               = "CC_BANDWIDTH_ERROR",
    [CC_NO_SLOTS_ERROR]                = "CC_NO_SLOTS_ERROR",
    [CC_INVALID_STREAM_TYPE_ERROR]     = "CC_INVALID_STREAM_TYPE_ERROR",
    [CC_SLOT_NOT_ENABLED_ERROR]        = "CC_SLOT_NOT_ENABLED_ERROR",
    [CC_EP_NOT_ENABLED_ERROR]          = "CC_EP_NOT_ENABLED_ERROR",
    [CC_SHORT_PACKET]                  = "CC_SHORT_PACKET",
    [CC_RING_UNDERRUN]                 = "CC_RING_UNDERRUN",
    [CC_RING_OVERRUN]                  = "CC_RING_OVERRUN",
    [CC_VF_ER_FULL]                    = "CC_VF_ER_FULL",
    [CC_PARAMETER_ERROR]               = "CC_PARAMETER_ERROR",
    [CC_BANDWIDTH_OVERRUN]             = "CC_BANDWIDTH_OVERRUN",
    [CC_CONTEXT_STATE_ERROR]           = "CC_CONTEXT_STATE_ERROR",
    [CC_NO_PING_RESPONSE_ERROR]        = "CC_NO_PING_RESPONSE_ERROR",
    [CC_EVENT_RING_FULL_ERROR]         = "CC_EVENT_RING_FULL_ERROR",
    [CC_INCOMPATIBLE_DEVICE_ERROR]     = "CC_INCOMPATIBLE_DEVICE_ERROR",
    [CC_MISSED_SERVICE_ERROR]          = "CC_MISSED_SERVICE_ERROR",
    [CC_COMMAND_RING_STOPPED]          = "CC_COMMAND_RING_STOPPED",
    [CC_COMMAND_ABORTED]               = "CC_COMMAND_ABORTED",
    [CC_STOPPED]                       = "CC_STOPPED",
    [CC_STOPPED_LENGTH_INVALID]        = "CC_STOPPED_LENGTH_INVALID",
    [CC_MAX_EXIT_LATENCY_TOO_LARGE_ERROR]
    = "CC_MAX_EXIT_LATENCY_TOO_LARGE_ERROR",
    [CC_ISOCH_BUFFER_OVERRUN]          = "CC_ISOCH_BUFFER_OVERRUN",
    [CC_EVENT_LOST_ERROR]              = "CC_EVENT_LOST_ERROR",
    [CC_UNDEFINED_ERROR]               = "CC_UNDEFINED_ERROR",
    [CC_INVALID_STREAM_ID_ERROR]       = "CC_INVALID_STREAM_ID_ERROR",
    [CC_SECONDARY_BANDWIDTH_ERROR]     = "CC_SECONDARY_BANDWIDTH_ERROR",
    [CC_SPLIT_TRANSACTION_ERROR]       = "CC_SPLIT_TRANSACTION_ERROR",
};

static const char *ep_state_names[] = {
    [EP_DISABLED] = "disabled",
    [EP_RUNNING]  = "running",
    [EP_HALTED]   = "halted",
    [EP_STOPPED]  = "stopped",
    [EP_ERROR]    = "error",
};

static const char *lookup_name(uint32_t index, const char **list, uint32_t llen)
{
    if (index >= llen || list[index] == NULL) {
        return "???";
    }
    return list[index];
}

static const char *trb_name(XHCITRB *trb)
{
    return lookup_name(TRB_TYPE(*trb), TRBType_names,
                       ARRAY_SIZE(TRBType_names));
}

static const char *event_name(XHCIEvent *event)
{
    return lookup_name(event->ccode, TRBCCode_names,
                       ARRAY_SIZE(TRBCCode_names));
}

static const char *ep_state_name(uint32_t state)
{
    return lookup_name(state, ep_state_names,
                       ARRAY_SIZE(ep_state_names));
}

bool xhci_get_flag(XHCIState *xhci, enum xhci_flags bit)
{
    return xhci->flags & (1 << bit);
}

void xhci_set_flag(XHCIState *xhci, enum xhci_flags bit)
{
    xhci->flags |= (1 << bit);
}

static uint64_t xhci_mfindex_get(XHCIState *xhci)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    return (now - xhci->mfindex_start) / 125000;
}

static void xhci_mfwrap_update(XHCIState *xhci)
{
    const uint32_t bits = USBCMD_RS | USBCMD_EWE;
    uint32_t mfindex, left;
    int64_t now;

    if ((xhci->usbcmd & bits) == bits) {
        now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        mfindex = ((now - xhci->mfindex_start) / 125000) & 0x3fff;
        left = 0x4000 - mfindex;
        timer_mod(xhci->mfwrap_timer, now + left * 125000);
    } else {
        timer_del(xhci->mfwrap_timer);
    }
}

static void xhci_mfwrap_timer(void *opaque)
{
    XHCIState *xhci = opaque;
    XHCIEvent wrap = { ER_MFINDEX_WRAP, CC_SUCCESS };

    xhci_event(xhci, &wrap, 0);
    xhci_mfwrap_update(xhci);
}

static void xhci_die(XHCIState *xhci)
{
    xhci->usbsts |= USBSTS_HCE;
    DPRINTF("xhci: asserted controller error\n");
}

static inline dma_addr_t xhci_addr64(uint32_t low, uint32_t high)
{
    if (sizeof(dma_addr_t) == 4) {
        return low;
    } else {
        return low | (((dma_addr_t)high << 16) << 16);
    }
}

static inline dma_addr_t xhci_mask64(uint64_t addr)
{
    if (sizeof(dma_addr_t) == 4) {
        return addr & 0xffffffff;
    } else {
        return addr;
    }
}

static inline void xhci_dma_read_u32s(XHCIState *xhci, dma_addr_t addr,
                                      uint32_t *buf, size_t len)
{
    int i;

    assert((len % sizeof(uint32_t)) == 0);

    if (dma_memory_read(xhci->as, addr, buf, len,
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA memory access failed!\n",
                      __func__);
        memset(buf, 0xff, len);
        xhci_die(xhci);
        return;
    }

    for (i = 0; i < (len / sizeof(uint32_t)); i++) {
        buf[i] = le32_to_cpu(buf[i]);
    }
}

static inline void xhci_dma_write_u32s(XHCIState *xhci, dma_addr_t addr,
                                       const uint32_t *buf, size_t len)
{
    int i;
    uint32_t tmp[5];
    uint32_t n = len / sizeof(uint32_t);

    assert((len % sizeof(uint32_t)) == 0);
    assert(n <= ARRAY_SIZE(tmp));

    for (i = 0; i < n; i++) {
        tmp[i] = cpu_to_le32(buf[i]);
    }
    if (dma_memory_write(xhci->as, addr, tmp, len,
                         MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA memory access failed!\n",
                      __func__);
        xhci_die(xhci);
        return;
    }
}

static XHCIPort *xhci_lookup_port(XHCIState *xhci, struct USBPort *uport)
{
    int index;

    if (!uport->dev) {
        return NULL;
    }
    switch (uport->dev->speed) {
    case USB_SPEED_LOW:
    case USB_SPEED_FULL:
    case USB_SPEED_HIGH:
        index = uport->index + xhci->numports_3;
        break;
    case USB_SPEED_SUPER:
        index = uport->index;
        break;
    default:
        return NULL;
    }
    return &xhci->ports[index];
}

static void xhci_intr_update(XHCIState *xhci, int v)
{
    int level = 0;

    if (v == 0) {
        if (xhci->intr[0].iman & IMAN_IP &&
            xhci->intr[0].iman & IMAN_IE &&
            xhci->usbcmd & USBCMD_INTE) {
            level = 1;
        }
        if (xhci->intr_raise) {
            if (xhci->intr_raise(xhci, 0, level)) {
                xhci->intr[0].iman &= ~IMAN_IP;
            }
        }
    }
    if (xhci->intr_update) {
        xhci->intr_update(xhci, v,
                     xhci->intr[v].iman & IMAN_IE);
    }
}

static void xhci_intr_raise(XHCIState *xhci, int v)
{
    bool pending = (xhci->intr[v].erdp_low & ERDP_EHB);

    xhci->intr[v].erdp_low |= ERDP_EHB;
    xhci->intr[v].iman |= IMAN_IP;
    xhci->usbsts |= USBSTS_EINT;

    if (pending) {
        return;
    }
    if (!(xhci->intr[v].iman & IMAN_IE)) {
        return;
    }

    if (!(xhci->usbcmd & USBCMD_INTE)) {
        return;
    }
    if (xhci->intr_raise) {
        if (xhci->intr_raise(xhci, v, true)) {
            xhci->intr[v].iman &= ~IMAN_IP;
        }
    }
}

static inline int xhci_running(XHCIState *xhci)
{
    return !(xhci->usbsts & USBSTS_HCH);
}

static void xhci_write_event(XHCIState *xhci, XHCIEvent *event, int v)
{
    XHCIInterrupter *intr = &xhci->intr[v];
    XHCITRB ev_trb;
    dma_addr_t addr;

    ev_trb.parameter = cpu_to_le64(event->ptr);
    ev_trb.status = cpu_to_le32(event->length | (event->ccode << 24));
    ev_trb.control = (event->slotid << 24) | (event->epid << 16) |
                     event->flags | (event->type << TRB_TYPE_SHIFT);
    if (intr->er_pcs) {
        ev_trb.control |= TRB_C;
    }
    ev_trb.control = cpu_to_le32(ev_trb.control);

    trace_usb_xhci_queue_event(v, intr->er_ep_idx, trb_name(&ev_trb),
                               event_name(event), ev_trb.parameter,
                               ev_trb.status, ev_trb.control);

    addr = intr->er_start + TRB_SIZE*intr->er_ep_idx;
    if (dma_memory_write(xhci->as, addr, &ev_trb, TRB_SIZE,
                         MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA memory access failed!\n",
                      __func__);
        xhci_die(xhci);
    }

    intr->er_ep_idx++;
    if (intr->er_ep_idx >= intr->er_size) {
        intr->er_ep_idx = 0;
        intr->er_pcs = !intr->er_pcs;
    }
}

static void xhci_event(XHCIState *xhci, XHCIEvent *event, int v)
{
    XHCIInterrupter *intr;
    dma_addr_t erdp;
    unsigned int dp_idx;

    if (xhci->numintrs == 1 ||
        (xhci->intr_mapping_supported && !xhci->intr_mapping_supported(xhci))) {
        v = 0;
    }

    if (v >= xhci->numintrs) {
        DPRINTF("intr nr out of range (%d >= %d)\n", v, xhci->numintrs);
        return;
    }
    intr = &xhci->intr[v];

    erdp = xhci_addr64(intr->erdp_low, intr->erdp_high);
    if (erdp < intr->er_start ||
        erdp >= (intr->er_start + TRB_SIZE*intr->er_size)) {
        DPRINTF("xhci: ERDP out of bounds: "DMA_ADDR_FMT"\n", erdp);
        DPRINTF("xhci: ER[%d] at "DMA_ADDR_FMT" len %d\n",
                v, intr->er_start, intr->er_size);
        xhci_die(xhci);
        return;
    }

    dp_idx = (erdp - intr->er_start) / TRB_SIZE;
    assert(dp_idx < intr->er_size);

    if ((intr->er_ep_idx + 2) % intr->er_size == dp_idx) {
        DPRINTF("xhci: ER %d full, send ring full error\n", v);
        XHCIEvent full = {ER_HOST_CONTROLLER, CC_EVENT_RING_FULL_ERROR};
        xhci_write_event(xhci, &full, v);
    } else if ((intr->er_ep_idx + 1) % intr->er_size == dp_idx) {
        DPRINTF("xhci: ER %d full, drop event\n", v);
    } else {
        xhci_write_event(xhci, event, v);
    }

    xhci_intr_raise(xhci, v);
}

static void xhci_ring_init(XHCIState *xhci, XHCIRing *ring,
                           dma_addr_t base)
{
    ring->dequeue = base;
    ring->ccs = 1;
}

static TRBType xhci_ring_fetch(XHCIState *xhci, XHCIRing *ring, XHCITRB *trb,
                               dma_addr_t *addr)
{
    uint32_t link_cnt = 0;

    while (1) {
        TRBType type;
        if (dma_memory_read(xhci->as, ring->dequeue, trb, TRB_SIZE,
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA memory access failed!\n",
                          __func__);
            return 0;
        }
        trb->addr = ring->dequeue;
        trb->ccs = ring->ccs;
        le64_to_cpus(&trb->parameter);
        le32_to_cpus(&trb->status);
        le32_to_cpus(&trb->control);

        trace_usb_xhci_fetch_trb(ring->dequeue, trb_name(trb),
                                 trb->parameter, trb->status, trb->control);

        if ((trb->control & TRB_C) != ring->ccs) {
            return 0;
        }

        type = TRB_TYPE(*trb);

        if (type != TR_LINK) {
            if (addr) {
                *addr = ring->dequeue;
            }
            ring->dequeue += TRB_SIZE;
            return type;
        } else {
            if (++link_cnt > TRB_LINK_LIMIT) {
                trace_usb_xhci_enforced_limit("trb-link");
                return 0;
            }
            ring->dequeue = xhci_mask64(trb->parameter);
            if (trb->control & TRB_LK_TC) {
                ring->ccs = !ring->ccs;
            }
        }
    }
}

static int xhci_ring_chain_length(XHCIState *xhci, const XHCIRing *ring)
{
    XHCITRB trb;
    int length = 0;
    dma_addr_t dequeue = ring->dequeue;
    bool ccs = ring->ccs;
    /* hack to bundle together the two/three TDs that make a setup transfer */
    bool control_td_set = 0;
    uint32_t link_cnt = 0;

    do {
        TRBType type;
        if (dma_memory_read(xhci->as, dequeue, &trb, TRB_SIZE,
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA memory access failed!\n",
                          __func__);
            return -1;
        }
        le64_to_cpus(&trb.parameter);
        le32_to_cpus(&trb.status);
        le32_to_cpus(&trb.control);

        if ((trb.control & TRB_C) != ccs) {
            return -length;
        }

        type = TRB_TYPE(trb);

        if (type == TR_LINK) {
            if (++link_cnt > TRB_LINK_LIMIT) {
                return -length;
            }
            dequeue = xhci_mask64(trb.parameter);
            if (trb.control & TRB_LK_TC) {
                ccs = !ccs;
            }
            continue;
        }

        length += 1;
        dequeue += TRB_SIZE;

        if (type == TR_SETUP) {
            control_td_set = 1;
        } else if (type == TR_STATUS) {
            control_td_set = 0;
        }

        if (!control_td_set && !(trb.control & TRB_TR_CH)) {
            return length;
        }

        /*
         * According to the xHCI spec, Transfer Ring segments should have
         * a maximum size of 64 kB (see chapter "6 Data Structures")
         */
    } while (length < TRB_LINK_LIMIT * 65536 / TRB_SIZE);

    qemu_log_mask(LOG_GUEST_ERROR, "%s: exceeded maximum transfer ring size!\n",
                          __func__);

    return -1;
}

static void xhci_er_reset(XHCIState *xhci, int v)
{
    XHCIInterrupter *intr = &xhci->intr[v];
    XHCIEvRingSeg seg;
    dma_addr_t erstba = xhci_addr64(intr->erstba_low, intr->erstba_high);

    if (intr->erstsz == 0 || erstba == 0) {
        /* disabled */
        intr->er_start = 0;
        intr->er_size = 0;
        return;
    }
    /* cache the (sole) event ring segment location */
    if (intr->erstsz != 1) {
        DPRINTF("xhci: invalid value for ERSTSZ: %d\n", intr->erstsz);
        xhci_die(xhci);
        return;
    }
    if (dma_memory_read(xhci->as, erstba, &seg, sizeof(seg),
                    MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA memory access failed!\n",
                      __func__);
        xhci_die(xhci);
        return;
    }

    le32_to_cpus(&seg.addr_low);
    le32_to_cpus(&seg.addr_high);
    le32_to_cpus(&seg.size);
    if (seg.size < 16 || seg.size > 4096) {
        DPRINTF("xhci: invalid value for segment size: %d\n", seg.size);
        xhci_die(xhci);
        return;
    }
    intr->er_start = xhci_addr64(seg.addr_low, seg.addr_high);
    intr->er_size = seg.size;

    intr->er_ep_idx = 0;
    intr->er_pcs = 1;

    DPRINTF("xhci: event ring[%d]:" DMA_ADDR_FMT " [%d]\n",
            v, intr->er_start, intr->er_size);
}

static void xhci_run(XHCIState *xhci)
{
    trace_usb_xhci_run();
    xhci->usbsts &= ~USBSTS_HCH;
    xhci->mfindex_start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void xhci_stop(XHCIState *xhci)
{
    trace_usb_xhci_stop();
    xhci->usbsts |= USBSTS_HCH;
    xhci->crcr_low &= ~CRCR_CRR;
}

static XHCIStreamContext *xhci_alloc_stream_contexts(unsigned count,
                                                     dma_addr_t base)
{
    XHCIStreamContext *stctx;
    unsigned int i;

    stctx = g_new0(XHCIStreamContext, count);
    for (i = 0; i < count; i++) {
        stctx[i].pctx = base + i * 16;
        stctx[i].sct = -1;
    }
    return stctx;
}

static void xhci_reset_streams(XHCIEPContext *epctx)
{
    unsigned int i;

    for (i = 0; i < epctx->nr_pstreams; i++) {
        epctx->pstreams[i].sct = -1;
    }
}

static void xhci_alloc_streams(XHCIEPContext *epctx, dma_addr_t base)
{
    assert(epctx->pstreams == NULL);
    epctx->nr_pstreams = 2 << epctx->max_pstreams;
    epctx->pstreams = xhci_alloc_stream_contexts(epctx->nr_pstreams, base);
}

static void xhci_free_streams(XHCIEPContext *epctx)
{
    assert(epctx->pstreams != NULL);

    g_free(epctx->pstreams);
    epctx->pstreams = NULL;
    epctx->nr_pstreams = 0;
}

static int xhci_epmask_to_eps_with_streams(XHCIState *xhci,
                                           unsigned int slotid,
                                           uint32_t epmask,
                                           XHCIEPContext **epctxs,
                                           USBEndpoint **eps)
{
    XHCISlot *slot;
    XHCIEPContext *epctx;
    USBEndpoint *ep;
    int i, j;

    assert(slotid >= 1 && slotid <= xhci->numslots);

    slot = &xhci->slots[slotid - 1];

    for (i = 2, j = 0; i <= 31; i++) {
        if (!(epmask & (1u << i))) {
            continue;
        }

        epctx = slot->eps[i - 1];
        ep = xhci_epid_to_usbep(epctx);
        if (!epctx || !epctx->nr_pstreams || !ep) {
            continue;
        }

        if (epctxs) {
            epctxs[j] = epctx;
        }
        eps[j++] = ep;
    }
    return j;
}

static void xhci_free_device_streams(XHCIState *xhci, unsigned int slotid,
                                     uint32_t epmask)
{
    USBEndpoint *eps[30];
    int nr_eps;

    nr_eps = xhci_epmask_to_eps_with_streams(xhci, slotid, epmask, NULL, eps);
    if (nr_eps) {
        usb_device_free_streams(eps[0]->dev, eps, nr_eps);
    }
}

static TRBCCode xhci_alloc_device_streams(XHCIState *xhci, unsigned int slotid,
                                          uint32_t epmask)
{
    XHCIEPContext *epctxs[30];
    USBEndpoint *eps[30];
    int i, r, nr_eps, req_nr_streams, dev_max_streams;

    nr_eps = xhci_epmask_to_eps_with_streams(xhci, slotid, epmask, epctxs,
                                             eps);
    if (nr_eps == 0) {
        return CC_SUCCESS;
    }

    req_nr_streams = epctxs[0]->nr_pstreams;
    dev_max_streams = eps[0]->max_streams;

    for (i = 1; i < nr_eps; i++) {
        /*
         * HdG: I don't expect these to ever trigger, but if they do we need
         * to come up with another solution, ie group identical endpoints
         * together and make an usb_device_alloc_streams call per group.
         */
        if (epctxs[i]->nr_pstreams != req_nr_streams) {
            FIXME("guest streams config not identical for all eps");
            return CC_RESOURCE_ERROR;
        }
        if (eps[i]->max_streams != dev_max_streams) {
            FIXME("device streams config not identical for all eps");
            return CC_RESOURCE_ERROR;
        }
    }

    /*
     * max-streams in both the device descriptor and in the controller is a
     * power of 2. But stream id 0 is reserved, so if a device can do up to 4
     * streams the guest will ask for 5 rounded up to the next power of 2 which
     * becomes 8. For emulated devices usb_device_alloc_streams is a nop.
     *
     * For redirected devices however this is an issue, as there we must ask
     * the real xhci controller to alloc streams, and the host driver for the
     * real xhci controller will likely disallow allocating more streams then
     * the device can handle.
     *
     * So we limit the requested nr_streams to the maximum number the device
     * can handle.
     */
    if (req_nr_streams > dev_max_streams) {
        req_nr_streams = dev_max_streams;
    }

    r = usb_device_alloc_streams(eps[0]->dev, eps, nr_eps, req_nr_streams);
    if (r != 0) {
        DPRINTF("xhci: alloc streams failed\n");
        return CC_RESOURCE_ERROR;
    }

    return CC_SUCCESS;
}

static XHCIStreamContext *xhci_find_stream(XHCIEPContext *epctx,
                                           unsigned int streamid,
                                           uint32_t *cc_error)
{
    XHCIStreamContext *sctx;
    dma_addr_t base;
    uint32_t ctx[2], sct;

    assert(streamid != 0);
    if (epctx->lsa) {
        if (streamid >= epctx->nr_pstreams) {
            *cc_error = CC_INVALID_STREAM_ID_ERROR;
            return NULL;
        }
        sctx = epctx->pstreams + streamid;
    } else {
        fprintf(stderr, "xhci: FIXME: secondary streams not implemented yet");
        *cc_error = CC_INVALID_STREAM_TYPE_ERROR;
        return NULL;
    }

    if (sctx->sct == -1) {
        xhci_dma_read_u32s(epctx->xhci, sctx->pctx, ctx, sizeof(ctx));
        sct = (ctx[0] >> 1) & 0x07;
        if (epctx->lsa && sct != 1) {
            *cc_error = CC_INVALID_STREAM_TYPE_ERROR;
            return NULL;
        }
        sctx->sct = sct;
        base = xhci_addr64(ctx[0] & ~0xf, ctx[1]);
        xhci_ring_init(epctx->xhci, &sctx->ring, base);
    }
    return sctx;
}

static void xhci_set_ep_state(XHCIState *xhci, XHCIEPContext *epctx,
                              XHCIStreamContext *sctx, uint32_t state)
{
    XHCIRing *ring = NULL;
    uint32_t ctx[5];
    uint32_t ctx2[2];

    xhci_dma_read_u32s(xhci, epctx->pctx, ctx, sizeof(ctx));
    ctx[0] &= ~EP_STATE_MASK;
    ctx[0] |= state;

    /* update ring dequeue ptr */
    if (epctx->nr_pstreams) {
        if (sctx != NULL) {
            ring = &sctx->ring;
            xhci_dma_read_u32s(xhci, sctx->pctx, ctx2, sizeof(ctx2));
            ctx2[0] &= 0xe;
            ctx2[0] |= sctx->ring.dequeue | sctx->ring.ccs;
            ctx2[1] = (sctx->ring.dequeue >> 16) >> 16;
            xhci_dma_write_u32s(xhci, sctx->pctx, ctx2, sizeof(ctx2));
        }
    } else {
        ring = &epctx->ring;
    }
    if (ring) {
        ctx[2] = ring->dequeue | ring->ccs;
        ctx[3] = (ring->dequeue >> 16) >> 16;

        DPRINTF("xhci: set epctx: " DMA_ADDR_FMT " state=%d dequeue=%08x%08x\n",
                epctx->pctx, state, ctx[3], ctx[2]);
    }

    xhci_dma_write_u32s(xhci, epctx->pctx, ctx, sizeof(ctx));
    if (epctx->state != state) {
        trace_usb_xhci_ep_state(epctx->slotid, epctx->epid,
                                ep_state_name(epctx->state),
                                ep_state_name(state));
    }
    epctx->state = state;
}

static void xhci_ep_kick_timer(void *opaque)
{
    XHCIEPContext *epctx = opaque;
    xhci_kick_epctx(epctx, 0);
}

static XHCIEPContext *xhci_alloc_epctx(XHCIState *xhci,
                                       unsigned int slotid,
                                       unsigned int epid)
{
    XHCIEPContext *epctx;

    epctx = g_new0(XHCIEPContext, 1);
    epctx->xhci = xhci;
    epctx->slotid = slotid;
    epctx->epid = epid;

    QTAILQ_INIT(&epctx->transfers);
    epctx->kick_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, xhci_ep_kick_timer, epctx);

    return epctx;
}

static void xhci_init_epctx(XHCIEPContext *epctx,
                            dma_addr_t pctx, uint32_t *ctx)
{
    dma_addr_t dequeue;

    dequeue = xhci_addr64(ctx[2] & ~0xf, ctx[3]);

    epctx->type = (ctx[1] >> EP_TYPE_SHIFT) & EP_TYPE_MASK;
    epctx->pctx = pctx;
    epctx->max_psize = ctx[1]>>16;
    epctx->max_psize *= 1+((ctx[1]>>8)&0xff);
    epctx->max_pstreams = (ctx[0] >> 10) & epctx->xhci->max_pstreams_mask;
    epctx->lsa = (ctx[0] >> 15) & 1;
    if (epctx->max_pstreams) {
        xhci_alloc_streams(epctx, dequeue);
    } else {
        xhci_ring_init(epctx->xhci, &epctx->ring, dequeue);
        epctx->ring.ccs = ctx[2] & 1;
    }

    epctx->interval = 1 << ((ctx[0] >> 16) & 0xff);
}

static TRBCCode xhci_enable_ep(XHCIState *xhci, unsigned int slotid,
                               unsigned int epid, dma_addr_t pctx,
                               uint32_t *ctx)
{
    XHCISlot *slot;
    XHCIEPContext *epctx;

    trace_usb_xhci_ep_enable(slotid, epid);
    assert(slotid >= 1 && slotid <= xhci->numslots);
    assert(epid >= 1 && epid <= 31);

    slot = &xhci->slots[slotid-1];
    if (slot->eps[epid-1]) {
        xhci_disable_ep(xhci, slotid, epid);
    }

    epctx = xhci_alloc_epctx(xhci, slotid, epid);
    slot->eps[epid-1] = epctx;
    xhci_init_epctx(epctx, pctx, ctx);

    DPRINTF("xhci: endpoint %d.%d type is %d, max transaction (burst) "
            "size is %d\n", epid/2, epid%2, epctx->type, epctx->max_psize);

    epctx->mfindex_last = 0;

    epctx->state = EP_RUNNING;
    ctx[0] &= ~EP_STATE_MASK;
    ctx[0] |= EP_RUNNING;

    return CC_SUCCESS;
}

static XHCITransfer *xhci_ep_alloc_xfer(XHCIEPContext *epctx,
                                        uint32_t length)
{
    uint32_t limit = epctx->nr_pstreams + 16;
    XHCITransfer *xfer;

    if (epctx->xfer_count >= limit) {
        return NULL;
    }

    xfer = g_new0(XHCITransfer, 1);
    xfer->epctx = epctx;
    xfer->trbs = g_new(XHCITRB, length);
    xfer->trb_count = length;
    usb_packet_init(&xfer->packet);

    QTAILQ_INSERT_TAIL(&epctx->transfers, xfer, next);
    epctx->xfer_count++;

    return xfer;
}

static void xhci_ep_free_xfer(XHCITransfer *xfer)
{
    QTAILQ_REMOVE(&xfer->epctx->transfers, xfer, next);
    xfer->epctx->xfer_count--;

    usb_packet_cleanup(&xfer->packet);
    g_free(xfer->trbs);
    g_free(xfer);
}

static void xhci_xfer_unmap(XHCITransfer *xfer)
{
    usb_packet_unmap(&xfer->packet, &xfer->sgl);
    qemu_sglist_destroy(&xfer->sgl);
}

static int xhci_ep_nuke_one_xfer(XHCITransfer *t, TRBCCode report)
{
    int killed = 0;

    if (report && (t->running_async || t->running_retry)) {
        t->status = report;
        xhci_xfer_report(t);
    }

    if (t->running_async) {
        usb_cancel_packet(&t->packet);
        xhci_xfer_unmap(t);
        t->running_async = 0;
        killed = 1;
    }
    if (t->running_retry) {
        if (t->epctx) {
            t->epctx->retry = NULL;
            timer_del(t->epctx->kick_timer);
        }
        t->running_retry = 0;
        killed = 1;
    }
    g_free(t->trbs);

    t->trbs = NULL;
    t->trb_count = 0;

    return killed;
}

static int xhci_ep_nuke_xfers(XHCIState *xhci, unsigned int slotid,
                               unsigned int epid, TRBCCode report)
{
    XHCISlot *slot;
    XHCIEPContext *epctx;
    XHCITransfer *xfer;
    int killed = 0;
    USBEndpoint *ep = NULL;
    assert(slotid >= 1 && slotid <= xhci->numslots);
    assert(epid >= 1 && epid <= 31);

    DPRINTF("xhci_ep_nuke_xfers(%d, %d)\n", slotid, epid);

    slot = &xhci->slots[slotid-1];

    if (!slot->eps[epid-1]) {
        return 0;
    }

    epctx = slot->eps[epid-1];

    for (;;) {
        xfer = QTAILQ_FIRST(&epctx->transfers);
        if (xfer == NULL) {
            break;
        }
        killed += xhci_ep_nuke_one_xfer(xfer, report);
        if (killed) {
            report = 0; /* Only report once */
        }
        xhci_ep_free_xfer(xfer);
    }

    ep = xhci_epid_to_usbep(epctx);
    if (ep) {
        usb_device_ep_stopped(ep->dev, ep);
    }
    return killed;
}

static TRBCCode xhci_disable_ep(XHCIState *xhci, unsigned int slotid,
                               unsigned int epid)
{
    XHCISlot *slot;
    XHCIEPContext *epctx;

    trace_usb_xhci_ep_disable(slotid, epid);
    assert(slotid >= 1 && slotid <= xhci->numslots);
    assert(epid >= 1 && epid <= 31);

    slot = &xhci->slots[slotid-1];

    if (!slot->eps[epid-1]) {
        DPRINTF("xhci: slot %d ep %d already disabled\n", slotid, epid);
        return CC_SUCCESS;
    }

    xhci_ep_nuke_xfers(xhci, slotid, epid, 0);

    epctx = slot->eps[epid-1];

    if (epctx->nr_pstreams) {
        xhci_free_streams(epctx);
    }

    /* only touch guest RAM if we're not resetting the HC */
    if (xhci->dcbaap_low || xhci->dcbaap_high) {
        xhci_set_ep_state(xhci, epctx, NULL, EP_DISABLED);
    }

    timer_free(epctx->kick_timer);
    g_free(epctx);
    slot->eps[epid-1] = NULL;

    return CC_SUCCESS;
}

static TRBCCode xhci_stop_ep(XHCIState *xhci, unsigned int slotid,
                             unsigned int epid)
{
    XHCISlot *slot;
    XHCIEPContext *epctx;

    trace_usb_xhci_ep_stop(slotid, epid);
    assert(slotid >= 1 && slotid <= xhci->numslots);

    if (epid < 1 || epid > 31) {
        DPRINTF("xhci: bad ep %d\n", epid);
        return CC_TRB_ERROR;
    }

    slot = &xhci->slots[slotid-1];

    if (!slot->eps[epid-1]) {
        DPRINTF("xhci: slot %d ep %d not enabled\n", slotid, epid);
        return CC_EP_NOT_ENABLED_ERROR;
    }

    if (xhci_ep_nuke_xfers(xhci, slotid, epid, CC_STOPPED) > 0) {
        DPRINTF("xhci: FIXME: endpoint stopped w/ xfers running, "
                "data might be lost\n");
    }

    epctx = slot->eps[epid-1];

    xhci_set_ep_state(xhci, epctx, NULL, EP_STOPPED);

    if (epctx->nr_pstreams) {
        xhci_reset_streams(epctx);
    }

    return CC_SUCCESS;
}

static TRBCCode xhci_reset_ep(XHCIState *xhci, unsigned int slotid,
                              unsigned int epid)
{
    XHCISlot *slot;
    XHCIEPContext *epctx;

    trace_usb_xhci_ep_reset(slotid, epid);
    assert(slotid >= 1 && slotid <= xhci->numslots);

    if (epid < 1 || epid > 31) {
        DPRINTF("xhci: bad ep %d\n", epid);
        return CC_TRB_ERROR;
    }

    slot = &xhci->slots[slotid-1];

    if (!slot->eps[epid-1]) {
        DPRINTF("xhci: slot %d ep %d not enabled\n", slotid, epid);
        return CC_EP_NOT_ENABLED_ERROR;
    }

    epctx = slot->eps[epid-1];

    if (epctx->state != EP_HALTED) {
        DPRINTF("xhci: reset EP while EP %d not halted (%d)\n",
                epid, epctx->state);
        return CC_CONTEXT_STATE_ERROR;
    }

    if (xhci_ep_nuke_xfers(xhci, slotid, epid, 0) > 0) {
        DPRINTF("xhci: FIXME: endpoint reset w/ xfers running, "
                "data might be lost\n");
    }

    if (!xhci->slots[slotid-1].uport ||
        !xhci->slots[slotid-1].uport->dev ||
        !xhci->slots[slotid-1].uport->dev->attached) {
        return CC_USB_TRANSACTION_ERROR;
    }

    xhci_set_ep_state(xhci, epctx, NULL, EP_STOPPED);

    if (epctx->nr_pstreams) {
        xhci_reset_streams(epctx);
    }

    return CC_SUCCESS;
}

static TRBCCode xhci_set_ep_dequeue(XHCIState *xhci, unsigned int slotid,
                                    unsigned int epid, unsigned int streamid,
                                    uint64_t pdequeue)
{
    XHCISlot *slot;
    XHCIEPContext *epctx;
    XHCIStreamContext *sctx;
    dma_addr_t dequeue;

    assert(slotid >= 1 && slotid <= xhci->numslots);

    if (epid < 1 || epid > 31) {
        DPRINTF("xhci: bad ep %d\n", epid);
        return CC_TRB_ERROR;
    }

    trace_usb_xhci_ep_set_dequeue(slotid, epid, streamid, pdequeue);
    dequeue = xhci_mask64(pdequeue);

    slot = &xhci->slots[slotid-1];

    if (!slot->eps[epid-1]) {
        DPRINTF("xhci: slot %d ep %d not enabled\n", slotid, epid);
        return CC_EP_NOT_ENABLED_ERROR;
    }

    epctx = slot->eps[epid-1];

    if (epctx->state != EP_STOPPED) {
        DPRINTF("xhci: set EP dequeue pointer while EP %d not stopped\n", epid);
        return CC_CONTEXT_STATE_ERROR;
    }

    if (epctx->nr_pstreams) {
        uint32_t err;
        sctx = xhci_find_stream(epctx, streamid, &err);
        if (sctx == NULL) {
            return err;
        }
        xhci_ring_init(xhci, &sctx->ring, dequeue & ~0xf);
        sctx->ring.ccs = dequeue & 1;
    } else {
        sctx = NULL;
        xhci_ring_init(xhci, &epctx->ring, dequeue & ~0xF);
        epctx->ring.ccs = dequeue & 1;
    }

    xhci_set_ep_state(xhci, epctx, sctx, EP_STOPPED);

    return CC_SUCCESS;
}

static int xhci_xfer_create_sgl(XHCITransfer *xfer, int in_xfer)
{
    XHCIState *xhci = xfer->epctx->xhci;
    int i;

    xfer->int_req = false;
    qemu_sglist_init(&xfer->sgl, DEVICE(xhci), xfer->trb_count, xhci->as);
    for (i = 0; i < xfer->trb_count; i++) {
        XHCITRB *trb = &xfer->trbs[i];
        dma_addr_t addr;
        unsigned int chunk = 0;

        if (trb->control & TRB_TR_IOC) {
            xfer->int_req = true;
        }

        switch (TRB_TYPE(*trb)) {
        case TR_DATA:
            if ((!(trb->control & TRB_TR_DIR)) != (!in_xfer)) {
                DPRINTF("xhci: data direction mismatch for TR_DATA\n");
                goto err;
            }
            /* fallthrough */
        case TR_NORMAL:
        case TR_ISOCH:
            addr = xhci_mask64(trb->parameter);
            chunk = trb->status & 0x1ffff;
            if (trb->control & TRB_TR_IDT) {
                if (chunk > 8 || in_xfer) {
                    DPRINTF("xhci: invalid immediate data TRB\n");
                    goto err;
                }
                qemu_sglist_add(&xfer->sgl, trb->addr, chunk);
            } else {
                qemu_sglist_add(&xfer->sgl, addr, chunk);
            }
            break;
        }
    }

    return 0;

err:
    qemu_sglist_destroy(&xfer->sgl);
    xhci_die(xhci);
    return -1;
}

static void xhci_xfer_report(XHCITransfer *xfer)
{
    uint32_t edtla = 0;
    unsigned int left;
    bool reported = 0;
    bool shortpkt = 0;
    XHCIEvent event = {ER_TRANSFER, CC_SUCCESS};
    XHCIState *xhci = xfer->epctx->xhci;
    int i;

    left = xfer->packet.actual_length;

    for (i = 0; i < xfer->trb_count; i++) {
        XHCITRB *trb = &xfer->trbs[i];
        unsigned int chunk = 0;

        switch (TRB_TYPE(*trb)) {
        case TR_SETUP:
            chunk = trb->status & 0x1ffff;
            if (chunk > 8) {
                chunk = 8;
            }
            break;
        case TR_DATA:
        case TR_NORMAL:
        case TR_ISOCH:
            chunk = trb->status & 0x1ffff;
            if (chunk > left) {
                chunk = left;
                if (xfer->status == CC_SUCCESS) {
                    shortpkt = 1;
                }
            }
            left -= chunk;
            edtla += chunk;
            break;
        case TR_STATUS:
            reported = 0;
            shortpkt = 0;
            break;
        }

        if (!reported && ((trb->control & TRB_TR_IOC) ||
                          (shortpkt && (trb->control & TRB_TR_ISP)) ||
                          (xfer->status != CC_SUCCESS && left == 0))) {
            event.slotid = xfer->epctx->slotid;
            event.epid = xfer->epctx->epid;
            event.length = (trb->status & 0x1ffff) - chunk;
            event.flags = 0;
            event.ptr = trb->addr;
            if (xfer->status == CC_SUCCESS) {
                event.ccode = shortpkt ? CC_SHORT_PACKET : CC_SUCCESS;
            } else {
                event.ccode = xfer->status;
            }
            if (TRB_TYPE(*trb) == TR_EVDATA) {
                event.ptr = trb->parameter;
                event.flags |= TRB_EV_ED;
                event.length = edtla & 0xffffff;
                DPRINTF("xhci_xfer_data: EDTLA=%d\n", event.length);
                edtla = 0;
            }
            xhci_event(xhci, &event, TRB_INTR(*trb));
            reported = 1;
            if (xfer->status != CC_SUCCESS) {
                return;
            }
        }

        switch (TRB_TYPE(*trb)) {
        case TR_SETUP:
            reported = 0;
            shortpkt = 0;
            break;
        }

    }
}

static void xhci_stall_ep(XHCITransfer *xfer)
{
    XHCIEPContext *epctx = xfer->epctx;
    XHCIState *xhci = epctx->xhci;
    uint32_t err;
    XHCIStreamContext *sctx;

    if (epctx->type == ET_ISO_IN || epctx->type == ET_ISO_OUT) {
        /* never halt isoch endpoints, 4.10.2 */
        return;
    }

    if (epctx->nr_pstreams) {
        sctx = xhci_find_stream(epctx, xfer->streamid, &err);
        if (sctx == NULL) {
            return;
        }
        sctx->ring.dequeue = xfer->trbs[0].addr;
        sctx->ring.ccs = xfer->trbs[0].ccs;
        xhci_set_ep_state(xhci, epctx, sctx, EP_HALTED);
    } else {
        epctx->ring.dequeue = xfer->trbs[0].addr;
        epctx->ring.ccs = xfer->trbs[0].ccs;
        xhci_set_ep_state(xhci, epctx, NULL, EP_HALTED);
    }
}

static int xhci_setup_packet(XHCITransfer *xfer)
{
    USBEndpoint *ep;
    int dir;

    dir = xfer->in_xfer ? USB_TOKEN_IN : USB_TOKEN_OUT;

    if (xfer->packet.ep) {
        ep = xfer->packet.ep;
    } else {
        ep = xhci_epid_to_usbep(xfer->epctx);
        if (!ep) {
            DPRINTF("xhci: slot %d has no device\n",
                    xfer->epctx->slotid);
            return -1;
        }
    }

    xhci_xfer_create_sgl(xfer, dir == USB_TOKEN_IN); /* Also sets int_req */
    usb_packet_setup(&xfer->packet, dir, ep, xfer->streamid,
                     xfer->trbs[0].addr, false, xfer->int_req);
    if (usb_packet_map(&xfer->packet, &xfer->sgl)) {
        qemu_sglist_destroy(&xfer->sgl);
        return -1;
    }
    DPRINTF("xhci: setup packet pid 0x%x addr %d ep %d\n",
            xfer->packet.pid, ep->dev->addr, ep->nr);
    return 0;
}

static int xhci_try_complete_packet(XHCITransfer *xfer)
{
    if (xfer->packet.status == USB_RET_ASYNC) {
        trace_usb_xhci_xfer_async(xfer);
        xfer->running_async = 1;
        xfer->running_retry = 0;
        xfer->complete = 0;
        return 0;
    } else if (xfer->packet.status == USB_RET_NAK) {
        trace_usb_xhci_xfer_nak(xfer);
        xfer->running_async = 0;
        xfer->running_retry = 1;
        xfer->complete = 0;
        return 0;
    } else {
        xfer->running_async = 0;
        xfer->running_retry = 0;
        xfer->complete = 1;
        xhci_xfer_unmap(xfer);
    }

    if (xfer->packet.status == USB_RET_SUCCESS) {
        trace_usb_xhci_xfer_success(xfer, xfer->packet.actual_length);
        xfer->status = CC_SUCCESS;
        xhci_xfer_report(xfer);
        return 0;
    }

    /* error */
    trace_usb_xhci_xfer_error(xfer, xfer->packet.status);
    switch (xfer->packet.status) {
    case USB_RET_NODEV:
    case USB_RET_IOERROR:
        xfer->status = CC_USB_TRANSACTION_ERROR;
        xhci_xfer_report(xfer);
        xhci_stall_ep(xfer);
        break;
    case USB_RET_STALL:
        xfer->status = CC_STALL_ERROR;
        xhci_xfer_report(xfer);
        xhci_stall_ep(xfer);
        break;
    case USB_RET_BABBLE:
        xfer->status = CC_BABBLE_DETECTED;
        xhci_xfer_report(xfer);
        xhci_stall_ep(xfer);
        break;
    default:
        DPRINTF("%s: FIXME: status = %d\n", __func__,
                xfer->packet.status);
        FIXME("unhandled USB_RET_*");
    }
    return 0;
}

static int xhci_fire_ctl_transfer(XHCIState *xhci, XHCITransfer *xfer)
{
    XHCITRB *trb_setup, *trb_status;
    uint8_t bmRequestType;

    trb_setup = &xfer->trbs[0];
    trb_status = &xfer->trbs[xfer->trb_count-1];

    trace_usb_xhci_xfer_start(xfer, xfer->epctx->slotid,
                              xfer->epctx->epid, xfer->streamid);

    /* at most one Event Data TRB allowed after STATUS */
    if (TRB_TYPE(*trb_status) == TR_EVDATA && xfer->trb_count > 2) {
        trb_status--;
    }

    /* do some sanity checks */
    if (TRB_TYPE(*trb_setup) != TR_SETUP) {
        DPRINTF("xhci: ep0 first TD not SETUP: %d\n",
                TRB_TYPE(*trb_setup));
        return -1;
    }
    if (TRB_TYPE(*trb_status) != TR_STATUS) {
        DPRINTF("xhci: ep0 last TD not STATUS: %d\n",
                TRB_TYPE(*trb_status));
        return -1;
    }
    if (!(trb_setup->control & TRB_TR_IDT)) {
        DPRINTF("xhci: Setup TRB doesn't have IDT set\n");
        return -1;
    }
    if ((trb_setup->status & 0x1ffff) != 8) {
        DPRINTF("xhci: Setup TRB has bad length (%d)\n",
                (trb_setup->status & 0x1ffff));
        return -1;
    }

    bmRequestType = trb_setup->parameter;

    xfer->in_xfer = bmRequestType & USB_DIR_IN;
    xfer->iso_xfer = false;
    xfer->timed_xfer = false;

    if (xhci_setup_packet(xfer) < 0) {
        return -1;
    }
    xfer->packet.parameter = trb_setup->parameter;

    usb_handle_packet(xfer->packet.ep->dev, &xfer->packet);
    xhci_try_complete_packet(xfer);
    return 0;
}

static void xhci_calc_intr_kick(XHCIState *xhci, XHCITransfer *xfer,
                                XHCIEPContext *epctx, uint64_t mfindex)
{
    uint64_t asap = ((mfindex + epctx->interval - 1) &
                     ~(epctx->interval-1));
    uint64_t kick = epctx->mfindex_last + epctx->interval;

    assert(epctx->interval != 0);
    xfer->mfindex_kick = MAX(asap, kick);
}

static void xhci_calc_iso_kick(XHCIState *xhci, XHCITransfer *xfer,
                               XHCIEPContext *epctx, uint64_t mfindex)
{
    if (xfer->trbs[0].control & TRB_TR_SIA) {
        uint64_t asap = ((mfindex + epctx->interval - 1) &
                         ~(epctx->interval-1));
        if (asap >= epctx->mfindex_last &&
            asap <= epctx->mfindex_last + epctx->interval * 4) {
            xfer->mfindex_kick = epctx->mfindex_last + epctx->interval;
        } else {
            xfer->mfindex_kick = asap;
        }
    } else {
        xfer->mfindex_kick = ((xfer->trbs[0].control >> TRB_TR_FRAMEID_SHIFT)
                              & TRB_TR_FRAMEID_MASK) << 3;
        xfer->mfindex_kick |= mfindex & ~0x3fff;
        if (xfer->mfindex_kick + 0x100 < mfindex) {
            xfer->mfindex_kick += 0x4000;
        }
    }
}

static void xhci_check_intr_iso_kick(XHCIState *xhci, XHCITransfer *xfer,
                                     XHCIEPContext *epctx, uint64_t mfindex)
{
    if (xfer->mfindex_kick > mfindex) {
        timer_mod(epctx->kick_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                       (xfer->mfindex_kick - mfindex) * 125000);
        xfer->running_retry = 1;
    } else {
        epctx->mfindex_last = xfer->mfindex_kick;
        timer_del(epctx->kick_timer);
        xfer->running_retry = 0;
    }
}


static int xhci_submit(XHCIState *xhci, XHCITransfer *xfer, XHCIEPContext *epctx)
{
    uint64_t mfindex;

    DPRINTF("xhci_submit(slotid=%d,epid=%d)\n", epctx->slotid, epctx->epid);

    xfer->in_xfer = epctx->type>>2;

    switch(epctx->type) {
    case ET_INTR_OUT:
    case ET_INTR_IN:
        xfer->pkts = 0;
        xfer->iso_xfer = false;
        xfer->timed_xfer = true;
        mfindex = xhci_mfindex_get(xhci);
        xhci_calc_intr_kick(xhci, xfer, epctx, mfindex);
        xhci_check_intr_iso_kick(xhci, xfer, epctx, mfindex);
        if (xfer->running_retry) {
            return -1;
        }
        break;
    case ET_BULK_OUT:
    case ET_BULK_IN:
        xfer->pkts = 0;
        xfer->iso_xfer = false;
        xfer->timed_xfer = false;
        break;
    case ET_ISO_OUT:
    case ET_ISO_IN:
        xfer->pkts = 1;
        xfer->iso_xfer = true;
        xfer->timed_xfer = true;
        mfindex = xhci_mfindex_get(xhci);
        xhci_calc_iso_kick(xhci, xfer, epctx, mfindex);
        xhci_check_intr_iso_kick(xhci, xfer, epctx, mfindex);
        if (xfer->running_retry) {
            return -1;
        }
        break;
    default:
        trace_usb_xhci_unimplemented("endpoint type", epctx->type);
        return -1;
    }

    if (xhci_setup_packet(xfer) < 0) {
        return -1;
    }
    usb_handle_packet(xfer->packet.ep->dev, &xfer->packet);
    xhci_try_complete_packet(xfer);
    return 0;
}

static int xhci_fire_transfer(XHCIState *xhci, XHCITransfer *xfer, XHCIEPContext *epctx)
{
    trace_usb_xhci_xfer_start(xfer, xfer->epctx->slotid,
                              xfer->epctx->epid, xfer->streamid);
    return xhci_submit(xhci, xfer, epctx);
}

static void xhci_kick_ep(XHCIState *xhci, unsigned int slotid,
                         unsigned int epid, unsigned int streamid)
{
    XHCIEPContext *epctx;

    assert(slotid >= 1 && slotid <= xhci->numslots);
    assert(epid >= 1 && epid <= 31);

    if (!xhci->slots[slotid-1].enabled) {
        DPRINTF("xhci: xhci_kick_ep for disabled slot %d\n", slotid);
        return;
    }
    epctx = xhci->slots[slotid-1].eps[epid-1];
    if (!epctx) {
        DPRINTF("xhci: xhci_kick_ep for disabled endpoint %d,%d\n",
                epid, slotid);
        return;
    }

    if (epctx->kick_active) {
        return;
    }
    xhci_kick_epctx(epctx, streamid);
}

static bool xhci_slot_ok(XHCIState *xhci, int slotid)
{
    return (xhci->slots[slotid - 1].uport &&
            xhci->slots[slotid - 1].uport->dev &&
            xhci->slots[slotid - 1].uport->dev->attached);
}

static void xhci_kick_epctx(XHCIEPContext *epctx, unsigned int streamid)
{
    XHCIState *xhci = epctx->xhci;
    XHCIStreamContext *stctx = NULL;
    XHCITransfer *xfer;
    XHCIRing *ring;
    USBEndpoint *ep = NULL;
    uint64_t mfindex;
    unsigned int count = 0;
    int length;
    int i;

    trace_usb_xhci_ep_kick(epctx->slotid, epctx->epid, streamid);
    assert(!epctx->kick_active);

    /* If the device has been detached, but the guest has not noticed this
       yet the 2 above checks will succeed, but we must NOT continue */
    if (!xhci_slot_ok(xhci, epctx->slotid)) {
        return;
    }

    if (epctx->retry) {
        xfer = epctx->retry;

        trace_usb_xhci_xfer_retry(xfer);
        assert(xfer->running_retry);
        if (xfer->timed_xfer) {
            /* time to kick the transfer? */
            mfindex = xhci_mfindex_get(xhci);
            xhci_check_intr_iso_kick(xhci, xfer, epctx, mfindex);
            if (xfer->running_retry) {
                return;
            }
            xfer->timed_xfer = 0;
            xfer->running_retry = 1;
        }
        if (xfer->iso_xfer) {
            /* retry iso transfer */
            if (xhci_setup_packet(xfer) < 0) {
                return;
            }
            usb_handle_packet(xfer->packet.ep->dev, &xfer->packet);
            assert(xfer->packet.status != USB_RET_NAK);
            xhci_try_complete_packet(xfer);
        } else {
            /* retry nak'ed transfer */
            if (xhci_setup_packet(xfer) < 0) {
                return;
            }
            usb_handle_packet(xfer->packet.ep->dev, &xfer->packet);
            if (xfer->packet.status == USB_RET_NAK) {
                xhci_xfer_unmap(xfer);
                return;
            }
            xhci_try_complete_packet(xfer);
        }
        assert(!xfer->running_retry);
        if (xfer->complete) {
            /* update ring dequeue ptr */
            xhci_set_ep_state(xhci, epctx, stctx, epctx->state);
            xhci_ep_free_xfer(epctx->retry);
        }
        epctx->retry = NULL;
    }

    if (epctx->state == EP_HALTED) {
        DPRINTF("xhci: ep halted, not running schedule\n");
        return;
    }


    if (epctx->nr_pstreams) {
        uint32_t err;
        stctx = xhci_find_stream(epctx, streamid, &err);
        if (stctx == NULL) {
            return;
        }
        ring = &stctx->ring;
        xhci_set_ep_state(xhci, epctx, stctx, EP_RUNNING);
    } else {
        ring = &epctx->ring;
        streamid = 0;
        xhci_set_ep_state(xhci, epctx, NULL, EP_RUNNING);
    }
    if (!ring->dequeue) {
        return;
    }

    epctx->kick_active++;
    while (1) {
        length = xhci_ring_chain_length(xhci, ring);
        if (length <= 0) {
            if (epctx->type == ET_ISO_OUT || epctx->type == ET_ISO_IN) {
                /* 4.10.3.1 */
                XHCIEvent ev = { ER_TRANSFER };
                ev.ccode  = epctx->type == ET_ISO_IN ?
                    CC_RING_OVERRUN : CC_RING_UNDERRUN;
                ev.slotid = epctx->slotid;
                ev.epid   = epctx->epid;
                ev.ptr    = epctx->ring.dequeue;
                xhci_event(xhci, &ev, xhci->slots[epctx->slotid-1].intr);
            }
            break;
        }
        xfer = xhci_ep_alloc_xfer(epctx, length);
        if (xfer == NULL) {
            break;
        }

        for (i = 0; i < length; i++) {
            TRBType type;
            type = xhci_ring_fetch(xhci, ring, &xfer->trbs[i], NULL);
            if (!type) {
                xhci_die(xhci);
                xhci_ep_free_xfer(xfer);
                epctx->kick_active--;
                return;
            }
        }
        xfer->streamid = streamid;

        if (epctx->epid == 1) {
            xhci_fire_ctl_transfer(xhci, xfer);
        } else {
            xhci_fire_transfer(xhci, xfer, epctx);
        }
        if (!xhci_slot_ok(xhci, epctx->slotid)) {
            /* surprise removal -> stop processing */
            break;
        }
        if (xfer->complete) {
            /* update ring dequeue ptr */
            xhci_set_ep_state(xhci, epctx, stctx, epctx->state);
            xhci_ep_free_xfer(xfer);
            xfer = NULL;
        }

        if (epctx->state == EP_HALTED) {
            break;
        }
        if (xfer != NULL && xfer->running_retry) {
            DPRINTF("xhci: xfer nacked, stopping schedule\n");
            epctx->retry = xfer;
            xhci_xfer_unmap(xfer);
            break;
        }
        if (count++ > TRANSFER_LIMIT) {
            trace_usb_xhci_enforced_limit("transfers");
            break;
        }
    }
    epctx->kick_active--;

    ep = xhci_epid_to_usbep(epctx);
    if (ep) {
        usb_device_flush_ep_queue(ep->dev, ep);
    }
}

static TRBCCode xhci_enable_slot(XHCIState *xhci, unsigned int slotid)
{
    trace_usb_xhci_slot_enable(slotid);
    assert(slotid >= 1 && slotid <= xhci->numslots);
    xhci->slots[slotid-1].enabled = 1;
    xhci->slots[slotid-1].uport = NULL;
    memset(xhci->slots[slotid-1].eps, 0, sizeof(XHCIEPContext*)*31);

    return CC_SUCCESS;
}

static TRBCCode xhci_disable_slot(XHCIState *xhci, unsigned int slotid)
{
    int i;

    trace_usb_xhci_slot_disable(slotid);
    assert(slotid >= 1 && slotid <= xhci->numslots);

    for (i = 1; i <= 31; i++) {
        if (xhci->slots[slotid-1].eps[i-1]) {
            xhci_disable_ep(xhci, slotid, i);
        }
    }

    xhci->slots[slotid-1].enabled = 0;
    xhci->slots[slotid-1].addressed = 0;
    xhci->slots[slotid-1].uport = NULL;
    xhci->slots[slotid-1].intr = 0;
    return CC_SUCCESS;
}

static USBPort *xhci_lookup_uport(XHCIState *xhci, uint32_t *slot_ctx)
{
    USBPort *uport;
    char path[32];
    int i, pos, port;

    port = (slot_ctx[1]>>16) & 0xFF;
    if (port < 1 || port > xhci->numports) {
        return NULL;
    }
    port = xhci->ports[port-1].uport->index+1;
    pos = snprintf(path, sizeof(path), "%d", port);
    for (i = 0; i < 5; i++) {
        port = (slot_ctx[0] >> 4*i) & 0x0f;
        if (!port) {
            break;
        }
        pos += snprintf(path + pos, sizeof(path) - pos, ".%d", port);
    }

    QTAILQ_FOREACH(uport, &xhci->bus.used, next) {
        if (strcmp(uport->path, path) == 0) {
            return uport;
        }
    }
    return NULL;
}

static TRBCCode xhci_address_slot(XHCIState *xhci, unsigned int slotid,
                                  uint64_t pictx, bool bsr)
{
    XHCISlot *slot;
    USBPort *uport;
    USBDevice *dev;
    dma_addr_t ictx, octx, dcbaap;
    uint64_t poctx;
    uint32_t ictl_ctx[2];
    uint32_t slot_ctx[4];
    uint32_t ep0_ctx[5];
    int i;
    TRBCCode res;

    assert(slotid >= 1 && slotid <= xhci->numslots);

    dcbaap = xhci_addr64(xhci->dcbaap_low, xhci->dcbaap_high);
    ldq_le_dma(xhci->as, dcbaap + 8 * slotid, &poctx, MEMTXATTRS_UNSPECIFIED);
    ictx = xhci_mask64(pictx);
    octx = xhci_mask64(poctx);

    DPRINTF("xhci: input context at "DMA_ADDR_FMT"\n", ictx);
    DPRINTF("xhci: output context at "DMA_ADDR_FMT"\n", octx);

    xhci_dma_read_u32s(xhci, ictx, ictl_ctx, sizeof(ictl_ctx));

    if (ictl_ctx[0] != 0x0 || ictl_ctx[1] != 0x3) {
        DPRINTF("xhci: invalid input context control %08x %08x\n",
                ictl_ctx[0], ictl_ctx[1]);
        return CC_TRB_ERROR;
    }

    xhci_dma_read_u32s(xhci, ictx+32, slot_ctx, sizeof(slot_ctx));
    xhci_dma_read_u32s(xhci, ictx+64, ep0_ctx, sizeof(ep0_ctx));

    DPRINTF("xhci: input slot context: %08x %08x %08x %08x\n",
            slot_ctx[0], slot_ctx[1], slot_ctx[2], slot_ctx[3]);

    DPRINTF("xhci: input ep0 context: %08x %08x %08x %08x %08x\n",
            ep0_ctx[0], ep0_ctx[1], ep0_ctx[2], ep0_ctx[3], ep0_ctx[4]);

    uport = xhci_lookup_uport(xhci, slot_ctx);
    if (uport == NULL) {
        DPRINTF("xhci: port not found\n");
        return CC_TRB_ERROR;
    }
    trace_usb_xhci_slot_address(slotid, uport->path);

    dev = uport->dev;
    if (!dev || !dev->attached) {
        DPRINTF("xhci: port %s not connected\n", uport->path);
        return CC_USB_TRANSACTION_ERROR;
    }

    for (i = 0; i < xhci->numslots; i++) {
        if (i == slotid-1) {
            continue;
        }
        if (xhci->slots[i].uport == uport) {
            DPRINTF("xhci: port %s already assigned to slot %d\n",
                    uport->path, i+1);
            return CC_TRB_ERROR;
        }
    }

    slot = &xhci->slots[slotid-1];
    slot->uport = uport;
    slot->ctx = octx;
    slot->intr = get_field(slot_ctx[2], TRB_INTR);

    /* Make sure device is in USB_STATE_DEFAULT state */
    usb_device_reset(dev);
    if (bsr) {
        slot_ctx[3] = SLOT_DEFAULT << SLOT_STATE_SHIFT;
    } else {
        USBPacket p;
        uint8_t buf[1];

        slot_ctx[3] = (SLOT_ADDRESSED << SLOT_STATE_SHIFT) | slotid;
        memset(&p, 0, sizeof(p));
        usb_packet_addbuf(&p, buf, sizeof(buf));
        usb_packet_setup(&p, USB_TOKEN_OUT,
                         usb_ep_get(dev, USB_TOKEN_OUT, 0), 0,
                         0, false, false);
        usb_device_handle_control(dev, &p,
                                  DeviceOutRequest | USB_REQ_SET_ADDRESS,
                                  slotid, 0, 0, NULL);
        assert(p.status != USB_RET_ASYNC);
        usb_packet_cleanup(&p);
    }

    res = xhci_enable_ep(xhci, slotid, 1, octx+32, ep0_ctx);

    DPRINTF("xhci: output slot context: %08x %08x %08x %08x\n",
            slot_ctx[0], slot_ctx[1], slot_ctx[2], slot_ctx[3]);
    DPRINTF("xhci: output ep0 context: %08x %08x %08x %08x %08x\n",
            ep0_ctx[0], ep0_ctx[1], ep0_ctx[2], ep0_ctx[3], ep0_ctx[4]);

    xhci_dma_write_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));
    xhci_dma_write_u32s(xhci, octx+32, ep0_ctx, sizeof(ep0_ctx));

    xhci->slots[slotid-1].addressed = 1;
    return res;
}


static TRBCCode xhci_configure_slot(XHCIState *xhci, unsigned int slotid,
                                  uint64_t pictx, bool dc)
{
    dma_addr_t ictx, octx;
    uint32_t ictl_ctx[2];
    uint32_t slot_ctx[4];
    uint32_t islot_ctx[4];
    uint32_t ep_ctx[5];
    int i;
    TRBCCode res;

    trace_usb_xhci_slot_configure(slotid);
    assert(slotid >= 1 && slotid <= xhci->numslots);

    ictx = xhci_mask64(pictx);
    octx = xhci->slots[slotid-1].ctx;

    DPRINTF("xhci: input context at "DMA_ADDR_FMT"\n", ictx);
    DPRINTF("xhci: output context at "DMA_ADDR_FMT"\n", octx);

    if (dc) {
        for (i = 2; i <= 31; i++) {
            if (xhci->slots[slotid-1].eps[i-1]) {
                xhci_disable_ep(xhci, slotid, i);
            }
        }

        xhci_dma_read_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));
        slot_ctx[3] &= ~(SLOT_STATE_MASK << SLOT_STATE_SHIFT);
        slot_ctx[3] |= SLOT_ADDRESSED << SLOT_STATE_SHIFT;
        DPRINTF("xhci: output slot context: %08x %08x %08x %08x\n",
                slot_ctx[0], slot_ctx[1], slot_ctx[2], slot_ctx[3]);
        xhci_dma_write_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));

        return CC_SUCCESS;
    }

    xhci_dma_read_u32s(xhci, ictx, ictl_ctx, sizeof(ictl_ctx));

    if ((ictl_ctx[0] & 0x3) != 0x0 || (ictl_ctx[1] & 0x3) != 0x1) {
        DPRINTF("xhci: invalid input context control %08x %08x\n",
                ictl_ctx[0], ictl_ctx[1]);
        return CC_TRB_ERROR;
    }

    xhci_dma_read_u32s(xhci, ictx+32, islot_ctx, sizeof(islot_ctx));
    xhci_dma_read_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));

    if (SLOT_STATE(slot_ctx[3]) < SLOT_ADDRESSED) {
        DPRINTF("xhci: invalid slot state %08x\n", slot_ctx[3]);
        return CC_CONTEXT_STATE_ERROR;
    }

    xhci_free_device_streams(xhci, slotid, ictl_ctx[0] | ictl_ctx[1]);

    for (i = 2; i <= 31; i++) {
        if (ictl_ctx[0] & (1<<i)) {
            xhci_disable_ep(xhci, slotid, i);
        }
        if (ictl_ctx[1] & (1<<i)) {
            xhci_dma_read_u32s(xhci, ictx+32+(32*i), ep_ctx, sizeof(ep_ctx));
            DPRINTF("xhci: input ep%d.%d context: %08x %08x %08x %08x %08x\n",
                    i/2, i%2, ep_ctx[0], ep_ctx[1], ep_ctx[2],
                    ep_ctx[3], ep_ctx[4]);
            xhci_disable_ep(xhci, slotid, i);
            res = xhci_enable_ep(xhci, slotid, i, octx+(32*i), ep_ctx);
            if (res != CC_SUCCESS) {
                return res;
            }
            DPRINTF("xhci: output ep%d.%d context: %08x %08x %08x %08x %08x\n",
                    i/2, i%2, ep_ctx[0], ep_ctx[1], ep_ctx[2],
                    ep_ctx[3], ep_ctx[4]);
            xhci_dma_write_u32s(xhci, octx+(32*i), ep_ctx, sizeof(ep_ctx));
        }
    }

    res = xhci_alloc_device_streams(xhci, slotid, ictl_ctx[1]);
    if (res != CC_SUCCESS) {
        for (i = 2; i <= 31; i++) {
            if (ictl_ctx[1] & (1u << i)) {
                xhci_disable_ep(xhci, slotid, i);
            }
        }
        return res;
    }

    slot_ctx[3] &= ~(SLOT_STATE_MASK << SLOT_STATE_SHIFT);
    slot_ctx[3] |= SLOT_CONFIGURED << SLOT_STATE_SHIFT;
    slot_ctx[0] &= ~(SLOT_CONTEXT_ENTRIES_MASK << SLOT_CONTEXT_ENTRIES_SHIFT);
    slot_ctx[0] |= islot_ctx[0] & (SLOT_CONTEXT_ENTRIES_MASK <<
                                   SLOT_CONTEXT_ENTRIES_SHIFT);
    DPRINTF("xhci: output slot context: %08x %08x %08x %08x\n",
            slot_ctx[0], slot_ctx[1], slot_ctx[2], slot_ctx[3]);

    xhci_dma_write_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));

    return CC_SUCCESS;
}


static TRBCCode xhci_evaluate_slot(XHCIState *xhci, unsigned int slotid,
                                   uint64_t pictx)
{
    dma_addr_t ictx, octx;
    uint32_t ictl_ctx[2];
    uint32_t iep0_ctx[5];
    uint32_t ep0_ctx[5];
    uint32_t islot_ctx[4];
    uint32_t slot_ctx[4];

    trace_usb_xhci_slot_evaluate(slotid);
    assert(slotid >= 1 && slotid <= xhci->numslots);

    ictx = xhci_mask64(pictx);
    octx = xhci->slots[slotid-1].ctx;

    DPRINTF("xhci: input context at "DMA_ADDR_FMT"\n", ictx);
    DPRINTF("xhci: output context at "DMA_ADDR_FMT"\n", octx);

    xhci_dma_read_u32s(xhci, ictx, ictl_ctx, sizeof(ictl_ctx));

    if (ictl_ctx[0] != 0x0 || ictl_ctx[1] & ~0x3) {
        DPRINTF("xhci: invalid input context control %08x %08x\n",
                ictl_ctx[0], ictl_ctx[1]);
        return CC_TRB_ERROR;
    }

    if (ictl_ctx[1] & 0x1) {
        xhci_dma_read_u32s(xhci, ictx+32, islot_ctx, sizeof(islot_ctx));

        DPRINTF("xhci: input slot context: %08x %08x %08x %08x\n",
                islot_ctx[0], islot_ctx[1], islot_ctx[2], islot_ctx[3]);

        xhci_dma_read_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));

        slot_ctx[1] &= ~0xFFFF; /* max exit latency */
        slot_ctx[1] |= islot_ctx[1] & 0xFFFF;
        /* update interrupter target field */
        xhci->slots[slotid-1].intr = get_field(islot_ctx[2], TRB_INTR);
        set_field(&slot_ctx[2], xhci->slots[slotid-1].intr, TRB_INTR);

        DPRINTF("xhci: output slot context: %08x %08x %08x %08x\n",
                slot_ctx[0], slot_ctx[1], slot_ctx[2], slot_ctx[3]);

        xhci_dma_write_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));
    }

    if (ictl_ctx[1] & 0x2) {
        xhci_dma_read_u32s(xhci, ictx+64, iep0_ctx, sizeof(iep0_ctx));

        DPRINTF("xhci: input ep0 context: %08x %08x %08x %08x %08x\n",
                iep0_ctx[0], iep0_ctx[1], iep0_ctx[2],
                iep0_ctx[3], iep0_ctx[4]);

        xhci_dma_read_u32s(xhci, octx+32, ep0_ctx, sizeof(ep0_ctx));

        ep0_ctx[1] &= ~0xFFFF0000; /* max packet size*/
        ep0_ctx[1] |= iep0_ctx[1] & 0xFFFF0000;

        DPRINTF("xhci: output ep0 context: %08x %08x %08x %08x %08x\n",
                ep0_ctx[0], ep0_ctx[1], ep0_ctx[2], ep0_ctx[3], ep0_ctx[4]);

        xhci_dma_write_u32s(xhci, octx+32, ep0_ctx, sizeof(ep0_ctx));
    }

    return CC_SUCCESS;
}

static TRBCCode xhci_reset_slot(XHCIState *xhci, unsigned int slotid)
{
    uint32_t slot_ctx[4];
    dma_addr_t octx;
    int i;

    trace_usb_xhci_slot_reset(slotid);
    assert(slotid >= 1 && slotid <= xhci->numslots);

    octx = xhci->slots[slotid-1].ctx;

    DPRINTF("xhci: output context at "DMA_ADDR_FMT"\n", octx);

    for (i = 2; i <= 31; i++) {
        if (xhci->slots[slotid-1].eps[i-1]) {
            xhci_disable_ep(xhci, slotid, i);
        }
    }

    xhci_dma_read_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));
    slot_ctx[3] &= ~(SLOT_STATE_MASK << SLOT_STATE_SHIFT);
    slot_ctx[3] |= SLOT_DEFAULT << SLOT_STATE_SHIFT;
    DPRINTF("xhci: output slot context: %08x %08x %08x %08x\n",
            slot_ctx[0], slot_ctx[1], slot_ctx[2], slot_ctx[3]);
    xhci_dma_write_u32s(xhci, octx, slot_ctx, sizeof(slot_ctx));

    return CC_SUCCESS;
}

static unsigned int xhci_get_slot(XHCIState *xhci, XHCIEvent *event, XHCITRB *trb)
{
    unsigned int slotid;
    slotid = (trb->control >> TRB_CR_SLOTID_SHIFT) & TRB_CR_SLOTID_MASK;
    if (slotid < 1 || slotid > xhci->numslots) {
        DPRINTF("xhci: bad slot id %d\n", slotid);
        event->ccode = CC_TRB_ERROR;
        return 0;
    } else if (!xhci->slots[slotid-1].enabled) {
        DPRINTF("xhci: slot id %d not enabled\n", slotid);
        event->ccode = CC_SLOT_NOT_ENABLED_ERROR;
        return 0;
    }
    return slotid;
}

/* cleanup slot state on usb device detach */
static void xhci_detach_slot(XHCIState *xhci, USBPort *uport)
{
    int slot, ep;

    for (slot = 0; slot < xhci->numslots; slot++) {
        if (xhci->slots[slot].uport == uport) {
            break;
        }
    }
    if (slot == xhci->numslots) {
        return;
    }

    for (ep = 0; ep < 31; ep++) {
        if (xhci->slots[slot].eps[ep]) {
            xhci_ep_nuke_xfers(xhci, slot + 1, ep + 1, 0);
        }
    }
    xhci->slots[slot].uport = NULL;
}

static TRBCCode xhci_get_port_bandwidth(XHCIState *xhci, uint64_t pctx)
{
    dma_addr_t ctx;

    DPRINTF("xhci_get_port_bandwidth()\n");

    ctx = xhci_mask64(pctx);

    DPRINTF("xhci: bandwidth context at "DMA_ADDR_FMT"\n", ctx);

    /* TODO: actually implement real values here. This is 80% for all ports. */
    if (stb_dma(xhci->as, ctx, 0, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK ||
        dma_memory_set(xhci->as, ctx + 1, 80, xhci->numports,
                       MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: DMA memory write failed!\n",
                      __func__);
        return CC_TRB_ERROR;
    }

    return CC_SUCCESS;
}

static uint32_t rotl(uint32_t v, unsigned count)
{
    count &= 31;
    return (v << count) | (v >> (32 - count));
}


static uint32_t xhci_nec_challenge(uint32_t hi, uint32_t lo)
{
    uint32_t val;
    val = rotl(lo - 0x49434878, 32 - ((hi>>8) & 0x1F));
    val += rotl(lo + 0x49434878, hi & 0x1F);
    val -= rotl(hi ^ 0x49434878, (lo >> 16) & 0x1F);
    return ~val;
}

static void xhci_process_commands(XHCIState *xhci)
{
    XHCITRB trb;
    TRBType type;
    XHCIEvent event = {ER_COMMAND_COMPLETE, CC_SUCCESS};
    dma_addr_t addr;
    unsigned int i, slotid = 0, count = 0;

    DPRINTF("xhci_process_commands()\n");
    if (!xhci_running(xhci)) {
        DPRINTF("xhci_process_commands() called while xHC stopped or paused\n");
        return;
    }

    xhci->crcr_low |= CRCR_CRR;

    while ((type = xhci_ring_fetch(xhci, &xhci->cmd_ring, &trb, &addr))) {
        event.ptr = addr;
        switch (type) {
        case CR_ENABLE_SLOT:
            for (i = 0; i < xhci->numslots; i++) {
                if (!xhci->slots[i].enabled) {
                    break;
                }
            }
            if (i >= xhci->numslots) {
                DPRINTF("xhci: no device slots available\n");
                event.ccode = CC_NO_SLOTS_ERROR;
            } else {
                slotid = i+1;
                event.ccode = xhci_enable_slot(xhci, slotid);
            }
            break;
        case CR_DISABLE_SLOT:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                event.ccode = xhci_disable_slot(xhci, slotid);
            }
            break;
        case CR_ADDRESS_DEVICE:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                event.ccode = xhci_address_slot(xhci, slotid, trb.parameter,
                                                trb.control & TRB_CR_BSR);
            }
            break;
        case CR_CONFIGURE_ENDPOINT:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                event.ccode = xhci_configure_slot(xhci, slotid, trb.parameter,
                                                  trb.control & TRB_CR_DC);
            }
            break;
        case CR_EVALUATE_CONTEXT:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                event.ccode = xhci_evaluate_slot(xhci, slotid, trb.parameter);
            }
            break;
        case CR_STOP_ENDPOINT:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                unsigned int epid = (trb.control >> TRB_CR_EPID_SHIFT)
                    & TRB_CR_EPID_MASK;
                event.ccode = xhci_stop_ep(xhci, slotid, epid);
            }
            break;
        case CR_RESET_ENDPOINT:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                unsigned int epid = (trb.control >> TRB_CR_EPID_SHIFT)
                    & TRB_CR_EPID_MASK;
                event.ccode = xhci_reset_ep(xhci, slotid, epid);
            }
            break;
        case CR_SET_TR_DEQUEUE:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                unsigned int epid = (trb.control >> TRB_CR_EPID_SHIFT)
                    & TRB_CR_EPID_MASK;
                unsigned int streamid = (trb.status >> 16) & 0xffff;
                event.ccode = xhci_set_ep_dequeue(xhci, slotid,
                                                  epid, streamid,
                                                  trb.parameter);
            }
            break;
        case CR_RESET_DEVICE:
            slotid = xhci_get_slot(xhci, &event, &trb);
            if (slotid) {
                event.ccode = xhci_reset_slot(xhci, slotid);
            }
            break;
        case CR_GET_PORT_BANDWIDTH:
            event.ccode = xhci_get_port_bandwidth(xhci, trb.parameter);
            break;
        case CR_NOOP:
            event.ccode = CC_SUCCESS;
            break;
        case CR_VENDOR_NEC_FIRMWARE_REVISION:
            if (xhci->nec_quirks) {
                event.type = 48; /* NEC reply */
                event.length = 0x3034;
            } else {
                event.ccode = CC_TRB_ERROR;
            }
            break;
        case CR_VENDOR_NEC_CHALLENGE_RESPONSE:
            if (xhci->nec_quirks) {
                uint32_t chi = trb.parameter >> 32;
                uint32_t clo = trb.parameter;
                uint32_t val = xhci_nec_challenge(chi, clo);
                event.length = val & 0xFFFF;
                event.epid = val >> 16;
                slotid = val >> 24;
                event.type = 48; /* NEC reply */
            } else {
                event.ccode = CC_TRB_ERROR;
            }
            break;
        default:
            trace_usb_xhci_unimplemented("command", type);
            event.ccode = CC_TRB_ERROR;
            break;
        }
        event.slotid = slotid;
        xhci_event(xhci, &event, 0);

        if (count++ > COMMAND_LIMIT) {
            trace_usb_xhci_enforced_limit("commands");
            return;
        }
    }
}

static bool xhci_port_have_device(XHCIPort *port)
{
    if (!port->uport->dev || !port->uport->dev->attached) {
        return false; /* no device present */
    }
    if (!((1 << port->uport->dev->speed) & port->speedmask)) {
        return false; /* speed mismatch */
    }
    return true;
}

static void xhci_port_notify(XHCIPort *port, uint32_t bits)
{
    XHCIEvent ev = { ER_PORT_STATUS_CHANGE, CC_SUCCESS,
                     port->portnr << 24 };

    if ((port->portsc & bits) == bits) {
        return;
    }
    trace_usb_xhci_port_notify(port->portnr, bits);
    port->portsc |= bits;
    if (!xhci_running(port->xhci)) {
        return;
    }
    xhci_event(port->xhci, &ev, 0);
}

static void xhci_port_update(XHCIPort *port, int is_detach)
{
    uint32_t pls = PLS_RX_DETECT;

    assert(port);
    port->portsc = PORTSC_PP;
    if (!is_detach && xhci_port_have_device(port)) {
        port->portsc |= PORTSC_CCS;
        switch (port->uport->dev->speed) {
        case USB_SPEED_LOW:
            port->portsc |= PORTSC_SPEED_LOW;
            pls = PLS_POLLING;
            break;
        case USB_SPEED_FULL:
            port->portsc |= PORTSC_SPEED_FULL;
            pls = PLS_POLLING;
            break;
        case USB_SPEED_HIGH:
            port->portsc |= PORTSC_SPEED_HIGH;
            pls = PLS_POLLING;
            break;
        case USB_SPEED_SUPER:
            port->portsc |= PORTSC_SPEED_SUPER;
            port->portsc |= PORTSC_PED;
            pls = PLS_U0;
            break;
        }
    }
    set_field(&port->portsc, pls, PORTSC_PLS);
    trace_usb_xhci_port_link(port->portnr, pls);
    xhci_port_notify(port, PORTSC_CSC);
}

static void xhci_port_reset(XHCIPort *port, bool warm_reset)
{
    trace_usb_xhci_port_reset(port->portnr, warm_reset);

    if (!xhci_port_have_device(port)) {
        return;
    }

    usb_device_reset(port->uport->dev);

    switch (port->uport->dev->speed) {
    case USB_SPEED_SUPER:
        if (warm_reset) {
            port->portsc |= PORTSC_WRC;
        }
        /* fall through */
    case USB_SPEED_LOW:
    case USB_SPEED_FULL:
    case USB_SPEED_HIGH:
        set_field(&port->portsc, PLS_U0, PORTSC_PLS);
        trace_usb_xhci_port_link(port->portnr, PLS_U0);
        port->portsc |= PORTSC_PED;
        break;
    }

    port->portsc &= ~PORTSC_PR;
    xhci_port_notify(port, PORTSC_PRC);
}

static void xhci_reset(DeviceState *dev)
{
    XHCIState *xhci = XHCI(dev);
    int i;

    trace_usb_xhci_reset();
    if (!(xhci->usbsts & USBSTS_HCH)) {
        DPRINTF("xhci: reset while running!\n");
    }

    xhci->usbcmd = 0;
    xhci->usbsts = USBSTS_HCH;
    xhci->dnctrl = 0;
    xhci->crcr_low = 0;
    xhci->crcr_high = 0;
    xhci->dcbaap_low = 0;
    xhci->dcbaap_high = 0;
    xhci->config = 0;

    for (i = 0; i < xhci->numslots; i++) {
        xhci_disable_slot(xhci, i+1);
    }

    for (i = 0; i < xhci->numports; i++) {
        xhci_port_update(xhci->ports + i, 0);
    }

    for (i = 0; i < xhci->numintrs; i++) {
        xhci->intr[i].iman = 0;
        xhci->intr[i].imod = 0;
        xhci->intr[i].erstsz = 0;
        xhci->intr[i].erstba_low = 0;
        xhci->intr[i].erstba_high = 0;
        xhci->intr[i].erdp_low = 0;
        xhci->intr[i].erdp_high = 0;

        xhci->intr[i].er_ep_idx = 0;
        xhci->intr[i].er_pcs = 1;
        xhci->intr[i].ev_buffer_put = 0;
        xhci->intr[i].ev_buffer_get = 0;
    }

    xhci->mfindex_start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    xhci_mfwrap_update(xhci);
}

static uint64_t xhci_cap_read(void *ptr, hwaddr reg, unsigned size)
{
    XHCIState *xhci = ptr;
    uint32_t ret;

    switch (reg) {
    case 0x00: /* HCIVERSION, CAPLENGTH */
        ret = 0x01000000 | LEN_CAP;
        break;
    case 0x04: /* HCSPARAMS 1 */
        ret = ((xhci->numports_2+xhci->numports_3)<<24)
            | (xhci->numintrs<<8) | xhci->numslots;
        break;
    case 0x08: /* HCSPARAMS 2 */
        ret = 0x0000000f;
        break;
    case 0x0c: /* HCSPARAMS 3 */
        ret = 0x00000000;
        break;
    case 0x10: /* HCCPARAMS */
        if (sizeof(dma_addr_t) == 4) {
            ret = 0x00080000 | (xhci->max_pstreams_mask << 12);
        } else {
            ret = 0x00080001 | (xhci->max_pstreams_mask << 12);
        }
        break;
    case 0x14: /* DBOFF */
        ret = OFF_DOORBELL;
        break;
    case 0x18: /* RTSOFF */
        ret = OFF_RUNTIME;
        break;

    /* extended capabilities */
    case 0x20: /* Supported Protocol:00 */
        ret = 0x02000402; /* USB 2.0 */
        break;
    case 0x24: /* Supported Protocol:04 */
        ret = 0x20425355; /* "USB " */
        break;
    case 0x28: /* Supported Protocol:08 */
        ret = (xhci->numports_2 << 8) | (xhci->numports_3 + 1);
        break;
    case 0x2c: /* Supported Protocol:0c */
        ret = 0x00000000; /* reserved */
        break;
    case 0x30: /* Supported Protocol:00 */
        ret = 0x03000002; /* USB 3.0 */
        break;
    case 0x34: /* Supported Protocol:04 */
        ret = 0x20425355; /* "USB " */
        break;
    case 0x38: /* Supported Protocol:08 */
        ret = (xhci->numports_3 << 8) | 1;
        break;
    case 0x3c: /* Supported Protocol:0c */
        ret = 0x00000000; /* reserved */
        break;
    default:
        trace_usb_xhci_unimplemented("cap read", reg);
        ret = 0;
    }

    trace_usb_xhci_cap_read(reg, ret);
    return ret;
}

static uint64_t xhci_port_read(void *ptr, hwaddr reg, unsigned size)
{
    XHCIPort *port = ptr;
    uint32_t ret;

    switch (reg) {
    case 0x00: /* PORTSC */
        ret = port->portsc;
        break;
    case 0x04: /* PORTPMSC */
    case 0x08: /* PORTLI */
        ret = 0;
        break;
    case 0x0c: /* PORTHLPMC */
        ret = 0;
        qemu_log_mask(LOG_UNIMP, "%s: read from port register PORTHLPMC",
                      __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from port offset 0x%" HWADDR_PRIx,
                      __func__, reg);
        ret = 0;
    }

    trace_usb_xhci_port_read(port->portnr, reg, ret);
    return ret;
}

static void xhci_port_write(void *ptr, hwaddr reg,
                            uint64_t val, unsigned size)
{
    XHCIPort *port = ptr;
    uint32_t portsc, notify;

    trace_usb_xhci_port_write(port->portnr, reg, val);

    switch (reg) {
    case 0x00: /* PORTSC */
        /* write-1-to-start bits */
        if (val & PORTSC_WPR) {
            xhci_port_reset(port, true);
            break;
        }
        if (val & PORTSC_PR) {
            xhci_port_reset(port, false);
            break;
        }

        portsc = port->portsc;
        notify = 0;
        /* write-1-to-clear bits*/
        portsc &= ~(val & (PORTSC_CSC|PORTSC_PEC|PORTSC_WRC|PORTSC_OCC|
                           PORTSC_PRC|PORTSC_PLC|PORTSC_CEC));
        if (val & PORTSC_LWS) {
            /* overwrite PLS only when LWS=1 */
            uint32_t old_pls = get_field(port->portsc, PORTSC_PLS);
            uint32_t new_pls = get_field(val, PORTSC_PLS);
            switch (new_pls) {
            case PLS_U0:
                if (old_pls != PLS_U0) {
                    set_field(&portsc, new_pls, PORTSC_PLS);
                    trace_usb_xhci_port_link(port->portnr, new_pls);
                    notify = PORTSC_PLC;
                }
                break;
            case PLS_U3:
                if (old_pls < PLS_U3) {
                    set_field(&portsc, new_pls, PORTSC_PLS);
                    trace_usb_xhci_port_link(port->portnr, new_pls);
                }
                break;
            case PLS_RESUME:
                /* windows does this for some reason, don't spam stderr */
                break;
            default:
                DPRINTF("%s: ignore pls write (old %d, new %d)\n",
                        __func__, old_pls, new_pls);
                break;
            }
        }
        /* read/write bits */
        portsc &= ~(PORTSC_PP|PORTSC_WCE|PORTSC_WDE|PORTSC_WOE);
        portsc |= (val & (PORTSC_PP|PORTSC_WCE|PORTSC_WDE|PORTSC_WOE));
        port->portsc = portsc;
        if (notify) {
            xhci_port_notify(port, notify);
        }
        break;
    case 0x04: /* PORTPMSC */
    case 0x0c: /* PORTHLPMC */
        qemu_log_mask(LOG_UNIMP,
                      "%s: write 0x%" PRIx64
                      " (%u bytes) to port register at offset 0x%" HWADDR_PRIx,
                      __func__, val, size, reg);
        break;
    case 0x08: /* PORTLI */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Write to read-only PORTLI register",
                      __func__);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write 0x%" PRIx64 " (%u bytes) to unknown port "
                      "register at offset 0x%" HWADDR_PRIx,
                      __func__, val, size, reg);
        break;
    }
}

static uint64_t xhci_oper_read(void *ptr, hwaddr reg, unsigned size)
{
    XHCIState *xhci = ptr;
    uint32_t ret;

    switch (reg) {
    case 0x00: /* USBCMD */
        ret = xhci->usbcmd;
        break;
    case 0x04: /* USBSTS */
        ret = xhci->usbsts;
        break;
    case 0x08: /* PAGESIZE */
        ret = 1; /* 4KiB */
        break;
    case 0x14: /* DNCTRL */
        ret = xhci->dnctrl;
        break;
    case 0x18: /* CRCR low */
        ret = xhci->crcr_low & ~0xe;
        break;
    case 0x1c: /* CRCR high */
        ret = xhci->crcr_high;
        break;
    case 0x30: /* DCBAAP low */
        ret = xhci->dcbaap_low;
        break;
    case 0x34: /* DCBAAP high */
        ret = xhci->dcbaap_high;
        break;
    case 0x38: /* CONFIG */
        ret = xhci->config;
        break;
    default:
        trace_usb_xhci_unimplemented("oper read", reg);
        ret = 0;
    }

    trace_usb_xhci_oper_read(reg, ret);
    return ret;
}

static void xhci_oper_write(void *ptr, hwaddr reg,
                            uint64_t val, unsigned size)
{
    XHCIState *xhci = XHCI(ptr);

    trace_usb_xhci_oper_write(reg, val);

    switch (reg) {
    case 0x00: /* USBCMD */
        if ((val & USBCMD_RS) && !(xhci->usbcmd & USBCMD_RS)) {
            xhci_run(xhci);
        } else if (!(val & USBCMD_RS) && (xhci->usbcmd & USBCMD_RS)) {
            xhci_stop(xhci);
        }
        if (val & USBCMD_CSS) {
            /* save state */
            xhci->usbsts &= ~USBSTS_SRE;
        }
        if (val & USBCMD_CRS) {
            /* restore state */
            xhci->usbsts |= USBSTS_SRE;
        }
        xhci->usbcmd = val & 0xc0f;
        xhci_mfwrap_update(xhci);
        if (val & USBCMD_HCRST) {
            xhci_reset(DEVICE(xhci));
        }
        xhci_intr_update(xhci, 0);
        break;

    case 0x04: /* USBSTS */
        /* these bits are write-1-to-clear */
        xhci->usbsts &= ~(val & (USBSTS_HSE|USBSTS_EINT|USBSTS_PCD|USBSTS_SRE));
        xhci_intr_update(xhci, 0);
        break;

    case 0x14: /* DNCTRL */
        xhci->dnctrl = val & 0xffff;
        break;
    case 0x18: /* CRCR low */
        xhci->crcr_low = (val & 0xffffffcf) | (xhci->crcr_low & CRCR_CRR);
        break;
    case 0x1c: /* CRCR high */
        xhci->crcr_high = val;
        if (xhci->crcr_low & (CRCR_CA|CRCR_CS) && (xhci->crcr_low & CRCR_CRR)) {
            XHCIEvent event = {ER_COMMAND_COMPLETE, CC_COMMAND_RING_STOPPED};
            xhci->crcr_low &= ~CRCR_CRR;
            xhci_event(xhci, &event, 0);
            DPRINTF("xhci: command ring stopped (CRCR=%08x)\n", xhci->crcr_low);
        } else {
            dma_addr_t base = xhci_addr64(xhci->crcr_low & ~0x3f, val);
            xhci_ring_init(xhci, &xhci->cmd_ring, base);
        }
        xhci->crcr_low &= ~(CRCR_CA | CRCR_CS);
        break;
    case 0x30: /* DCBAAP low */
        xhci->dcbaap_low = val & 0xffffffc0;
        break;
    case 0x34: /* DCBAAP high */
        xhci->dcbaap_high = val;
        break;
    case 0x38: /* CONFIG */
        xhci->config = val & 0xff;
        break;
    default:
        trace_usb_xhci_unimplemented("oper write", reg);
    }
}

static uint64_t xhci_runtime_read(void *ptr, hwaddr reg,
                                  unsigned size)
{
    XHCIState *xhci = ptr;
    uint32_t ret = 0;

    if (reg < 0x20) {
        switch (reg) {
        case 0x00: /* MFINDEX */
            ret = xhci_mfindex_get(xhci) & 0x3fff;
            break;
        default:
            trace_usb_xhci_unimplemented("runtime read", reg);
            break;
        }
    } else {
        int v = (reg - 0x20) / 0x20;
        XHCIInterrupter *intr = &xhci->intr[v];
        switch (reg & 0x1f) {
        case 0x00: /* IMAN */
            ret = intr->iman;
            break;
        case 0x04: /* IMOD */
            ret = intr->imod;
            break;
        case 0x08: /* ERSTSZ */
            ret = intr->erstsz;
            break;
        case 0x10: /* ERSTBA low */
            ret = intr->erstba_low;
            break;
        case 0x14: /* ERSTBA high */
            ret = intr->erstba_high;
            break;
        case 0x18: /* ERDP low */
            ret = intr->erdp_low;
            break;
        case 0x1c: /* ERDP high */
            ret = intr->erdp_high;
            break;
        }
    }

    trace_usb_xhci_runtime_read(reg, ret);
    return ret;
}

static void xhci_runtime_write(void *ptr, hwaddr reg,
                               uint64_t val, unsigned size)
{
    XHCIState *xhci = ptr;
    XHCIInterrupter *intr;
    int v;

    trace_usb_xhci_runtime_write(reg, val);

    if (reg < 0x20) {
        trace_usb_xhci_unimplemented("runtime write", reg);
        return;
    }
    v = (reg - 0x20) / 0x20;
    intr = &xhci->intr[v];

    switch (reg & 0x1f) {
    case 0x00: /* IMAN */
        if (val & IMAN_IP) {
            intr->iman &= ~IMAN_IP;
        }
        intr->iman &= ~IMAN_IE;
        intr->iman |= val & IMAN_IE;
        xhci_intr_update(xhci, v);
        break;
    case 0x04: /* IMOD */
        intr->imod = val;
        break;
    case 0x08: /* ERSTSZ */
        intr->erstsz = val & 0xffff;
        break;
    case 0x10: /* ERSTBA low */
        if (xhci->nec_quirks) {
            /* NEC driver bug: it doesn't align this to 64 bytes */
            intr->erstba_low = val & 0xfffffff0;
        } else {
            intr->erstba_low = val & 0xffffffc0;
        }
        break;
    case 0x14: /* ERSTBA high */
        intr->erstba_high = val;
        xhci_er_reset(xhci, v);
        break;
    case 0x18: /* ERDP low */
        if (val & ERDP_EHB) {
            intr->erdp_low &= ~ERDP_EHB;
        }
        intr->erdp_low = (val & ~ERDP_EHB) | (intr->erdp_low & ERDP_EHB);
        if (val & ERDP_EHB) {
            dma_addr_t erdp = xhci_addr64(intr->erdp_low, intr->erdp_high);
            unsigned int dp_idx = (erdp - intr->er_start) / TRB_SIZE;
            if (erdp >= intr->er_start &&
                erdp < (intr->er_start + TRB_SIZE * intr->er_size) &&
                dp_idx != intr->er_ep_idx) {
                xhci_intr_raise(xhci, v);
            }
        }
        break;
    case 0x1c: /* ERDP high */
        intr->erdp_high = val;
        break;
    default:
        trace_usb_xhci_unimplemented("oper write", reg);
    }
}

static uint64_t xhci_doorbell_read(void *ptr, hwaddr reg,
                                   unsigned size)
{
    /* doorbells always read as 0 */
    trace_usb_xhci_doorbell_read(reg, 0);
    return 0;
}

static void xhci_doorbell_write(void *ptr, hwaddr reg,
                                uint64_t val, unsigned size)
{
    XHCIState *xhci = ptr;
    unsigned int epid, streamid;

    trace_usb_xhci_doorbell_write(reg, val);

    if (!xhci_running(xhci)) {
        DPRINTF("xhci: wrote doorbell while xHC stopped or paused\n");
        return;
    }

    reg >>= 2;

    if (reg == 0) {
        if (val == 0) {
            xhci_process_commands(xhci);
        } else {
            DPRINTF("xhci: bad doorbell 0 write: 0x%x\n",
                    (uint32_t)val);
        }
    } else {
        epid = val & 0xff;
        streamid = (val >> 16) & 0xffff;
        if (reg > xhci->numslots) {
            DPRINTF("xhci: bad doorbell %d\n", (int)reg);
        } else if (epid == 0 || epid > 31) {
            DPRINTF("xhci: bad doorbell %d write: 0x%x\n",
                    (int)reg, (uint32_t)val);
        } else {
            xhci_kick_ep(xhci, reg, epid, streamid);
        }
    }
}

static void xhci_cap_write(void *opaque, hwaddr addr, uint64_t val,
                           unsigned width)
{
    /* nothing */
}

static const MemoryRegionOps xhci_cap_ops = {
    .read = xhci_cap_read,
    .write = xhci_cap_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps xhci_oper_ops = {
    .read = xhci_oper_read,
    .write = xhci_oper_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = sizeof(dma_addr_t),
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps xhci_port_ops = {
    .read = xhci_port_read,
    .write = xhci_port_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps xhci_runtime_ops = {
    .read = xhci_runtime_read,
    .write = xhci_runtime_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = sizeof(dma_addr_t),
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static const MemoryRegionOps xhci_doorbell_ops = {
    .read = xhci_doorbell_read,
    .write = xhci_doorbell_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void xhci_attach(USBPort *usbport)
{
    XHCIState *xhci = usbport->opaque;
    XHCIPort *port = xhci_lookup_port(xhci, usbport);

    xhci_port_update(port, 0);
}

static void xhci_detach(USBPort *usbport)
{
    XHCIState *xhci = usbport->opaque;
    XHCIPort *port = xhci_lookup_port(xhci, usbport);

    xhci_detach_slot(xhci, usbport);
    xhci_port_update(port, 1);
}

static void xhci_wakeup(USBPort *usbport)
{
    XHCIState *xhci = usbport->opaque;
    XHCIPort *port = xhci_lookup_port(xhci, usbport);

    assert(port);
    if (get_field(port->portsc, PORTSC_PLS) != PLS_U3) {
        return;
    }
    set_field(&port->portsc, PLS_RESUME, PORTSC_PLS);
    xhci_port_notify(port, PORTSC_PLC);
}

static void xhci_complete(USBPort *port, USBPacket *packet)
{
    XHCITransfer *xfer = container_of(packet, XHCITransfer, packet);

    if (packet->status == USB_RET_REMOVE_FROM_QUEUE) {
        xhci_ep_nuke_one_xfer(xfer, 0);
        return;
    }
    xhci_try_complete_packet(xfer);
    xhci_kick_epctx(xfer->epctx, xfer->streamid);
    if (xfer->complete) {
        xhci_ep_free_xfer(xfer);
    }
}

static void xhci_child_detach(USBPort *uport, USBDevice *child)
{
    USBBus *bus = usb_bus_from_device(child);
    XHCIState *xhci = container_of(bus, XHCIState, bus);

    xhci_detach_slot(xhci, child->port);
}

static USBPortOps xhci_uport_ops = {
    .attach   = xhci_attach,
    .detach   = xhci_detach,
    .wakeup   = xhci_wakeup,
    .complete = xhci_complete,
    .child_detach = xhci_child_detach,
};

static int xhci_find_epid(USBEndpoint *ep)
{
    if (ep->nr == 0) {
        return 1;
    }
    if (ep->pid == USB_TOKEN_IN) {
        return ep->nr * 2 + 1;
    } else {
        return ep->nr * 2;
    }
}

static USBEndpoint *xhci_epid_to_usbep(XHCIEPContext *epctx)
{
    USBPort *uport;
    uint32_t token;

    if (!epctx) {
        return NULL;
    }
    uport = epctx->xhci->slots[epctx->slotid - 1].uport;
    if (!uport || !uport->dev) {
        return NULL;
    }
    token = (epctx->epid & 1) ? USB_TOKEN_IN : USB_TOKEN_OUT;
    return usb_ep_get(uport->dev, token, epctx->epid >> 1);
}

static void xhci_wakeup_endpoint(USBBus *bus, USBEndpoint *ep,
                                 unsigned int stream)
{
    XHCIState *xhci = container_of(bus, XHCIState, bus);
    int slotid;

    DPRINTF("%s\n", __func__);
    slotid = ep->dev->addr;
    if (slotid == 0 || slotid > xhci->numslots ||
        !xhci->slots[slotid - 1].enabled) {
        DPRINTF("%s: oops, no slot for dev %d\n", __func__, ep->dev->addr);
        return;
    }
    xhci_kick_ep(xhci, slotid, xhci_find_epid(ep), stream);
}

static USBBusOps xhci_bus_ops = {
    .wakeup_endpoint = xhci_wakeup_endpoint,
};

static void usb_xhci_init(XHCIState *xhci)
{
    XHCIPort *port;
    unsigned int i, usbports, speedmask;

    xhci->usbsts = USBSTS_HCH;

    if (xhci->numports_2 > XHCI_MAXPORTS_2) {
        xhci->numports_2 = XHCI_MAXPORTS_2;
    }
    if (xhci->numports_3 > XHCI_MAXPORTS_3) {
        xhci->numports_3 = XHCI_MAXPORTS_3;
    }
    usbports = MAX(xhci->numports_2, xhci->numports_3);
    xhci->numports = xhci->numports_2 + xhci->numports_3;

    usb_bus_new(&xhci->bus, sizeof(xhci->bus), &xhci_bus_ops, xhci->hostOpaque);

    for (i = 0; i < usbports; i++) {
        speedmask = 0;
        if (i < xhci->numports_2) {
            port = &xhci->ports[i + xhci->numports_3];
            port->portnr = i + 1 + xhci->numports_3;
            port->uport = &xhci->uports[i];
            port->speedmask =
                USB_SPEED_MASK_LOW  |
                USB_SPEED_MASK_FULL |
                USB_SPEED_MASK_HIGH;
            assert(i < XHCI_MAXPORTS);
            snprintf(port->name, sizeof(port->name), "usb2 port #%d", i+1);
            speedmask |= port->speedmask;
        }
        if (i < xhci->numports_3) {
            port = &xhci->ports[i];
            port->portnr = i + 1;
            port->uport = &xhci->uports[i];
            port->speedmask = USB_SPEED_MASK_SUPER;
            assert(i < XHCI_MAXPORTS);
            snprintf(port->name, sizeof(port->name), "usb3 port #%d", i+1);
            speedmask |= port->speedmask;
        }
        usb_register_port(&xhci->bus, &xhci->uports[i], xhci, i,
                          &xhci_uport_ops, speedmask);
    }
}

static void usb_xhci_realize(DeviceState *dev, Error **errp)
{
    int i;

    XHCIState *xhci = XHCI(dev);

    if (xhci->numintrs > XHCI_MAXINTRS) {
        xhci->numintrs = XHCI_MAXINTRS;
    }
    while (xhci->numintrs & (xhci->numintrs - 1)) {   /* ! power of 2 */
        xhci->numintrs++;
    }
    if (xhci->numintrs < 1) {
        xhci->numintrs = 1;
    }
    if (xhci->numslots > XHCI_MAXSLOTS) {
        xhci->numslots = XHCI_MAXSLOTS;
    }
    if (xhci->numslots < 1) {
        xhci->numslots = 1;
    }
    if (xhci_get_flag(xhci, XHCI_FLAG_ENABLE_STREAMS)) {
        xhci->max_pstreams_mask = 7; /* == 256 primary streams */
    } else {
        xhci->max_pstreams_mask = 0;
    }

    usb_xhci_init(xhci);
    xhci->mfwrap_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, xhci_mfwrap_timer, xhci);

    memory_region_init(&xhci->mem, OBJECT(dev), "xhci", XHCI_LEN_REGS);
    memory_region_init_io(&xhci->mem_cap, OBJECT(dev), &xhci_cap_ops, xhci,
                          "capabilities", LEN_CAP);
    memory_region_init_io(&xhci->mem_oper, OBJECT(dev), &xhci_oper_ops, xhci,
                          "operational", 0x400);
    memory_region_init_io(&xhci->mem_runtime, OBJECT(dev), &xhci_runtime_ops,
                           xhci, "runtime", LEN_RUNTIME);
    memory_region_init_io(&xhci->mem_doorbell, OBJECT(dev), &xhci_doorbell_ops,
                           xhci, "doorbell", LEN_DOORBELL);

    memory_region_add_subregion(&xhci->mem, 0,            &xhci->mem_cap);
    memory_region_add_subregion(&xhci->mem, OFF_OPER,     &xhci->mem_oper);
    memory_region_add_subregion(&xhci->mem, OFF_RUNTIME,  &xhci->mem_runtime);
    memory_region_add_subregion(&xhci->mem, OFF_DOORBELL, &xhci->mem_doorbell);

    for (i = 0; i < xhci->numports; i++) {
        XHCIPort *port = &xhci->ports[i];
        uint32_t offset = OFF_OPER + 0x400 + 0x10 * i;
        port->xhci = xhci;
        memory_region_init_io(&port->mem, OBJECT(dev), &xhci_port_ops, port,
                              port->name, 0x10);
        memory_region_add_subregion(&xhci->mem, offset, &port->mem);
    }
}

static void usb_xhci_unrealize(DeviceState *dev)
{
    int i;
    XHCIState *xhci = XHCI(dev);

    trace_usb_xhci_exit();

    for (i = 0; i < xhci->numslots; i++) {
        xhci_disable_slot(xhci, i + 1);
    }

    if (xhci->mfwrap_timer) {
        timer_free(xhci->mfwrap_timer);
        xhci->mfwrap_timer = NULL;
    }

    memory_region_del_subregion(&xhci->mem, &xhci->mem_cap);
    memory_region_del_subregion(&xhci->mem, &xhci->mem_oper);
    memory_region_del_subregion(&xhci->mem, &xhci->mem_runtime);
    memory_region_del_subregion(&xhci->mem, &xhci->mem_doorbell);

    for (i = 0; i < xhci->numports; i++) {
        XHCIPort *port = &xhci->ports[i];
        memory_region_del_subregion(&xhci->mem, &port->mem);
    }

    usb_bus_release(&xhci->bus);
}

static int usb_xhci_post_load(void *opaque, int version_id)
{
    XHCIState *xhci = opaque;
    XHCISlot *slot;
    XHCIEPContext *epctx;
    dma_addr_t dcbaap, pctx;
    uint32_t slot_ctx[4];
    uint32_t ep_ctx[5];
    int slotid, epid, state;
    uint64_t addr;

    dcbaap = xhci_addr64(xhci->dcbaap_low, xhci->dcbaap_high);

    for (slotid = 1; slotid <= xhci->numslots; slotid++) {
        slot = &xhci->slots[slotid-1];
        if (!slot->addressed) {
            continue;
        }
        ldq_le_dma(xhci->as, dcbaap + 8 * slotid, &addr, MEMTXATTRS_UNSPECIFIED);
        slot->ctx = xhci_mask64(addr);

        xhci_dma_read_u32s(xhci, slot->ctx, slot_ctx, sizeof(slot_ctx));
        slot->uport = xhci_lookup_uport(xhci, slot_ctx);
        if (!slot->uport) {
            /* should not happen, but may trigger on guest bugs */
            slot->enabled = 0;
            slot->addressed = 0;
            continue;
        }
        assert(slot->uport && slot->uport->dev);

        for (epid = 1; epid <= 31; epid++) {
            pctx = slot->ctx + 32 * epid;
            xhci_dma_read_u32s(xhci, pctx, ep_ctx, sizeof(ep_ctx));
            state = ep_ctx[0] & EP_STATE_MASK;
            if (state == EP_DISABLED) {
                continue;
            }
            epctx = xhci_alloc_epctx(xhci, slotid, epid);
            slot->eps[epid-1] = epctx;
            xhci_init_epctx(epctx, pctx, ep_ctx);
            epctx->state = state;
            if (state == EP_RUNNING) {
                /* kick endpoint after vmload is finished */
                timer_mod(epctx->kick_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
            }
        }
    }
    return 0;
}

static const VMStateDescription vmstate_xhci_ring = {
    .name = "xhci-ring",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(dequeue, XHCIRing),
        VMSTATE_BOOL(ccs, XHCIRing),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_xhci_port = {
    .name = "xhci-port",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(portsc, XHCIPort),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_xhci_slot = {
    .name = "xhci-slot",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(enabled,   XHCISlot),
        VMSTATE_BOOL(addressed, XHCISlot),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_xhci_event = {
    .name = "xhci-event",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(type,   XHCIEvent),
        VMSTATE_UINT32(ccode,  XHCIEvent),
        VMSTATE_UINT64(ptr,    XHCIEvent),
        VMSTATE_UINT32(length, XHCIEvent),
        VMSTATE_UINT32(flags,  XHCIEvent),
        VMSTATE_UINT8(slotid,  XHCIEvent),
        VMSTATE_UINT8(epid,    XHCIEvent),
        VMSTATE_END_OF_LIST()
    }
};

static bool xhci_er_full(void *opaque, int version_id)
{
    return false;
}

static const VMStateDescription vmstate_xhci_intr = {
    .name = "xhci-intr",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        /* registers */
        VMSTATE_UINT32(iman,          XHCIInterrupter),
        VMSTATE_UINT32(imod,          XHCIInterrupter),
        VMSTATE_UINT32(erstsz,        XHCIInterrupter),
        VMSTATE_UINT32(erstba_low,    XHCIInterrupter),
        VMSTATE_UINT32(erstba_high,   XHCIInterrupter),
        VMSTATE_UINT32(erdp_low,      XHCIInterrupter),
        VMSTATE_UINT32(erdp_high,     XHCIInterrupter),

        /* state */
        VMSTATE_BOOL(msix_used,       XHCIInterrupter),
        VMSTATE_BOOL(er_pcs,          XHCIInterrupter),
        VMSTATE_UINT64(er_start,      XHCIInterrupter),
        VMSTATE_UINT32(er_size,       XHCIInterrupter),
        VMSTATE_UINT32(er_ep_idx,     XHCIInterrupter),

        /* event queue (used if ring is full) */
        VMSTATE_BOOL(er_full_unused,  XHCIInterrupter),
        VMSTATE_UINT32_TEST(ev_buffer_put, XHCIInterrupter, xhci_er_full),
        VMSTATE_UINT32_TEST(ev_buffer_get, XHCIInterrupter, xhci_er_full),
        VMSTATE_STRUCT_ARRAY_TEST(ev_buffer, XHCIInterrupter, EV_QUEUE,
                                  xhci_er_full, 1,
                                  vmstate_xhci_event, XHCIEvent),

        VMSTATE_END_OF_LIST()
    }
};

const VMStateDescription vmstate_xhci = {
    .name = "xhci-core",
    .version_id = 1,
    .post_load = usb_xhci_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_UINT32(ports, XHCIState, numports, 1,
                                     vmstate_xhci_port, XHCIPort),
        VMSTATE_STRUCT_VARRAY_UINT32(slots, XHCIState, numslots, 1,
                                     vmstate_xhci_slot, XHCISlot),
        VMSTATE_STRUCT_VARRAY_UINT32(intr, XHCIState, numintrs, 1,
                                     vmstate_xhci_intr, XHCIInterrupter),

        /* Operational Registers */
        VMSTATE_UINT32(usbcmd,        XHCIState),
        VMSTATE_UINT32(usbsts,        XHCIState),
        VMSTATE_UINT32(dnctrl,        XHCIState),
        VMSTATE_UINT32(crcr_low,      XHCIState),
        VMSTATE_UINT32(crcr_high,     XHCIState),
        VMSTATE_UINT32(dcbaap_low,    XHCIState),
        VMSTATE_UINT32(dcbaap_high,   XHCIState),
        VMSTATE_UINT32(config,        XHCIState),

        /* Runtime Registers & state */
        VMSTATE_INT64(mfindex_start,  XHCIState),
        VMSTATE_TIMER_PTR(mfwrap_timer,   XHCIState),
        VMSTATE_STRUCT(cmd_ring, XHCIState, 1, vmstate_xhci_ring, XHCIRing),

        VMSTATE_END_OF_LIST()
    }
};

static const Property xhci_properties[] = {
    DEFINE_PROP_BIT("streams", XHCIState, flags,
                    XHCI_FLAG_ENABLE_STREAMS, true),
    DEFINE_PROP_UINT32("p2",    XHCIState, numports_2, 4),
    DEFINE_PROP_UINT32("p3",    XHCIState, numports_3, 4),
    DEFINE_PROP_LINK("host",    XHCIState, hostOpaque, TYPE_DEVICE,
                     DeviceState *),
};

static void xhci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = usb_xhci_realize;
    dc->unrealize = usb_xhci_unrealize;
    device_class_set_legacy_reset(dc, xhci_reset);
    device_class_set_props(dc, xhci_properties);
    dc->user_creatable = false;
}

static const TypeInfo xhci_info = {
    .name          = TYPE_XHCI,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(XHCIState),
    .class_init    = xhci_class_init,
};

static void xhci_register_types(void)
{
    type_register_static(&xhci_info);
}

type_init(xhci_register_types)
