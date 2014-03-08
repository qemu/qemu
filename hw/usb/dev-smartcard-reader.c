/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * CCID Device emulation
 *
 * Written by Alon Levy, with contributions from Robert Relyea.
 *
 * Based on usb-serial.c, see its copyright and attributions below.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.1 or later.
 * See the COPYING file in the top-level directory.
 * ------- (original copyright & attribution for usb-serial.c below) --------
 * Copyright (c) 2006 CodeSourcery.
 * Copyright (c) 2008 Samuel Thibault <samuel.thibault@ens-lyon.org>
 * Written by Paul Brook, reused for FTDI by Samuel Thibault,
 */

/*
 * References:
 *
 * CCID Specification Revision 1.1 April 22nd 2005
 *  "Universal Serial Bus, Device Class: Smart Card"
 *  Specification for Integrated Circuit(s) Cards Interface Devices
 *
 * Endianness note: from the spec (1.3)
 *  "Fields that are larger than a byte are stored in little endian"
 *
 * KNOWN BUGS
 * 1. remove/insert can sometimes result in removed state instead of inserted.
 * This is a result of the following:
 *  symptom: dmesg shows ERMOTEIO (-121), pcscd shows -99. This can happen
 *  when a short packet is sent, as seen in uhci-usb.c, resulting from a urb
 *  from the guest requesting SPD and us returning a smaller packet.
 *  Not sure which messages trigger this.
 */

#include "qemu-common.h"
#include "qemu/error-report.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "monitor/monitor.h"

#include "ccid.h"

