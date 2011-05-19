/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * CCID Device emulation
 *
 * Written by Alon Levy, with contributions from Robert Relyea.
 *
 * Based on usb-serial.c, see it's copyright and attributions below.
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
#include "qemu-error.h"
#include "usb.h"
#include "monitor.h"

#include "hw/ccid.h"

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

#define InterfaceOutClass \
    ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)<<8)

#define InterfaceInClass  \
    ((USB_DIR_IN  | USB_TYPE_CLASS | USB_RECIP_INTERFACE)<<8)

#define CCID_MAX_PACKET_SIZE                64

#define CCID_CONTROL_ABORT                  0x1
#define CCID_CONTROL_GET_CLOCK_FREQUENCIES  0x2
#define CCID_CONTROL_GET_DATA_RATES         0x3

#define CCID_PRODUCT_DESCRIPTION        "QEMU USB CCID"
#define CCID_VENDOR_DESCRIPTION         "QEMU " QEMU_VERSION
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

typedef struct __attribute__ ((__packed__)) CCID_Header {
    uint8_t     bMessageType;
    uint32_t    dwLength;
    uint8_t     bSlot;
    uint8_t     bSeq;
} CCID_Header;

typedef struct __attribute__ ((__packed__)) CCID_BULK_IN {
    CCID_Header hdr;
    uint8_t     bStatus;        /* Only used in BULK_IN */
    uint8_t     bError;         /* Only used in BULK_IN */
} CCID_BULK_IN;

typedef struct __attribute__ ((__packed__)) CCID_SlotStatus {
    CCID_BULK_IN b;
    uint8_t     bClockStatus;
} CCID_SlotStatus;

typedef struct __attribute__ ((__packed__)) CCID_Parameter {
    CCID_BULK_IN b;
    uint8_t     bProtocolNum;
    uint8_t     abProtocolDataStructure[0];
} CCID_Parameter;

typedef struct __attribute__ ((__packed__)) CCID_DataBlock {
    CCID_BULK_IN b;
    uint8_t      bChainParameter;
    uint8_t      abData[0];
} CCID_DataBlock;

/* 6.1.4 PC_to_RDR_XfrBlock */
typedef struct __attribute__ ((__packed__)) CCID_XferBlock {
    CCID_Header  hdr;
    uint8_t      bBWI; /* Block Waiting Timeout */
    uint16_t     wLevelParameter; /* XXX currently unused */
    uint8_t      abData[0];
} CCID_XferBlock;

typedef struct __attribute__ ((__packed__)) CCID_IccPowerOn {
    CCID_Header hdr;
    uint8_t     bPowerSelect;
    uint16_t    abRFU;
} CCID_IccPowerOn;

typedef struct __attribute__ ((__packed__)) CCID_IccPowerOff {
    CCID_Header hdr;
    uint16_t    abRFU;
} CCID_IccPowerOff;

