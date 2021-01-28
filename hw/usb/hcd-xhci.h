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

#ifndef HW_USB_HCD_XHCI_H
#define HW_USB_HCD_XHCI_H
#include "qom/object.h"

#include "hw/usb.h"
#include "hw/usb/xhci.h"
#include "sysemu/dma.h"

OBJECT_DECLARE_SIMPLE_TYPE(XHCIState, XHCI)

/* Very pessimistic, let's hope it's enough for all cases */
#define EV_QUEUE (((3 * 24) + 16) * XHCI_MAXSLOTS)

typedef struct XHCIStreamContext XHCIStreamContext;
typedef struct XHCIEPContext XHCIEPContext;

enum xhci_flags {
    XHCI_FLAG_SS_FIRST = 1,
    XHCI_FLAG_FORCE_PCIE_ENDCAP,
    XHCI_FLAG_ENABLE_STREAMS,
};

typedef enum TRBType {
    TRB_RESERVED = 0,
    TR_NORMAL,
    TR_SETUP,
    TR_DATA,
    TR_STATUS,
    TR_ISOCH,
    TR_LINK,
    TR_EVDATA,
    TR_NOOP,
    CR_ENABLE_SLOT,
    CR_DISABLE_SLOT,
    CR_ADDRESS_DEVICE,
    CR_CONFIGURE_ENDPOINT,
    CR_EVALUATE_CONTEXT,
    CR_RESET_ENDPOINT,
    CR_STOP_ENDPOINT,
    CR_SET_TR_DEQUEUE,
    CR_RESET_DEVICE,
    CR_FORCE_EVENT,
    CR_NEGOTIATE_BW,
    CR_SET_LATENCY_TOLERANCE,
    CR_GET_PORT_BANDWIDTH,
    CR_FORCE_HEADER,
    CR_NOOP,
    ER_TRANSFER = 32,
    ER_COMMAND_COMPLETE,
    ER_PORT_STATUS_CHANGE,
    ER_BANDWIDTH_REQUEST,
    ER_DOORBELL,
    ER_HOST_CONTROLLER,
    ER_DEVICE_NOTIFICATION,
    ER_MFINDEX_WRAP,
    /* vendor specific bits */
    CR_VENDOR_NEC_FIRMWARE_REVISION  = 49,
    CR_VENDOR_NEC_CHALLENGE_RESPONSE = 50,
} TRBType;

typedef enum TRBCCode {
    CC_INVALID = 0,
    CC_SUCCESS,
    CC_DATA_BUFFER_ERROR,
    CC_BABBLE_DETECTED,
    CC_USB_TRANSACTION_ERROR,
    CC_TRB_ERROR,
    CC_STALL_ERROR,
    CC_RESOURCE_ERROR,
    CC_BANDWIDTH_ERROR,
    CC_NO_SLOTS_ERROR,
    CC_INVALID_STREAM_TYPE_ERROR,
    CC_SLOT_NOT_ENABLED_ERROR,
    CC_EP_NOT_ENABLED_ERROR,
    CC_SHORT_PACKET,
    CC_RING_UNDERRUN,
    CC_RING_OVERRUN,
    CC_VF_ER_FULL,
    CC_PARAMETER_ERROR,
    CC_BANDWIDTH_OVERRUN,
    CC_CONTEXT_STATE_ERROR,
    CC_NO_PING_RESPONSE_ERROR,
    CC_EVENT_RING_FULL_ERROR,
    CC_INCOMPATIBLE_DEVICE_ERROR,
    CC_MISSED_SERVICE_ERROR,
    CC_COMMAND_RING_STOPPED,
    CC_COMMAND_ABORTED,
    CC_STOPPED,
    CC_STOPPED_LENGTH_INVALID,
    CC_MAX_EXIT_LATENCY_TOO_LARGE_ERROR = 29,
    CC_ISOCH_BUFFER_OVERRUN = 31,
    CC_EVENT_LOST_ERROR,
    CC_UNDEFINED_ERROR,
    CC_INVALID_STREAM_ID_ERROR,
    CC_SECONDARY_BANDWIDTH_ERROR,
    CC_SPLIT_TRANSACTION_ERROR
} TRBCCode;

typedef struct XHCIRing {
    dma_addr_t dequeue;
    bool ccs;
} XHCIRing;

typedef struct XHCIPort {
    XHCIState *xhci;
    uint32_t portsc;
    uint32_t portnr;
    USBPort  *uport;
    uint32_t speedmask;
    char name[20];
    MemoryRegion mem;
} XHCIPort;

typedef struct XHCISlot {
    bool enabled;
    bool addressed;
    uint16_t intr;
    dma_addr_t ctx;
    USBPort *uport;
    XHCIEPContext *eps[31];
} XHCISlot;

typedef struct XHCIEvent {
    TRBType type;
    TRBCCode ccode;
    uint64_t ptr;
    uint32_t length;
    uint32_t flags;
    uint8_t slotid;
    uint8_t epid;
} XHCIEvent;

typedef struct XHCIInterrupter {
    uint32_t iman;
    uint32_t imod;
    uint32_t erstsz;
    uint32_t erstba_low;
    uint32_t erstba_high;
    uint32_t erdp_low;
    uint32_t erdp_high;

    bool msix_used, er_pcs;

    dma_addr_t er_start;
    uint32_t er_size;
    unsigned int er_ep_idx;

    /* kept for live migration compat only */
    bool er_full_unused;
    XHCIEvent ev_buffer[EV_QUEUE];
    unsigned int ev_buffer_put;
    unsigned int ev_buffer_get;

} XHCIInterrupter;

typedef struct XHCIState {
    DeviceState parent;

    USBBus bus;
    MemoryRegion mem;
    MemoryRegion *dma_mr;
    AddressSpace *as;
    MemoryRegion mem_cap;
    MemoryRegion mem_oper;
    MemoryRegion mem_runtime;
    MemoryRegion mem_doorbell;

    /* properties */
    uint32_t numports_2;
    uint32_t numports_3;
    uint32_t numintrs;
    uint32_t numslots;
    uint32_t flags;
    uint32_t max_pstreams_mask;
    void (*intr_update)(XHCIState *s, int n, bool enable);
    void (*intr_raise)(XHCIState *s, int n, bool level);
    DeviceState *hostOpaque;

    /* Operational Registers */
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t dnctrl;
    uint32_t crcr_low;
    uint32_t crcr_high;
    uint32_t dcbaap_low;
    uint32_t dcbaap_high;
    uint32_t config;

    USBPort  uports[MAX_CONST(XHCI_MAXPORTS_2, XHCI_MAXPORTS_3)];
    XHCIPort ports[XHCI_MAXPORTS];
    XHCISlot slots[XHCI_MAXSLOTS];
    uint32_t numports;

    /* Runtime Registers */
    int64_t mfindex_start;
    QEMUTimer *mfwrap_timer;
    XHCIInterrupter intr[XHCI_MAXINTRS];

    XHCIRing cmd_ring;

    bool nec_quirks;
} XHCIState;

extern const VMStateDescription vmstate_xhci;
bool xhci_get_flag(XHCIState *xhci, enum xhci_flags bit);
void xhci_set_flag(XHCIState *xhci, enum xhci_flags bit);
#endif