#define DPRINTF(s, lvl, fmt, ...) \
do { \
    if (lvl <= s->debug) { \
        printf("usb-ccid: " fmt , ## __VA_ARGS__); \
    } \
} while (0)

#define D_WARN 1
#define D_INFO 2
#define D_MORE_INFO 3
#define D_VERBOSE 4

#define CCID_DEV_NAME "usb-ccid"

/*
 * The two options for variable sized buffers:
 * make them constant size, for large enough constant,
 * or handle the migration complexity - VMState doesn't handle this case.
 * sizes are expected never to be exceeded, unless guest misbehaves.
 */
#define BULK_OUT_DATA_SIZE 65536
#define PENDING_ANSWERS_NUM 128

#define BULK_IN_BUF_SIZE 384
#define BULK_IN_PENDING_NUM 8

#define CCID_MAX_PACKET_SIZE                64

#define CCID_CONTROL_ABORT                  0x1
#define CCID_CONTROL_GET_CLOCK_FREQUENCIES  0x2
#define CCID_CONTROL_GET_DATA_RATES         0x3

#define CCID_PRODUCT_DESCRIPTION        "QEMU USB CCID"
#define CCID_VENDOR_DESCRIPTION         "QEMU"
#define CCID_INTERFACE_NAME             "CCID Interface"
#define CCID_SERIAL_NUMBER_STRING       "1"
/*
 * Using Gemplus Vendor and Product id
 * Effect on various drivers:
 *  usbccid.sys (winxp, others untested) is a class driver so it doesn't care.
 *  linux has a number of class drivers, but openct filters based on
 *   vendor/product (/etc/openct.conf under fedora), hence Gemplus.
 */
#define CCID_VENDOR_ID                  0x08e6
#define CCID_PRODUCT_ID                 0x4433
#define CCID_DEVICE_VERSION             0x0000

/*
 * BULK_OUT messages from PC to Reader
 * Defined in CCID Rev 1.1 6.1 (page 26)
 */
#define CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOn              0x62
#define CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOff             0x63
#define CCID_MESSAGE_TYPE_PC_to_RDR_GetSlotStatus           0x65
#define CCID_MESSAGE_TYPE_PC_to_RDR_XfrBlock                0x6f
#define CCID_MESSAGE_TYPE_PC_to_RDR_GetParameters           0x6c
#define CCID_MESSAGE_TYPE_PC_to_RDR_ResetParameters         0x6d
#define CCID_MESSAGE_TYPE_PC_to_RDR_SetParameters           0x61
#define CCID_MESSAGE_TYPE_PC_to_RDR_Escape                  0x6b
#define CCID_MESSAGE_TYPE_PC_to_RDR_IccClock                0x6e
#define CCID_MESSAGE_TYPE_PC_to_RDR_T0APDU                  0x6a
#define CCID_MESSAGE_TYPE_PC_to_RDR_Secure                  0x69
#define CCID_MESSAGE_TYPE_PC_to_RDR_Mechanical              0x71
#define CCID_MESSAGE_TYPE_PC_to_RDR_Abort                   0x72
#define CCID_MESSAGE_TYPE_PC_to_RDR_SetDataRateAndClockFrequency 0x73

/*
 * BULK_IN messages from Reader to PC
 * Defined in CCID Rev 1.1 6.2 (page 48)
 */
#define CCID_MESSAGE_TYPE_RDR_to_PC_DataBlock               0x80
#define CCID_MESSAGE_TYPE_RDR_to_PC_SlotStatus              0x81
#define CCID_MESSAGE_TYPE_RDR_to_PC_Parameters              0x82
#define CCID_MESSAGE_TYPE_RDR_to_PC_Escape                  0x83
#define CCID_MESSAGE_TYPE_RDR_to_PC_DataRateAndClockFrequency 0x84

/*
 * INTERRUPT_IN messages from Reader to PC
 * Defined in CCID Rev 1.1 6.3 (page 56)
 */
#define CCID_MESSAGE_TYPE_RDR_to_PC_NotifySlotChange        0x50
#define CCID_MESSAGE_TYPE_RDR_to_PC_HardwareError           0x51

/*
 * Endpoints for CCID - addresses are up to us to decide.
 * To support slot insertion and removal we must have an interrupt in ep
 * in addition we need a bulk in and bulk out ep
 * 5.2, page 20
 */
#define CCID_INT_IN_EP       1
#define CCID_BULK_IN_EP      2
#define CCID_BULK_OUT_EP     3

/* bmSlotICCState masks */
#define SLOT_0_STATE_MASK    1
#define SLOT_0_CHANGED_MASK  2

/* Status codes that go in bStatus (see 6.2.6) */
enum {
    ICC_STATUS_PRESENT_ACTIVE = 0,
    ICC_STATUS_PRESENT_INACTIVE,
    ICC_STATUS_NOT_PRESENT
};

enum {
    COMMAND_STATUS_NO_ERROR = 0,
    COMMAND_STATUS_FAILED,
    COMMAND_STATUS_TIME_EXTENSION_REQUIRED
};

/* Error codes that go in bError (see 6.2.6) */
enum {
    ERROR_CMD_NOT_SUPPORTED = 0,
    ERROR_CMD_ABORTED       = -1,
    ERROR_ICC_MUTE          = -2,
    ERROR_XFR_PARITY_ERROR  = -3,
    ERROR_XFR_OVERRUN       = -4,
    ERROR_HW_ERROR          = -5,
};

/* 6.2.6 RDR_to_PC_SlotStatus definitions */
enum {
    CLOCK_STATUS_RUNNING = 0,
    /*
     * 0 - Clock Running, 1 - Clock stopped in State L, 2 - H,
     * 3 - unknown state. rest are RFU
     */
};

typedef struct QEMU_PACKED CCID_Header {
    uint8_t     bMessageType;
    uint32_t    dwLength;
    uint8_t     bSlot;
    uint8_t     bSeq;
} CCID_Header;

typedef struct QEMU_PACKED CCID_BULK_IN {
    CCID_Header hdr;
    uint8_t     bStatus;        /* Only used in BULK_IN */
    uint8_t     bError;         /* Only used in BULK_IN */
} CCID_BULK_IN;

typedef struct QEMU_PACKED CCID_SlotStatus {
    CCID_BULK_IN b;
    uint8_t     bClockStatus;
} CCID_SlotStatus;

typedef struct QEMU_PACKED CCID_T0ProtocolDataStructure {
    uint8_t     bmFindexDindex;
    uint8_t     bmTCCKST0;
    uint8_t     bGuardTimeT0;
    uint8_t     bWaitingIntegerT0;
    uint8_t     bClockStop;
} CCID_T0ProtocolDataStructure;

typedef struct QEMU_PACKED CCID_T1ProtocolDataStructure {
    uint8_t     bmFindexDindex;
    uint8_t     bmTCCKST1;
    uint8_t     bGuardTimeT1;
    uint8_t     bWaitingIntegerT1;
    uint8_t     bClockStop;
    uint8_t     bIFSC;
    uint8_t     bNadValue;
} CCID_T1ProtocolDataStructure;

typedef union CCID_ProtocolDataStructure {
    CCID_T0ProtocolDataStructure t0;
    CCID_T1ProtocolDataStructure t1;
    uint8_t data[7]; /* must be = max(sizeof(t0), sizeof(t1)) */
} CCID_ProtocolDataStructure;

typedef struct QEMU_PACKED CCID_Parameter {
    CCID_BULK_IN b;
    uint8_t     bProtocolNum;
    CCID_ProtocolDataStructure abProtocolDataStructure;
} CCID_Parameter;

typedef struct QEMU_PACKED CCID_DataBlock {
    CCID_BULK_IN b;
    uint8_t      bChainParameter;
    uint8_t      abData[0];
} CCID_DataBlock;

/* 6.1.4 PC_to_RDR_XfrBlock */
typedef struct QEMU_PACKED CCID_XferBlock {
    CCID_Header  hdr;
    uint8_t      bBWI; /* Block Waiting Timeout */
    uint16_t     wLevelParameter; /* XXX currently unused */
    uint8_t      abData[0];
} CCID_XferBlock;

typedef struct QEMU_PACKED CCID_IccPowerOn {
    CCID_Header hdr;
    uint8_t     bPowerSelect;
    uint16_t    abRFU;
} CCID_IccPowerOn;

typedef struct QEMU_PACKED CCID_IccPowerOff {
    CCID_Header hdr;
    uint16_t    abRFU;
} CCID_IccPowerOff;

typedef struct QEMU_PACKED CCID_SetParameters {
    CCID_Header hdr;
    uint8_t     bProtocolNum;
    uint16_t   abRFU;
    CCID_ProtocolDataStructure abProtocolDataStructure;
} CCID_SetParameters;

typedef struct CCID_Notify_Slot_Change {
    uint8_t     bMessageType; /* CCID_MESSAGE_TYPE_RDR_to_PC_NotifySlotChange */
    uint8_t     bmSlotICCState;
} CCID_Notify_Slot_Change;

/* used for DataBlock response to XferBlock */
typedef struct Answer {
    uint8_t slot;
    uint8_t seq;
} Answer;

/* pending BULK_IN messages */
typedef struct BulkIn {
    uint8_t  data[BULK_IN_BUF_SIZE];
    uint32_t len;
    uint32_t pos;
} BulkIn;

enum {
    MIGRATION_NONE,
    MIGRATION_MIGRATED,
};

typedef struct CCIDBus {
    BusState qbus;
} CCIDBus;

/*
 * powered - defaults to true, changed by PowerOn/PowerOff messages
 */
typedef struct USBCCIDState {
    USBDevice dev;
    USBEndpoint *intr;
    CCIDBus bus;
    CCIDCardState *card;
    BulkIn bulk_in_pending[BULK_IN_PENDING_NUM]; /* circular */
    uint32_t bulk_in_pending_start;
    uint32_t bulk_in_pending_end; /* first free */
    uint32_t bulk_in_pending_num;
    BulkIn *current_bulk_in;
    uint8_t  bulk_out_data[BULK_OUT_DATA_SIZE];
    uint32_t bulk_out_pos;
    uint64_t last_answer_error;
    Answer pending_answers[PENDING_ANSWERS_NUM];
    uint32_t pending_answers_start;
    uint32_t pending_answers_end;
    uint32_t pending_answers_num;
    uint8_t  bError;
    uint8_t  bmCommandStatus;
    uint8_t  bProtocolNum;
    CCID_ProtocolDataStructure abProtocolDataStructure;
    uint32_t ulProtocolDataStructureSize;
    uint32_t state_vmstate;
    uint32_t migration_target_ip;
    uint16_t migration_target_port;
    uint8_t  migration_state;
    uint8_t  bmSlotICCState;
    uint8_t  powered;
    uint8_t  notify_slot_change;
    uint8_t  debug;
} USBCCIDState;

/*
 * CCID Spec chapter 4: CCID uses a standard device descriptor per Chapter 9,
 * "USB Device Framework", section 9.6.1, in the Universal Serial Bus
 * Specification.
 *
 * This device implemented based on the spec and with an Athena Smart Card
 * Reader as reference:
 *   0dc3:1004 Athena Smartcard Solutions, Inc.
 */

static const uint8_t qemu_ccid_descriptor[] = {
        /* Smart Card Device Class Descriptor */
        0x36,       /* u8  bLength; */
        0x21,       /* u8  bDescriptorType; Functional */
        0x10, 0x01, /* u16 bcdCCID; CCID Specification Release Number. */
        0x00,       /*
                     * u8  bMaxSlotIndex; The index of the highest available
                     * slot on this device. All slots are consecutive starting
                     * at 00h.
                     */
        0x07,       /* u8  bVoltageSupport; 01h - 5.0v, 02h - 3.0, 03 - 1.8 */

        0x00, 0x00, /* u32 dwProtocols; RRRR PPPP. RRRR = 0000h.*/
        0x01, 0x00, /* PPPP: 0001h = Protocol T=0, 0002h = Protocol T=1 */
                    /* u32 dwDefaultClock; in kHZ (0x0fa0 is 4 MHz) */
        0xa0, 0x0f, 0x00, 0x00,
                    /* u32 dwMaximumClock; */
        0x00, 0x00, 0x01, 0x00,
        0x00,       /* u8 bNumClockSupported;                 *
                     *    0 means just the default and max.   */
                    /* u32 dwDataRate ;bps. 9600 == 00002580h */
        0x80, 0x25, 0x00, 0x00,
                    /* u32 dwMaxDataRate ; 11520 bps == 0001C200h */
        0x00, 0xC2, 0x01, 0x00,
        0x00,       /* u8  bNumDataRatesSupported; 00 means all rates between
                     *     default and max */
                    /* u32 dwMaxIFSD;                                  *
                     *     maximum IFSD supported by CCID for protocol *
                     *     T=1 (Maximum seen from various cards)       */
        0xfe, 0x00, 0x00, 0x00,
                    /* u32 dwSyncProtocols; 1 - 2-wire, 2 - 3-wire, 4 - I2C */
        0x00, 0x00, 0x00, 0x00,
                    /* u32 dwMechanical;  0 - no special characteristics. */
        0x00, 0x00, 0x00, 0x00,
                    /*
                     * u32 dwFeatures;
                     * 0 - No special characteristics
                     * + 2 Automatic parameter configuration based on ATR data
                     * + 4 Automatic activation of ICC on inserting
                     * + 8 Automatic ICC voltage selection
                     * + 10 Automatic ICC clock frequency change
                     * + 20 Automatic baud rate change
                     * + 40 Automatic parameters negotiation made by the CCID
                     * + 80 automatic PPS made by the CCID
                     * 100 CCID can set ICC in clock stop mode
                     * 200 NAD value other then 00 accepted (T=1 protocol)
                     * + 400 Automatic IFSD exchange as first exchange (T=1)
                     * One of the following only:
                     * + 10000 TPDU level exchanges with CCID
                     * 20000 Short APDU level exchange with CCID
                     * 40000 Short and Extended APDU level exchange with CCID
                     *
                     * 100000 USB Wake up signaling supported on card
                     * insertion and removal. Must set bit 5 in bmAttributes
                     * in Configuration descriptor if 100000 is set.
                     */
        0xfe, 0x04, 0x01, 0x00,
                    /*
                     * u32 dwMaxCCIDMessageLength; For extended APDU in
                     * [261 + 10 , 65544 + 10]. Otherwise the minimum is
                     * wMaxPacketSize of the Bulk-OUT endpoint
                     */
        0x12, 0x00, 0x01, 0x00,
        0xFF,       /*
                     * u8  bClassGetResponse; Significant only for CCID that
                     * offers an APDU level for exchanges. Indicates the
                     * default class value used by the CCID when it sends a
                     * Get Response command to perform the transportation of
                     * an APDU by T=0 protocol
                     * FFh indicates that the CCID echos the class of the APDU.
                     */
        0xFF,       /*
                     * u8  bClassEnvelope; EAPDU only. Envelope command for
                     * T=0
                     */
        0x00, 0x00, /*
                     * u16 wLcdLayout; XXYY Number of lines (XX) and chars per
                     * line for LCD display used for PIN entry. 0000 - no LCD
                     */
        0x01,       /*
                     * u8  bPINSupport; 01h PIN Verification,
                     *                  02h PIN Modification
                     */
        0x01,       /* u8  bMaxCCIDBusySlots; */
};

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
    STR_INTERFACE,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]  = "QEMU",
    [STR_PRODUCT]       = "QEMU USB CCID",
    [STR_SERIALNUMBER]  = "1",
    [STR_INTERFACE]     = "CCID Interface",
};