typedef struct __attribute__ ((__packed__)) CCID_SetParameters {
    CCID_Header hdr;
    uint8_t     bProtocolNum;
    uint16_t   abRFU;
    uint8_t    abProtocolDataStructure[0];
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

#define MAX_PROTOCOL_SIZE   7

/*
 * powered - defaults to true, changed by PowerOn/PowerOff messages
 */
typedef struct USBCCIDState {
    USBDevice dev;
    CCIDBus bus;
    CCIDCardState *card;
    CCIDCardInfo *cardinfo; /* caching the info pointer */
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
    uint8_t  abProtocolDataStructure[MAX_PROTOCOL_SIZE];
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

static const uint8_t qemu_ccid_dev_descriptor[] = {
        0x12,       /*  u8 bLength; */
        USB_DT_DEVICE, /*  u8 bDescriptorType; Device */
        0x10, 0x01, /*  u16 bcdUSB; v1.1 */

        0x00,       /*  u8  bDeviceClass; */
        0x00,       /*  u8  bDeviceSubClass; */
        0x00,       /*  u8  bDeviceProtocol; [ low/full speeds only ] */
        0x40,       /*  u8  bMaxPacketSize0; 8 Bytes (valid: 8,16,32,64) */

        /* Vendor and product id are arbitrary.  */
                    /*  u16 idVendor  */
        CCID_VENDOR_ID & 0xff, CCID_VENDOR_ID >> 8,
                    /*  u16 idProduct */
        CCID_PRODUCT_ID & 0xff, CCID_PRODUCT_ID >> 8,
                    /*  u16 bcdDevice */
        CCID_DEVICE_VERSION & 0xff, CCID_DEVICE_VERSION >> 8,
        0x01,       /*  u8  iManufacturer; */
        0x02,       /*  u8  iProduct; */
        0x03,       /*  u8  iSerialNumber; */
        0x01,       /*  u8  bNumConfigurations; */
};

static const uint8_t qemu_ccid_config_descriptor[] = {

        /* one configuration */
        0x09,       /* u8  bLength; */
        USB_DT_CONFIG, /* u8  bDescriptorType; Configuration */
        0x5d, 0x00, /* u16 wTotalLength; 9+9+54+7+7+7 */
        0x01,       /* u8  bNumInterfaces; (1) */
        0x01,       /* u8  bConfigurationValue; */
        0x00,       /* u8  iConfiguration; */
        0xe0,       /* u8  bmAttributes;
                                 Bit 7: must be set,
                                     6: Self-powered,
                                     5: Remote wakeup,
                                     4..0: resvd */
        100/2,      /* u8  MaxPower; 50 == 100mA */

        /* one interface */
        0x09,       /* u8  if_bLength; */
        USB_DT_INTERFACE, /* u8  if_bDescriptorType; Interface */
        0x00,       /* u8  if_bInterfaceNumber; */
        0x00,       /* u8  if_bAlternateSetting; */
        0x03,       /* u8  if_bNumEndpoints; */
        0x0b,       /* u8  if_bInterfaceClass; Smart Card Device Class */
        0x00,       /* u8  if_bInterfaceSubClass; Subclass code */
        0x00,       /* u8  if_bInterfaceProtocol; Protocol code */
        0x04,       /* u8  if_iInterface; Index of string descriptor */

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

        0x03, 0x00, /* u32 dwProtocols; RRRR PPPP. RRRR = 0000h.*/
        0x00, 0x00, /* PPPP: 0001h = Protocol T=0, 0002h = Protocol T=1 */
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
                     * + 100000 USB Wake up signaling supported on card
                     * insertion and removal. Must set bit 5 in bmAttributes
                     * in Configuration descriptor if 100000 is set.
                     */
        0xfe, 0x04, 0x11, 0x00,
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

        /* Interrupt-IN endpoint */
        0x07,       /* u8  ep_bLength; */
                    /* u8  ep_bDescriptorType; Endpoint */
        USB_DT_ENDPOINT,
                    /* u8  ep_bEndpointAddress; IN Endpoint 1 */
        0x80 | CCID_INT_IN_EP,
        0x03,       /* u8  ep_bmAttributes; Interrupt */
                    /* u16 ep_wMaxPacketSize; */
        CCID_MAX_PACKET_SIZE & 0xff, (CCID_MAX_PACKET_SIZE >> 8),
        0xff,       /* u8  ep_bInterval; */

        /* Bulk-In endpoint */
        0x07,       /* u8  ep_bLength; */
                    /* u8  ep_bDescriptorType; Endpoint */
        USB_DT_ENDPOINT,
                    /* u8  ep_bEndpointAddress; IN Endpoint 2 */
        0x80 | CCID_BULK_IN_EP,
        0x02,       /* u8  ep_bmAttributes; Bulk */
        0x40, 0x00, /* u16 ep_wMaxPacketSize; */
        0x00,       /* u8  ep_bInterval; */

        /* Bulk-Out endpoint */
        0x07,       /* u8  ep_bLength; */
                    /* u8  ep_bDescriptorType; Endpoint */
        USB_DT_ENDPOINT,
                    /* u8  ep_bEndpointAddress; OUT Endpoint 3 */
        CCID_BULK_OUT_EP,
        0x02,       /* u8  ep_bmAttributes; Bulk */
        0x40, 0x00, /* u16 ep_wMaxPacketSize; */
        0x00,       /* u8  ep_bInterval; */

};

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

static int ccid_handle_control(USBDevice *dev, USBPacket *p, int request,
                               int value, int index, int length, uint8_t *data)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);
    int ret = 0;

    DPRINTF(s, 1, "got control %x, value %x\n", request, value);
    switch (request) {
    case DeviceRequest | USB_REQ_GET_STATUS:
        data[0] = (1 << USB_DEVICE_SELF_POWERED) |
            (dev->remote_wakeup << USB_DEVICE_REMOTE_WAKEUP);
        data[1] = 0x00;
        ret = 2;
        break;
    case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 0;
        } else {
            goto fail;
        }
        ret = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_FEATURE:
        if (value == USB_DEVICE_REMOTE_WAKEUP) {
            dev->remote_wakeup = 1;
        } else {
            goto fail;
        }
        ret = 0;
        break;
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
        dev->addr = value;
        ret = 0;
        break;
    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
        switch (value >> 8) {
        case USB_DT_DEVICE:
            memcpy(data, qemu_ccid_dev_descriptor,
                   sizeof(qemu_ccid_dev_descriptor));
            ret = sizeof(qemu_ccid_dev_descriptor);
            break;
        case USB_DT_CONFIG:
            memcpy(data, qemu_ccid_config_descriptor,
                   sizeof(qemu_ccid_config_descriptor));
            ret = sizeof(qemu_ccid_config_descriptor);
            break;
        case USB_DT_STRING:
            switch (value & 0xff) {
            case 0:
                /* language ids */
                data[0] = 4;
                data[1] = 3;
                data[2] = 0x09;
                data[3] = 0x04;
                ret = 4;
                break;
            case 1:
                /* vendor description */
                ret = set_usb_string(data, CCID_VENDOR_DESCRIPTION);
                break;
            case 2:
                /* product description */
                ret = set_usb_string(data, CCID_PRODUCT_DESCRIPTION);
                break;
            case 3:
                /* serial number */
                ret = set_usb_string(data, CCID_SERIAL_NUMBER_STRING);
                break;
            case 4:
                /* interface name */
                ret = set_usb_string(data, CCID_INTERFACE_NAME);
                break;
            default:
                goto fail;
            }
            break;
        default:
            goto fail;
        }
        break;
    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
        data[0] = 1;
        ret = 1;
        break;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
        /* Only one configuration - we just ignore the request */
        ret = 0;
        break;
    case DeviceRequest | USB_REQ_GET_INTERFACE:
        data[0] = 0;
        ret = 1;
        break;
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
        ret = 0;
        break;
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
        ret = 0;
        break;

        /* Class specific requests.  */
    case InterfaceOutClass | CCID_CONTROL_ABORT:
        DPRINTF(s, 1, "ccid_control abort UNIMPLEMENTED\n");
        ret = USB_RET_STALL;
        break;
    case InterfaceInClass | CCID_CONTROL_GET_CLOCK_FREQUENCIES:
        DPRINTF(s, 1, "ccid_control get clock frequencies UNIMPLEMENTED\n");
        ret = USB_RET_STALL;
        break;
    case InterfaceInClass | CCID_CONTROL_GET_DATA_RATES:
        DPRINTF(s, 1, "ccid_control get data rates UNIMPLEMENTED\n");
        ret = USB_RET_STALL;
        break;
    default:
fail:
        DPRINTF(s, 1, "got unsupported/bogus control %x, value %x\n",
                request, value);
        ret = USB_RET_STALL;
        break;
    }
    return ret;
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
    DPRINTF(s, D_VERBOSE, "status = %d\n", ret);
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
    memcpy(h->abProtocolDataStructure, s->abProtocolDataStructure, len);
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
        DPRINTF(s, D_VERBOSE, "error %d", p->b.bError);
    }
    memcpy(p->abData, data, len);
    ccid_reset_error_status(s);
}