static const USBDescIface desc_iface0 = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 3,
    .bInterfaceClass               = USB_CLASS_CSCID,
    .bInterfaceSubClass            = USB_SUBCLASS_UNDEFINED,
    .bInterfaceProtocol            = 0x00,
    .iInterface                    = STR_INTERFACE,
    .ndesc                         = 1,
    .descs = (USBDescOther[]) {
        {
            /* smartcard descriptor */
            .data = qemu_ccid_descriptor,
        },
    },
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | CCID_INT_IN_EP,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .bInterval             = 255,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_IN | CCID_BULK_IN_EP,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },{
            .bEndpointAddress      = USB_DIR_OUT | CCID_BULK_OUT_EP,
            .bmAttributes          = USB_ENDPOINT_XFER_BULK,
            .wMaxPacketSize        = 64,
        },
    }
};

static const USBDescDevice desc_device = {
    .bcdUSB                        = 0x0110,
    .bMaxPacketSize0               = 64,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER |
                                     USB_CFG_ATT_WAKEUP,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface0,
        },
    },
};

static const USBDesc desc_ccid = {
    .id = {
        .idVendor          = CCID_VENDOR_ID,
        .idProduct         = CCID_PRODUCT_ID,
        .bcdDevice         = CCID_DEVICE_VERSION,
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device,
    .str  = desc_strings,
};

static const uint8_t *ccid_card_get_atr(CCIDCardState *card, uint32_t *len)
{
    CCIDCardClass *cc = CCID_CARD_GET_CLASS(card);

    if (cc->get_atr) {
        return cc->get_atr(card, len);
    }
    return NULL;
}

static void ccid_card_apdu_from_guest(CCIDCardState *card,
                                      const uint8_t *apdu,
                                      uint32_t len)
{
    CCIDCardClass *cc = CCID_CARD_GET_CLASS(card);

    if (cc->apdu_from_guest) {
        cc->apdu_from_guest(card, apdu, len);
    }
}

static int ccid_card_exitfn(CCIDCardState *card)
{
    CCIDCardClass *cc = CCID_CARD_GET_CLASS(card);

    if (cc->exitfn) {
        return cc->exitfn(card);
    }
    return 0;
}

static int ccid_card_initfn(CCIDCardState *card)
{
    CCIDCardClass *cc = CCID_CARD_GET_CLASS(card);

    if (cc->initfn) {
        return cc->initfn(card);
    }
    return 0;
}

static bool ccid_has_pending_answers(USBCCIDState *s)
{
    return s->pending_answers_num > 0;
}

static void ccid_clear_pending_answers(USBCCIDState *s)
{
    s->pending_answers_num = 0;
    s->pending_answers_start = 0;
    s->pending_answers_end = 0;
}

static void ccid_print_pending_answers(USBCCIDState *s)
{
    Answer *answer;
    int i, count;

    DPRINTF(s, D_VERBOSE, "usb-ccid: pending answers:");
    if (!ccid_has_pending_answers(s)) {
        DPRINTF(s, D_VERBOSE, " empty\n");
        return;
    }
    for (i = s->pending_answers_start, count = s->pending_answers_num ;
         count > 0; count--, i++) {
        answer = &s->pending_answers[i % PENDING_ANSWERS_NUM];
        if (count == 1) {
            DPRINTF(s, D_VERBOSE, "%d:%d\n", answer->slot, answer->seq);
        } else {
            DPRINTF(s, D_VERBOSE, "%d:%d,", answer->slot, answer->seq);
        }
    }
}

static void ccid_add_pending_answer(USBCCIDState *s, CCID_Header *hdr)
{
    Answer *answer;

    assert(s->pending_answers_num < PENDING_ANSWERS_NUM);
    s->pending_answers_num++;
    answer =
        &s->pending_answers[(s->pending_answers_end++) % PENDING_ANSWERS_NUM];
    answer->slot = hdr->bSlot;
    answer->seq = hdr->bSeq;
    ccid_print_pending_answers(s);
}

static void ccid_remove_pending_answer(USBCCIDState *s,
    uint8_t *slot, uint8_t *seq)
{
    Answer *answer;

    assert(s->pending_answers_num > 0);
    s->pending_answers_num--;
    answer =
        &s->pending_answers[(s->pending_answers_start++) % PENDING_ANSWERS_NUM];
    *slot = answer->slot;
    *seq = answer->seq;
    ccid_print_pending_answers(s);
}

static void ccid_bulk_in_clear(USBCCIDState *s)
{
    s->bulk_in_pending_start = 0;
    s->bulk_in_pending_end = 0;
    s->bulk_in_pending_num = 0;
}

static void ccid_bulk_in_release(USBCCIDState *s)
{
    assert(s->current_bulk_in != NULL);
    s->current_bulk_in->pos = 0;
    s->current_bulk_in = NULL;
}

static void ccid_bulk_in_get(USBCCIDState *s)
{
    if (s->current_bulk_in != NULL || s->bulk_in_pending_num == 0) {
        return;
    }
    assert(s->bulk_in_pending_num > 0);
    s->bulk_in_pending_num--;
    s->current_bulk_in =
        &s->bulk_in_pending[(s->bulk_in_pending_start++) % BULK_IN_PENDING_NUM];
}

static void *ccid_reserve_recv_buf(USBCCIDState *s, uint16_t len)
{
    BulkIn *bulk_in;

    DPRINTF(s, D_VERBOSE, "%s: QUEUE: reserve %d bytes\n", __func__, len);

    /* look for an existing element */
    if (len > BULK_IN_BUF_SIZE) {
        DPRINTF(s, D_WARN, "usb-ccid.c: %s: len larger then max (%d>%d). "
                           "discarding message.\n",
                           __func__, len, BULK_IN_BUF_SIZE);
        return NULL;
    }
    if (s->bulk_in_pending_num >= BULK_IN_PENDING_NUM) {
        DPRINTF(s, D_WARN, "usb-ccid.c: %s: No free bulk_in buffers. "
                           "discarding message.\n", __func__);
        return NULL;
    }
    bulk_in =
        &s->bulk_in_pending[(s->bulk_in_pending_end++) % BULK_IN_PENDING_NUM];
    s->bulk_in_pending_num++;
    bulk_in->len = len;
    return bulk_in->data;
}

static void ccid_reset(USBCCIDState *s)
{
    ccid_bulk_in_clear(s);
    ccid_clear_pending_answers(s);
}

static void ccid_detach(USBCCIDState *s)
{
    ccid_reset(s);
}

static void ccid_handle_reset(USBDevice *dev)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);

    DPRINTF(s, 1, "Reset\n");

    ccid_reset(s);
}

static const char *ccid_control_to_str(USBCCIDState *s, int request)
{
    switch (request) {
        /* generic - should be factored out if there are other debugees */
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        return "(generic) set address";
    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        return "(generic) get descriptor";
    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        return "(generic) get configuration";
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        return "(generic) set configuration";
    case DeviceRequest | USB_REQ_GET_STATUS:
        return "(generic) get status";
    case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
        return "(generic) clear feature";
    case DeviceOutRequest | USB_REQ_SET_FEATURE:
        return "(generic) set_feature";
    case InterfaceRequest | USB_REQ_GET_INTERFACE:
        return "(generic) get interface";
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        return "(generic) set interface";
        /* class requests */
    case ClassInterfaceOutRequest | CCID_CONTROL_ABORT:
        return "ABORT";
    case ClassInterfaceRequest | CCID_CONTROL_GET_CLOCK_FREQUENCIES:
        return "GET_CLOCK_FREQUENCIES";
    case ClassInterfaceRequest | CCID_CONTROL_GET_DATA_RATES:
        return "GET_DATA_RATES";
    }
    return "unknown";
}

static void ccid_handle_control(USBDevice *dev, USBPacket *p, int request,
                               int value, int index, int length, uint8_t *data)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);
    int ret;

    DPRINTF(s, 1, "%s: got control %s (%x), value %x\n", __func__,
            ccid_control_to_str(s, request), request, value);
    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return;
    }

    switch (request) {
        /* Class specific requests.  */
    case ClassInterfaceOutRequest | CCID_CONTROL_ABORT:
        DPRINTF(s, 1, "ccid_control abort UNIMPLEMENTED\n");
        p->status = USB_RET_STALL;
        break;
    case ClassInterfaceRequest | CCID_CONTROL_GET_CLOCK_FREQUENCIES:
        DPRINTF(s, 1, "ccid_control get clock frequencies UNIMPLEMENTED\n");
        p->status = USB_RET_STALL;
        break;
    case ClassInterfaceRequest | CCID_CONTROL_GET_DATA_RATES:
        DPRINTF(s, 1, "ccid_control get data rates UNIMPLEMENTED\n");
        p->status = USB_RET_STALL;
        break;
    default:
        DPRINTF(s, 1, "got unsupported/bogus control %x, value %x\n",
                request, value);
        p->status = USB_RET_STALL;
        break;
    }
}

static bool ccid_card_inserted(USBCCIDState *s)
{
    return s->bmSlotICCState & SLOT_0_STATE_MASK;
}