static void ccid_write_data_block_answer(USBCCIDState *s,
    const uint8_t *data, uint32_t len)
{
    uint8_t seq;
    uint8_t slot;

    if (!ccid_has_pending_answers(s)) {
        abort();
    }
    ccid_remove_pending_answer(s, &slot, &seq);
    ccid_write_data_block(s, slot, seq, data, len);
}

static void ccid_write_data_block_atr(USBCCIDState *s, CCID_Header *recv)
{
    const uint8_t *atr = NULL;
    uint32_t len = 0;

    if (s->card) {
        atr = s->cardinfo->get_atr(s->card, &len);
    }
    ccid_write_data_block(s, recv->bSlot, recv->bSeq, atr, len);
}

static void ccid_set_parameters(USBCCIDState *s, CCID_Header *recv)
{
    CCID_SetParameters *ph = (CCID_SetParameters *) recv;
    uint32_t len = 0;
    if ((ph->bProtocolNum & 3) == 0) {
        len = 5;
    }
    if ((ph->bProtocolNum & 3) == 1) {
        len = 7;
    }
    if (len == 0) {
        s->bmCommandStatus = COMMAND_STATUS_FAILED;
        s->bError = 7; /* Protocol invalid or not supported */
        return;
    }
    s->bProtocolNum = ph->bProtocolNum;
    memcpy(s->abProtocolDataStructure, ph->abProtocolDataStructure, len);
    s->ulProtocolDataStructureSize = len;
    DPRINTF(s, 1, "%s: using len %d\n", __func__, len);
}