static uint8_t ccid_card_status(USBCCIDState *s)
{
    return ccid_card_inserted(s)
            ? (s->powered ?
                ICC_STATUS_PRESENT_ACTIVE
              : ICC_STATUS_PRESENT_INACTIVE
              )
            : ICC_STATUS_NOT_PRESENT;
}

static uint8_t ccid_calc_status(USBCCIDState *s)
{
    /*
     * page 55, 6.2.6, calculation of bStatus from bmICCStatus and
     * bmCommandStatus
     */
    uint8_t ret = ccid_card_status(s) | (s->bmCommandStatus << 6);
    DPRINTF(s, D_VERBOSE, "%s: status = %d\n", __func__, ret);
    return ret;
}

static void ccid_reset_error_status(USBCCIDState *s)
{
    s->bError = ERROR_CMD_NOT_SUPPORTED;
    s->bmCommandStatus = COMMAND_STATUS_NO_ERROR;
}

static void ccid_write_slot_status(USBCCIDState *s, CCID_Header *recv)
{
    CCID_SlotStatus *h = ccid_reserve_recv_buf(s, sizeof(CCID_SlotStatus));
    if (h == NULL) {
        return;
    }
    h->b.hdr.bMessageType = CCID_MESSAGE_TYPE_RDR_to_PC_SlotStatus;
    h->b.hdr.dwLength = 0;
    h->b.hdr.bSlot = recv->bSlot;
    h->b.hdr.bSeq = recv->bSeq;
    h->b.bStatus = ccid_calc_status(s);
    h->b.bError = s->bError;
    h->bClockStatus = CLOCK_STATUS_RUNNING;
    ccid_reset_error_status(s);
}

static void ccid_write_parameters(USBCCIDState *s, CCID_Header *recv)
{
    CCID_Parameter *h;
    uint32_t len = s->ulProtocolDataStructureSize;

    h = ccid_reserve_recv_buf(s, sizeof(CCID_Parameter) + len);
    if (h == NULL) {
        return;
    }
    h->b.hdr.bMessageType = CCID_MESSAGE_TYPE_RDR_to_PC_Parameters;
    h->b.hdr.dwLength = 0;
    h->b.hdr.bSlot = recv->bSlot;
    h->b.hdr.bSeq = recv->bSeq;
    h->b.bStatus = ccid_calc_status(s);
    h->b.bError = s->bError;
    h->bProtocolNum = s->bProtocolNum;
    h->abProtocolDataStructure = s->abProtocolDataStructure;
    ccid_reset_error_status(s);
}

static void ccid_write_data_block(USBCCIDState *s, uint8_t slot, uint8_t seq,
                                  const uint8_t *data, uint32_t len)
{
    CCID_DataBlock *p = ccid_reserve_recv_buf(s, sizeof(*p) + len);

    if (p == NULL) {
        return;
    }
    p->b.hdr.bMessageType = CCID_MESSAGE_TYPE_RDR_to_PC_DataBlock;
    p->b.hdr.dwLength = cpu_to_le32(len);
    p->b.hdr.bSlot = slot;
    p->b.hdr.bSeq = seq;
    p->b.bStatus = ccid_calc_status(s);
    p->b.bError = s->bError;
    if (p->b.bError) {
        DPRINTF(s, D_VERBOSE, "error %d\n", p->b.bError);
    }
    memcpy(p->abData, data, len);
    ccid_reset_error_status(s);
}

static void ccid_report_error_failed(USBCCIDState *s, uint8_t error)
{
    s->bmCommandStatus = COMMAND_STATUS_FAILED;
    s->bError = error;
}

static void ccid_write_data_block_answer(USBCCIDState *s,
    const uint8_t *data, uint32_t len)
{
    uint8_t seq;
    uint8_t slot;

    if (!ccid_has_pending_answers(s)) {
        DPRINTF(s, D_WARN, "error: no pending answer to return to guest\n");
        ccid_report_error_failed(s, ERROR_ICC_MUTE);
        return;
    }
    ccid_remove_pending_answer(s, &slot, &seq);
    ccid_write_data_block(s, slot, seq, data, len);
}

static uint8_t atr_get_protocol_num(const uint8_t *atr, uint32_t len)
{
    int i;

    if (len < 2 || !(atr[1] & 0x80)) {
        /* too short or TD1 not included */
        return 0; /* T=0, default */
    }
    i = 1 + !!(atr[1] & 0x10) + !!(atr[1] & 0x20) + !!(atr[1] & 0x40);
    i += !!(atr[1] & 0x80);
    return atr[i] & 0x0f;
}

static void ccid_write_data_block_atr(USBCCIDState *s, CCID_Header *recv)
{
    const uint8_t *atr = NULL;
    uint32_t len = 0;
    uint8_t atr_protocol_num;
    CCID_T0ProtocolDataStructure *t0 = &s->abProtocolDataStructure.t0;
    CCID_T1ProtocolDataStructure *t1 = &s->abProtocolDataStructure.t1;

    if (s->card) {
        atr = ccid_card_get_atr(s->card, &len);
    }
    atr_protocol_num = atr_get_protocol_num(atr, len);
    DPRINTF(s, D_VERBOSE, "%s: atr contains protocol=%d\n", __func__,
            atr_protocol_num);
    /* set parameters from ATR - see spec page 109 */
    s->bProtocolNum = (atr_protocol_num <= 1 ? atr_protocol_num
                                             : s->bProtocolNum);
    switch (atr_protocol_num) {
    case 0:
        /* TODO: unimplemented ATR T0 parameters */
        t0->bmFindexDindex = 0;
        t0->bmTCCKST0 = 0;
        t0->bGuardTimeT0 = 0;
        t0->bWaitingIntegerT0 = 0;
        t0->bClockStop = 0;
        break;
    case 1:
        /* TODO: unimplemented ATR T1 parameters */
        t1->bmFindexDindex = 0;
        t1->bmTCCKST1 = 0;
        t1->bGuardTimeT1 = 0;
        t1->bWaitingIntegerT1 = 0;
        t1->bClockStop = 0;
        t1->bIFSC = 0;
        t1->bNadValue = 0;
        break;
    default:
        DPRINTF(s, D_WARN, "%s: error: unsupported ATR protocol %d\n",
                __func__, atr_protocol_num);
    }
    ccid_write_data_block(s, recv->bSlot, recv->bSeq, atr, len);
}

static void ccid_set_parameters(USBCCIDState *s, CCID_Header *recv)
{
    CCID_SetParameters *ph = (CCID_SetParameters *) recv;
    uint32_t protocol_num = ph->bProtocolNum & 3;

    if (protocol_num != 0 && protocol_num != 1) {
        ccid_report_error_failed(s, ERROR_CMD_NOT_SUPPORTED);
        return;
    }
    s->bProtocolNum = protocol_num;
    s->abProtocolDataStructure = ph->abProtocolDataStructure;
}

/*
 * must be 5 bytes for T=0, 7 bytes for T=1
 * See page 52
 */
static const CCID_ProtocolDataStructure defaultProtocolDataStructure = {
    .t1 = {
        .bmFindexDindex = 0x77,
        .bmTCCKST1 = 0x00,
        .bGuardTimeT1 = 0x00,
        .bWaitingIntegerT1 = 0x00,
        .bClockStop = 0x00,
        .bIFSC = 0xfe,
        .bNadValue = 0x00,
    }
};

static void ccid_reset_parameters(USBCCIDState *s)
{
   s->bProtocolNum = 0; /* T=0 */
   s->abProtocolDataStructure = defaultProtocolDataStructure;
}

/* NOTE: only a single slot is supported (SLOT_0) */
static void ccid_on_slot_change(USBCCIDState *s, bool full)
{
    /* RDR_to_PC_NotifySlotChange, 6.3.1 page 56 */
    uint8_t current = s->bmSlotICCState;
    if (full) {
        s->bmSlotICCState |= SLOT_0_STATE_MASK;
    } else {
        s->bmSlotICCState &= ~SLOT_0_STATE_MASK;
    }
    if (current != s->bmSlotICCState) {
        s->bmSlotICCState |= SLOT_0_CHANGED_MASK;
    }
    s->notify_slot_change = true;
    usb_wakeup(s->intr, 0);
}

static void ccid_write_data_block_error(
    USBCCIDState *s, uint8_t slot, uint8_t seq)
{
    ccid_write_data_block(s, slot, seq, NULL, 0);
}

static void ccid_on_apdu_from_guest(USBCCIDState *s, CCID_XferBlock *recv)
{
    uint32_t len;

    if (ccid_card_status(s) != ICC_STATUS_PRESENT_ACTIVE) {
        DPRINTF(s, 1,
                "usb-ccid: not sending apdu to client, no card connected\n");
        ccid_write_data_block_error(s, recv->hdr.bSlot, recv->hdr.bSeq);
        return;
    }
    len = le32_to_cpu(recv->hdr.dwLength);
    DPRINTF(s, 1, "%s: seq %d, len %d\n", __func__,
                recv->hdr.bSeq, len);
    ccid_add_pending_answer(s, (CCID_Header *)recv);
    if (s->card) {
        ccid_card_apdu_from_guest(s->card, recv->abData, len);
    } else {
        DPRINTF(s, D_WARN, "warning: discarded apdu\n");
    }
}

static const char *ccid_message_type_to_str(uint8_t type)
{
    switch (type) {
    case CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOn: return "IccPowerOn";
    case CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOff: return "IccPowerOff";
    case CCID_MESSAGE_TYPE_PC_to_RDR_GetSlotStatus: return "GetSlotStatus";
    case CCID_MESSAGE_TYPE_PC_to_RDR_XfrBlock: return "XfrBlock";
    case CCID_MESSAGE_TYPE_PC_to_RDR_GetParameters: return "GetParameters";
    case CCID_MESSAGE_TYPE_PC_to_RDR_ResetParameters: return "ResetParameters";
    case CCID_MESSAGE_TYPE_PC_to_RDR_SetParameters: return "SetParameters";
    case CCID_MESSAGE_TYPE_PC_to_RDR_Escape: return "Escape";
    case CCID_MESSAGE_TYPE_PC_to_RDR_IccClock: return "IccClock";
    case CCID_MESSAGE_TYPE_PC_to_RDR_T0APDU: return "T0APDU";
    case CCID_MESSAGE_TYPE_PC_to_RDR_Secure: return "Secure";
    case CCID_MESSAGE_TYPE_PC_to_RDR_Mechanical: return "Mechanical";
    case CCID_MESSAGE_TYPE_PC_to_RDR_Abort: return "Abort";
    case CCID_MESSAGE_TYPE_PC_to_RDR_SetDataRateAndClockFrequency:
        return "SetDataRateAndClockFrequency";
    }
    return "unknown";
}