/*
 * must be 5 bytes for T=0, 7 bytes for T=1
 * See page 52
 */
static const uint8_t abDefaultProtocolDataStructure[7] = {
    0x77, 0x00, 0x00, 0x00, 0x00, 0xfe /*IFSC*/, 0x00 /*NAD*/ };

static void ccid_reset_parameters(USBCCIDState *s)
{
   uint32_t len = sizeof(abDefaultProtocolDataStructure);

   s->bProtocolNum = 1; /* T=1 */
   s->ulProtocolDataStructureSize = len;
   memcpy(s->abProtocolDataStructure, abDefaultProtocolDataStructure, len);
}

static void ccid_report_error_failed(USBCCIDState *s, uint8_t error)
{
    s->bmCommandStatus = COMMAND_STATUS_FAILED;
    s->bError = error;
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
        s->cardinfo->apdu_from_guest(s->card, recv->abData, len);
    } else {
        DPRINTF(s, D_WARN, "warning: discarded apdu\n");
    }
}

/*
 * Handle a single USB_TOKEN_OUT, return value returned to guest.
 * Return value:
 *  0             - all ok
 *  USB_RET_STALL - failed to handle packet
 */
static int ccid_handle_bulk_out(USBCCIDState *s, USBPacket *p)
{
    CCID_Header *ccid_header;

    if (p->len + s->bulk_out_pos > BULK_OUT_DATA_SIZE) {
        return USB_RET_STALL;
    }
    ccid_header = (CCID_Header *)s->bulk_out_data;
    memcpy(s->bulk_out_data + s->bulk_out_pos, p->data, p->len);
    s->bulk_out_pos += p->len;
    if (p->len == CCID_MAX_PACKET_SIZE) {
        DPRINTF(s, D_VERBOSE,
            "usb-ccid: bulk_in: expecting more packets (%d/%d)\n",
            p->len, ccid_header->dwLength);
        return 0;
    }
    if (s->bulk_out_pos < 10) {
        DPRINTF(s, 1,
                "%s: bad USB_TOKEN_OUT length, should be at least 10 bytes\n",
                __func__);
    } else {
        DPRINTF(s, D_MORE_INFO, "%s %x\n", __func__, ccid_header->bMessageType);
        switch (ccid_header->bMessageType) {
        case CCID_MESSAGE_TYPE_PC_to_RDR_GetSlotStatus:
            ccid_write_slot_status(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOn:
            DPRINTF(s, 1, "PowerOn: %d\n",
                ((CCID_IccPowerOn *)(ccid_header))->bPowerSelect);
            s->powered = true;
            if (!ccid_card_inserted(s)) {
                ccid_report_error_failed(s, ERROR_ICC_MUTE);
            }
            /* atr is written regardless of error. */
            ccid_write_data_block_atr(s, ccid_header);
            break;
        case CCID_MESSAGE_TYPE_PC_to_RDR_IccPowerOff:
            DPRINTF(s, 1, "PowerOff\n");
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
    return 0;
}

static int ccid_bulk_in_copy_to_guest(USBCCIDState *s, uint8_t *data, int len)
{
    int ret = 0;

    assert(len > 0);
    ccid_bulk_in_get(s);
    if (s->current_bulk_in != NULL) {
        ret = MIN(s->current_bulk_in->len - s->current_bulk_in->pos, len);
        memcpy(data, s->current_bulk_in->data + s->current_bulk_in->pos, ret);
        s->current_bulk_in->pos += ret;
        if (s->current_bulk_in->pos == s->current_bulk_in->len) {
            ccid_bulk_in_release(s);
        }
    } else {
        /* return when device has no data - usb 2.0 spec Table 8-4 */
        ret = USB_RET_NAK;
    }
    if (ret > 0) {
        DPRINTF(s, D_MORE_INFO,
                "%s: %d/%d req/act to guest (BULK_IN)\n", __func__, len, ret);
    }
    if (ret != USB_RET_NAK && ret < len) {
        DPRINTF(s, 1,
            "%s: returning short (EREMOTEIO) %d < %d\n", __func__, ret, len);
    }
    return ret;
}

static int ccid_handle_data(USBDevice *dev, USBPacket *p)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);
    int ret = 0;
    uint8_t *data = p->data;
    int len = p->len;

    switch (p->pid) {
    case USB_TOKEN_OUT:
        ret = ccid_handle_bulk_out(s, p);
        break;

    case USB_TOKEN_IN:
        switch (p->devep & 0xf) {
        case CCID_BULK_IN_EP:
            if (!len) {
                ret = USB_RET_NAK;
            } else {
                ret = ccid_bulk_in_copy_to_guest(s, data, len);
            }
            break;
        case CCID_INT_IN_EP:
            if (s->notify_slot_change) {
                /* page 56, RDR_to_PC_NotifySlotChange */
                data[0] = CCID_MESSAGE_TYPE_RDR_to_PC_NotifySlotChange;
                data[1] = s->bmSlotICCState;
                ret = 2;
                s->notify_slot_change = false;
                s->bmSlotICCState &= ~SLOT_0_CHANGED_MASK;
                DPRINTF(s, D_INFO,
                        "handle_data: int_in: notify_slot_change %X, "
                        "requested len %d\n",
                        s->bmSlotICCState, len);
            }
            break;
        default:
            DPRINTF(s, 1, "Bad endpoint\n");
            break;
        }
        break;
    default:
        DPRINTF(s, 1, "Bad token\n");
        ret = USB_RET_STALL;
        break;
    }

    return ret;
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

static struct BusInfo ccid_bus_info = {
    .name = "ccid-bus",
    .size = sizeof(CCIDBus),
    .props = (Property[]) {
        DEFINE_PROP_UINT32("slot", struct CCIDCardState, slot, 0),
        DEFINE_PROP_END_OF_LIST(),
    }
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
        abort();
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
    CCIDCardState *card = DO_UPCAST(CCIDCardState, qdev, qdev);
    CCIDCardInfo *info = DO_UPCAST(CCIDCardInfo, qdev, qdev->info);
    USBCCIDState *s =
        DO_UPCAST(USBCCIDState, dev.qdev, card->qdev.parent_bus->parent);

    if (ccid_card_inserted(s)) {
        ccid_card_card_removed(card);
    }
    if (info->exitfn) {
        ret = info->exitfn(card);
    }
    s->card = NULL;
    s->cardinfo = NULL;
    return ret;
}

static int ccid_card_init(DeviceState *qdev, DeviceInfo *base)
{
    CCIDCardState *card = DO_UPCAST(CCIDCardState, qdev, qdev);
    CCIDCardInfo *info = DO_UPCAST(CCIDCardInfo, qdev, base);
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
    ret = info->initfn ? info->initfn(card) : ret;
    if (ret == 0) {
        s->card = card;
        s->cardinfo = info;
    }
    return ret;
}

void ccid_card_qdev_register(CCIDCardInfo *card)
{
    card->qdev.bus_info = &ccid_bus_info;
    card->qdev.init = ccid_card_init;
    card->qdev.exit = ccid_card_exit;
    qdev_register(&card->qdev);
}

static int ccid_initfn(USBDevice *dev)
{
    USBCCIDState *s = DO_UPCAST(USBCCIDState, dev, dev);

    qbus_create_inplace(&s->bus.qbus, &ccid_bus_info, &dev->qdev, NULL);
    s->bus.qbus.allow_hotplug = 1;
    s->card = NULL;
    s->cardinfo = NULL;
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
    .name = CCID_DEV_NAME,
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
        VMSTATE_BUFFER(abProtocolDataStructure, USBCCIDState),
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

static struct USBDeviceInfo ccid_info = {
    .product_desc   = "QEMU USB CCID",
    .qdev.name      = CCID_DEV_NAME,
    .qdev.desc      = "CCID Rev 1.1 smartcard reader",
    .qdev.size      = sizeof(USBCCIDState),
    .init           = ccid_initfn,
    .handle_packet  = usb_generic_handle_packet,
    .handle_reset   = ccid_handle_reset,
    .handle_control = ccid_handle_control,
    .handle_data    = ccid_handle_data,
    .handle_destroy = ccid_handle_destroy,
    .usbdevice_name = "ccid",
    .qdev.props     = (Property[]) {
        DEFINE_PROP_UINT8("debug", USBCCIDState, debug, 0),
        DEFINE_PROP_END_OF_LIST(),
    },
    .qdev.vmsd      = &ccid_vmstate,
};

static void ccid_register_devices(void)
{
    usb_qdev_register(&ccid_info);
}
device_init(ccid_register_devices)