static void ccid_handle_bulk_out(USBCCIDState *s, USBPacket *p)
{
    CCID_Header *ccid_header;

    if (p->iov.size + s->bulk_out_pos > BULK_OUT_DATA_SIZE) {
        p->status = USB_RET_STALL;
        return;
    }
    ccid_header = (CCID_Header *)s->bulk_out_data;
    usb_packet_copy(p, s->bulk_out_data + s->bulk_out_pos, p->iov.size);
    s->bulk_out_pos += p->iov.size;
    if (p->iov.size == CCID_MAX_PACKET_SIZE) {
        DPRINTF(s, D_VERBOSE,
            "usb-ccid: bulk_in: expecting more packets (%zd/%d)\n",
            p->iov.size, ccid_header->dwLength);
        return;
    }
    if (s->bulk_out_pos < 10) {
        DPRINTF(s, 1,
                "%s: bad USB_TOKEN_OUT length, should be at least 10 bytes\n",
                __func__);
    } else {
        DPRINTF(s, D_MORE_INFO, "%s %x %s\n", __func__,
                ccid_header->bMessageType,
                ccid_message_type_to_str(ccid_header->bMessageType));
        switch (ccid_header->bMessageType) {
        case CCID_MESSAGE_TYPE_PC_to_RDR_GetSlotStatus:
            ccid_write_slot_status(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOn:
            DPRINTF(s, 1, "%s: PowerOn: %d\n", __func__,
                ((CCID_IccPowerOn *)(ccid_header))->bPowerSelect);
            s->powered = true;
            if (!ccid_card_inserted(s)) {
                ccid_report_error_failed(s, ERROR_ICC_MUTE);
            }
            /* atr is written regardless of error. */
            ccid_write_data_block_atr(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOff:
            ccid_reset_error_status(s);
            s->powered = false;
            ccid_write_slot_status(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_XfrBlock:
            ccid_on_apdu_from_guest(s, (CCID_XferBlock *)s->bulk_out_data);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_SetParameters:
            ccid_reset_error_status(s);
            ccid_set_parameters(s, ccid_header);
            ccid_write_parameters(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_ResetParameters:
            ccid_reset_error_status(s);
            ccid_reset_parameters(s);
            ccid_write_parameters(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_GetParameters:
            ccid_reset_error_status(s);
            ccid_write_parameters(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_Mechanical:
            ccid_report_error_failed(s, 0);
            ccid_write_slot_status(s, ccid_header);
            break;
        default:
            DPRINTF(s, 1,
                "handle_data: ERROR: unhandled message type %Xh\n",
                ccid_header->bMessageType);
            /*
             * The caller is expecting the device to respond, tell it we
             * don't support the operation.
             */
            ccid_report_error_failed(s, ERROR_CMD_NOT_SUPPORTED);
            ccid_write_slot_status(s, ccid_header);
            break;
        }
    }
    s->bulk_out_pos = 0;
}

static void ccid_bulk_in_copy_to_guest(USBCCIDState *s, USBPacket *p)
{
    int len = 0;

    ccid_bulk_in_get(s);
    if (s->current_bulk_in != NULL) {
        len = MIN(s->current_bulk_in->len - s->current_bulk_in->pos,
                  p->iov.size);
        usb_packet_copy(p, s->current_bulk_in->data +
                        s->current_bulk_in->pos, len);
        s->current_bulk_in->pos += len;
        if (s->current_bulk_in->pos == s->current_bulk_in->len) {
            ccid_bulk_in_release(s);
        }
    } else {
        /* return when device has no data - usb 2.0 spec Table 8-4 */
        p->status = USB_RET_NAK;
    }
    if (len) {
        DPRINTF(s, D_MORE_INFO,
                "%s: %zd/%d req/act to guest (BULK_IN)\n",
                __func__, p->iov.size, len);
    }
    if (len < p->iov.size) {
        DPRINTF(s, 1,
                "%s: returning short (EREMOTEIO) %d < %zd\n",
                __func__, len, p->iov.size);
    }
}

static void ccid_handle_data(USBDevice *dev, USBPacket *p)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);
    uint8_t buf[2];

    switch (p->pid) {
    case USB_TOKEN_OUT:
        ccid_handle_bulk_out(s, p);
        break;

    case USB_TOKEN_IN:
        switch (p->ep->nr) {
        case CCID_BULK_IN_EP:
            ccid_bulk_in_copy_to_guest(s, p);
            break;
        case CCID_INT_IN_EP:
            if (s->notify_slot_change) {
                /* page 56, RDR_to_PC_NotifySlotChange */
                buf[0] = CCID_MESSAGE_TYPE_RDR_to_PC_NotifySlotChange;
                buf[1] = s->bmSlotICCState;
                usb_packet_copy(p, buf, 2);
                s->notify_slot_change = false;
                s->bmSlotICCState &= ~SLOT_0_CHANGED_MASK;
                DPRINTF(s, D_INFO,
                        "handle_data: int_in: notify_slot_change %X, "
                        "requested len %zd\n",
                        s->bmSlotICCState, p->iov.size);
            } else {
                p->status = USB_RET_NAK;
            }
            break;
        default:
            DPRINTF(s, 1, "Bad endpoint\n");
            p->status = USB_RET_STALL;
            break;
        }
        break;
    default:
        DPRINTF(s, 1, "Bad token\n");
        p->status = USB_RET_STALL;
        break;
    }
}

static void ccid_handle_destroy(USBDevice *dev)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);

    ccid_bulk_in_clear(s);
}

static void ccid_flush_pending_answers(USBCCIDState *s)
{
    while (ccid_has_pending_answers(s)) {
        ccid_write_data_block_answer(s, NULL, 0);
    }
}

static Answer *ccid_peek_next_answer(USBCCIDState *s)
{
    return s->pending_answers_num == 0
        ? NULL
        : &s->pending_answers[s->pending_answers_start % PENDING_ANSWERS_NUM];
}

static Property ccid_props[] = {
    DEFINE_PROP_UINT32("slot", struct CCIDCardState, slot, 0),
    DEFINE_PROP_END_OF_LIST(),
};

#define TYPE_CCID_BUS "ccid-bus"
#define CCID_BUS(obj) OBJECT_CHECK(CCIDBus, (obj), TYPE_CCID_BUS)

static const TypeInfo ccid_bus_info = {
    .name = TYPE_CCID_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(CCIDBus),
};

void ccid_card_send_apdu_to_guest(CCIDCardState *card,
                                  uint8_t *apdu, uint32_t len)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev.qdev,
                                card->qdev.parent_bus->parent);
    Answer *answer;

    if (!ccid_has_pending_answers(s)) {
        DPRINTF(s, 1, "CCID ERROR: got an APDU without pending answers\n");
        return;
    }
    s->bmCommandStatus = COMMAND_STATUS_NO_ERROR;
    answer = ccid_peek_next_answer(s);
    if (answer == NULL) {
        DPRINTF(s, D_WARN, "%s: error: unexpected lack of answer\n", __func__);
        ccid_report_error_failed(s, ERROR_HW_ERROR);
        return;
    }
    DPRINTF(s, 1, "APDU returned to guest %d (answer seq %d, slot %d)\n",
        len, answer->seq, answer->slot);
    ccid_write_data_block_answer(s, apdu, len);
}

void ccid_card_card_removed(CCIDCardState *card)
{
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);

    ccid_on_slot_change(s, false);
    ccid_flush_pending_answers(s);
    ccid_reset(s);
}

int ccid_card_ccid_attach(CCIDCardState *card)
{
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);

    DPRINTF(s, 1, "CCID Attach\n");
    if (s->migration_state == MIGRATION_MIGRATED) {
        s->migration_state = MIGRATION_NONE;
    }
    return 0;
}

void ccid_card_ccid_detach(CCIDCardState *card)
{
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);

    DPRINTF(s, 1, "CCID Detach\n");
    if (ccid_card_inserted(s)) {
        ccid_on_slot_change(s, false);
    }
    ccid_detach(s);
}

void ccid_card_card_error(CCIDCardState *card, uint64_t error)
{
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);

    s->bmCommandStatus = COMMAND_STATUS_FAILED;
    s->last_answer_error = error;
    DPRINTF(s, 1, "VSC_Error: %" PRIX64 "\n", s->last_answer_error);
    /* TODO: these errors should be more verbose and propagated to the guest.*/
    /*
     * We flush all pending answers on CardRemove message in ccid-card-passthru,
     * so check that first to not trigger abort
     */
    if (ccid_has_pending_answers(s)) {
        ccid_write_data_block_answer(s, NULL, 0);
    }
}

void ccid_card_card_inserted(CCIDCardState *card)
{
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);

    s->bmCommandStatus = COMMAND_STATUS_NO_ERROR;
    ccid_flush_pending_answers(s);
    ccid_on_slot_change(s, true);
}

static int ccid_card_exit(DeviceState *qdev)
{
    int ret = 0;
    CCIDCardState *card = CCID_CARD(qdev);
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);

    if (ccid_card_inserted(s)) {
        ccid_card_card_removed(card);
    }
    ret = ccid_card_exitfn(card);
    s->card = NULL;
    return ret;
}

static int ccid_card_init(DeviceState *qdev)
{
    CCIDCardState *card = CCID_CARD(qdev);
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);
    int ret = 0;

    if (card->slot != 0) {
        error_report("Warning: usb-ccid supports one slot, can't add %d",
                card->slot);
        return -1;
    }
    if (s->card != NULL) {
        error_report("Warning: usb-ccid card already full, not adding");
        return -1;
    }
    ret = ccid_card_initfn(card);
    if (ret == 0) {
        s->card = card;
    }
    return ret;
}

static int ccid_initfn(USBDevice *dev)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);

    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    qbus_create_inplace(&s->bus, sizeof(s->bus), TYPE_CCID_BUS, DEVICE(dev),
                        NULL);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, CCID_INT_IN_EP);
    s->bus.qbus.allow_hotplug = 1;
    s->card = NULL;
    s->migration_state = MIGRATION_NONE;
    s->migration_target_ip = 0;
    s->migration_target_port = 0;
    s->dev.speed = USB_SPEED_FULL;
    s->dev.speedmask = USB_SPEED_MASK_FULL;
    s->notify_slot_change = false;
    s->powered = true;
    s->pending_answers_num = 0;
    s->last_answer_error = 0;
    s->bulk_in_pending_start = 0;
    s->bulk_in_pending_end = 0;
    s->current_bulk_in = NULL;
    ccid_reset_error_status(s);
    s->bulk_out_pos = 0;
    ccid_reset_parameters(s);
    ccid_reset(s);
    s->debug = parse_debug_env("QEMU_CCID_DEBUG", D_VERBOSE, s->debug);
    return 0;
}

static int ccid_post_load(void *opaque, int version_id)
{
    USBCCIDState *s = opaque;

    /*
     * This must be done after usb_device_attach, which sets state to ATTACHED,
     * while it must be DEFAULT in order to accept packets (like it is after
     * reset, but reset will reset our addr and call our reset handler which
     * may change state, and we don't want to do that when migrating).
     */
    s->dev.state = s->state_vmstate;
    return 0;
}

static void ccid_pre_save(void *opaque)
{
    USBCCIDState *s = opaque;

    s->state_vmstate = s->dev.state;
    if (s->dev.attached) {
        /*
         * Migrating an open device, ignore reconnection CHR_EVENT to avoid an
         * erroneous detach.
         */
        s->migration_state = MIGRATION_MIGRATED;
    }
}

static VMStateDescription bulk_in_vmstate = {
    .name = "CCID BulkIn state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_BUFFER(data, BulkIn),
        VMSTATE_UINT32(len, BulkIn),
        VMSTATE_UINT32(pos, BulkIn),
        VMSTATE_END_OF_LIST()
    }
};

static VMStateDescription answer_vmstate = {
    .name = "CCID Answer state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(slot, Answer),
        VMSTATE_UINT8(seq, Answer),
        VMSTATE_END_OF_LIST()
    }
};

static VMStateDescription usb_device_vmstate = {
    .name = "usb_device",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(addr, USBDevice),
        VMSTATE_BUFFER(setup_buf, USBDevice),
        VMSTATE_BUFFER(data_buf, USBDevice),
        VMSTATE_END_OF_LIST()
    }
};

static VMStateDescription ccid_vmstate = {
    .name = "usb-ccid",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ccid_post_load,
    .pre_save = ccid_pre_save,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(dev, USBCCIDState, 1, usb_device_vmstate, USBDevice),
        VMSTATE_UINT8(debug, USBCCIDState),
        VMSTATE_BUFFER(bulk_out_data, USBCCIDState),
        VMSTATE_UINT32(bulk_out_pos, USBCCIDState),
        VMSTATE_UINT8(bmSlotICCState, USBCCIDState),
        VMSTATE_UINT8(powered, USBCCIDState),
        VMSTATE_UINT8(notify_slot_change, USBCCIDState),
        VMSTATE_UINT64(last_answer_error, USBCCIDState),
        VMSTATE_UINT8(bError, USBCCIDState),
        VMSTATE_UINT8(bmCommandStatus, USBCCIDState),
        VMSTATE_UINT8(bProtocolNum, USBCCIDState),
        VMSTATE_BUFFER(abProtocolDataStructure.data, USBCCIDState),
        VMSTATE_UINT32(ulProtocolDataStructureSize, USBCCIDState),
        VMSTATE_STRUCT_ARRAY(bulk_in_pending, USBCCIDState,
                       BULK_IN_PENDING_NUM, 1, bulk_in_vmstate, BulkIn),
        VMSTATE_UINT32(bulk_in_pending_start, USBCCIDState),
        VMSTATE_UINT32(bulk_in_pending_end, USBCCIDState),
        VMSTATE_STRUCT_ARRAY(pending_answers, USBCCIDState,
                        PENDING_ANSWERS_NUM, 1, answer_vmstate, Answer),
        VMSTATE_UINT32(pending_answers_num, USBCCIDState),
        VMSTATE_UINT8(migration_state, USBCCIDState),
        VMSTATE_UINT32(state_vmstate, USBCCIDState),
        VMSTATE_END_OF_LIST()
    }
};

static Property ccid_properties[] = {
    DEFINE_PROP_UINT8("debug", USBCCIDState, debug, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ccid_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->init           = ccid_initfn;
    uc->product_desc   = "QEMU USB CCID";
    uc->usb_desc       = &desc_ccid;
    uc->handle_reset   = ccid_handle_reset;
    uc->handle_control = ccid_handle_control;
    uc->handle_data    = ccid_handle_data;
    uc->handle_destroy = ccid_handle_destroy;
    dc->desc = "CCID Rev 1.1 smartcard reader";
    dc->vmsd = &ccid_vmstate;
    dc->props = ccid_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo ccid_info = {
    .name          = CCID_DEV_NAME,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBCCIDState),
    .class_init    = ccid_class_initfn,
};

static void ccid_card_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->bus_type = TYPE_CCID_BUS;
    k->init = ccid_card_init;
    k->exit = ccid_card_exit;
    k->props = ccid_props;
}

static const TypeInfo ccid_card_type_info = {
    .name = TYPE_CCID_CARD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(CCIDCardState),
    .abstract = true,
    .class_size = sizeof(CCIDCardClass),
    .class_init = ccid_card_class_init,
};

static void ccid_register_types(void)
{
    type_register_static(&ccid_bus_info);
    type_register_static(&ccid_card_type_info);
    type_register_static(&ccid_info);
    usb_legacy_register(CCID_DEV_NAME, "ccid", NULL);
}

type_init(ccid_register_types)
