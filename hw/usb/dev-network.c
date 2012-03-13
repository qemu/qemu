/*
 * QEMU USB Net devices
 *
 * Copyright (c) 2006 Thomas Sailer
 * Copyright (c) 2008 Andrzej Zaborowski
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu-common.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "net.h"
#include "qemu-queue.h"
#include "sysemu.h"
#include "iov.h"

/*#define TRAFFIC_DEBUG*/
/* Thanks to NetChip Technologies for donating this product ID.
 * It's for devices with only CDC Ethernet configurations.
 */
#define CDC_VENDOR_NUM          0x0525  /* NetChip */
#define CDC_PRODUCT_NUM         0xa4a1  /* Linux-USB Ethernet Gadget */
/* For hardware that can talk RNDIS and either of the above protocols,
 * use this ID ... the windows INF files will know it.
 */
#define RNDIS_VENDOR_NUM        0x0525  /* NetChip */
#define RNDIS_PRODUCT_NUM       0xa4a2  /* Ethernet/RNDIS Gadget */

enum usbstring_idx {
    STRING_MANUFACTURER		= 1,
    STRING_PRODUCT,
    STRING_ETHADDR,
    STRING_DATA,
    STRING_CONTROL,
    STRING_RNDIS_CONTROL,
    STRING_CDC,
    STRING_SUBSET,
    STRING_RNDIS,
    STRING_SERIALNUMBER,
};

#define DEV_CONFIG_VALUE		1	/* CDC or a subset */
#define DEV_RNDIS_CONFIG_VALUE		2	/* RNDIS; optional */

#define USB_CDC_SUBCLASS_ACM		0x02
#define USB_CDC_SUBCLASS_ETHERNET	0x06

#define USB_CDC_PROTO_NONE		0
#define USB_CDC_ACM_PROTO_VENDOR	0xff

#define USB_CDC_HEADER_TYPE		0x00	/* header_desc */
#define USB_CDC_CALL_MANAGEMENT_TYPE	0x01	/* call_mgmt_descriptor */
#define USB_CDC_ACM_TYPE		0x02	/* acm_descriptor */
#define USB_CDC_UNION_TYPE		0x06	/* union_desc */
#define USB_CDC_ETHERNET_TYPE		0x0f	/* ether_desc */

#define USB_CDC_SEND_ENCAPSULATED_COMMAND	0x00
#define USB_CDC_GET_ENCAPSULATED_RESPONSE	0x01
#define USB_CDC_REQ_SET_LINE_CODING		0x20
#define USB_CDC_REQ_GET_LINE_CODING		0x21
#define USB_CDC_REQ_SET_CONTROL_LINE_STATE	0x22
#define USB_CDC_REQ_SEND_BREAK			0x23
#define USB_CDC_SET_ETHERNET_MULTICAST_FILTERS	0x40
#define USB_CDC_SET_ETHERNET_PM_PATTERN_FILTER	0x41
#define USB_CDC_GET_ETHERNET_PM_PATTERN_FILTER	0x42
#define USB_CDC_SET_ETHERNET_PACKET_FILTER	0x43
#define USB_CDC_GET_ETHERNET_STATISTIC		0x44

#define LOG2_STATUS_INTERVAL_MSEC	5    /* 1 << 5 == 32 msec */
#define STATUS_BYTECOUNT		16   /* 8 byte header + data */

#define ETH_FRAME_LEN			1514 /* Max. octets in frame sans FCS */

static const USBDescStrings usb_net_stringtable = {
    [STRING_MANUFACTURER]       = "QEMU",
    [STRING_PRODUCT]            = "RNDIS/QEMU USB Network Device",
    [STRING_ETHADDR]            = "400102030405",
    [STRING_DATA]               = "QEMU USB Net Data Interface",
    [STRING_CONTROL]            = "QEMU USB Net Control Interface",
    [STRING_RNDIS_CONTROL]      = "QEMU USB Net RNDIS Control Interface",
    [STRING_CDC]                = "QEMU USB Net CDC",
    [STRING_SUBSET]             = "QEMU USB Net Subset",
    [STRING_RNDIS]              = "QEMU USB Net RNDIS",
    [STRING_SERIALNUMBER]       = "1",
};

static const USBDescIface desc_iface_rndis[] = {
    {
        /* RNDIS Control Interface */
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_COMM,
        .bInterfaceSubClass            = USB_CDC_SUBCLASS_ACM,
        .bInterfaceProtocol            = USB_CDC_ACM_PROTO_VENDOR,
        .iInterface                    = STRING_RNDIS_CONTROL,
        .ndesc                         = 4,
        .descs = (USBDescOther[]) {
            {
                /* Header Descriptor */
                .data = (uint8_t[]) {
                    0x05,                       /*  u8    bLength */
                    USB_DT_CS_INTERFACE,        /*  u8    bDescriptorType */
                    USB_CDC_HEADER_TYPE,        /*  u8    bDescriptorSubType */
                    0x10, 0x01,                 /*  le16  bcdCDC */
                },
            },{
                /* Call Management Descriptor */
                .data = (uint8_t[]) {
                    0x05,                       /*  u8    bLength */
                    USB_DT_CS_INTERFACE,        /*  u8    bDescriptorType */
                    USB_CDC_CALL_MANAGEMENT_TYPE, /*  u8    bDescriptorSubType */
                    0x00,                       /*  u8    bmCapabilities */
                    0x01,                       /*  u8    bDataInterface */
                },
            },{
                /* ACM Descriptor */
                .data = (uint8_t[]) {
                    0x04,                       /*  u8    bLength */
                    USB_DT_CS_INTERFACE,        /*  u8    bDescriptorType */
                    USB_CDC_ACM_TYPE,           /*  u8    bDescriptorSubType */
                    0x00,                       /*  u8    bmCapabilities */
                },
            },{
                /* Union Descriptor */
                .data = (uint8_t[]) {
                    0x05,                       /*  u8    bLength */
                    USB_DT_CS_INTERFACE,        /*  u8    bDescriptorType */
                    USB_CDC_UNION_TYPE,         /*  u8    bDescriptorSubType */
                    0x00,                       /*  u8    bMasterInterface0 */
                    0x01,                       /*  u8    bSlaveInterface0 */
                },
            },
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | 0x01,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = STATUS_BYTECOUNT,
                .bInterval             = 1 << LOG2_STATUS_INTERVAL_MSEC,
            },
        }
    },{
        /* RNDIS Data Interface */
        .bInterfaceNumber              = 1,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = USB_CLASS_CDC_DATA,
        .iInterface                    = STRING_DATA,
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | 0x02,
                .bmAttributes          = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize        = 0x40,
            },{
                .bEndpointAddress      = USB_DIR_OUT | 0x02,
                .bmAttributes          = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize        = 0x40,
            }
        }
    }
};

static const USBDescIface desc_iface_cdc[] = {
    {
        /* CDC Control Interface */
        .bInterfaceNumber              = 0,
        .bNumEndpoints                 = 1,
        .bInterfaceClass               = USB_CLASS_COMM,
        .bInterfaceSubClass            = USB_CDC_SUBCLASS_ETHERNET,
        .bInterfaceProtocol            = USB_CDC_PROTO_NONE,
        .iInterface                    = STRING_CONTROL,
        .ndesc                         = 3,
        .descs = (USBDescOther[]) {
            {
                /* Header Descriptor */
                .data = (uint8_t[]) {
                    0x05,                       /*  u8    bLength */
                    USB_DT_CS_INTERFACE,        /*  u8    bDescriptorType */
                    USB_CDC_HEADER_TYPE,        /*  u8    bDescriptorSubType */
                    0x10, 0x01,                 /*  le16  bcdCDC */
                },
            },{
                /* Union Descriptor */
                .data = (uint8_t[]) {
                    0x05,                       /*  u8    bLength */
                    USB_DT_CS_INTERFACE,        /*  u8    bDescriptorType */
                    USB_CDC_UNION_TYPE,         /*  u8    bDescriptorSubType */
                    0x00,                       /*  u8    bMasterInterface0 */
                    0x01,                       /*  u8    bSlaveInterface0 */
                },
            },{
                /* Ethernet Descriptor */
                .data = (uint8_t[]) {
                    0x0d,                       /*  u8    bLength */
                    USB_DT_CS_INTERFACE,        /*  u8    bDescriptorType */
                    USB_CDC_ETHERNET_TYPE,      /*  u8    bDescriptorSubType */
                    STRING_ETHADDR,             /*  u8    iMACAddress */
                    0x00, 0x00, 0x00, 0x00,     /*  le32  bmEthernetStatistics */
                    ETH_FRAME_LEN & 0xff,
                    ETH_FRAME_LEN >> 8,         /*  le16  wMaxSegmentSize */
                    0x00, 0x00,                 /*  le16  wNumberMCFilters */
                    0x00,                       /*  u8    bNumberPowerFilters */
                },
            },
        },
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | 0x01,
                .bmAttributes          = USB_ENDPOINT_XFER_INT,
                .wMaxPacketSize        = STATUS_BYTECOUNT,
                .bInterval             = 1 << LOG2_STATUS_INTERVAL_MSEC,
            },
        }
    },{
        /* CDC Data Interface (off) */
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 0,
        .bNumEndpoints                 = 0,
        .bInterfaceClass               = USB_CLASS_CDC_DATA,
    },{
        /* CDC Data Interface */
        .bInterfaceNumber              = 1,
        .bAlternateSetting             = 1,
        .bNumEndpoints                 = 2,
        .bInterfaceClass               = USB_CLASS_CDC_DATA,
        .iInterface                    = STRING_DATA,
        .eps = (USBDescEndpoint[]) {
            {
                .bEndpointAddress      = USB_DIR_IN | 0x02,
                .bmAttributes          = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize        = 0x40,
            },{
                .bEndpointAddress      = USB_DIR_OUT | 0x02,
                .bmAttributes          = USB_ENDPOINT_XFER_BULK,
                .wMaxPacketSize        = 0x40,
            }
        }
    }
};

static const USBDescDevice desc_device_net = {
    .bcdUSB                        = 0x0200,
    .bDeviceClass                  = USB_CLASS_COMM,
    .bMaxPacketSize0               = 0x40,
    .bNumConfigurations            = 2,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 2,
            .bConfigurationValue   = DEV_RNDIS_CONFIG_VALUE,
            .iConfiguration        = STRING_RNDIS,
            .bmAttributes          = 0xc0,
            .bMaxPower             = 0x32,
            .nif = ARRAY_SIZE(desc_iface_rndis),
            .ifs = desc_iface_rndis,
        },{
            .bNumInterfaces        = 2,
            .bConfigurationValue   = DEV_CONFIG_VALUE,
            .iConfiguration        = STRING_CDC,
            .bmAttributes          = 0xc0,
            .bMaxPower             = 0x32,
            .nif = ARRAY_SIZE(desc_iface_cdc),
            .ifs = desc_iface_cdc,
        }
    },
};

static const USBDesc desc_net = {
    .id = {
        .idVendor          = RNDIS_VENDOR_NUM,
        .idProduct         = RNDIS_PRODUCT_NUM,
        .bcdDevice         = 0,
        .iManufacturer     = STRING_MANUFACTURER,
        .iProduct          = STRING_PRODUCT,
        .iSerialNumber     = STRING_SERIALNUMBER,
    },
    .full = &desc_device_net,
    .str  = usb_net_stringtable,
};

/*
 * RNDIS Definitions - in theory not specific to USB.
 */
#define RNDIS_MAXIMUM_FRAME_SIZE	1518
#define RNDIS_MAX_TOTAL_SIZE		1558

/* Remote NDIS Versions */
#define RNDIS_MAJOR_VERSION		1
#define RNDIS_MINOR_VERSION		0

/* Status Values */
#define RNDIS_STATUS_SUCCESS		0x00000000U /* Success */
#define RNDIS_STATUS_FAILURE		0xc0000001U /* Unspecified error */
#define RNDIS_STATUS_INVALID_DATA	0xc0010015U /* Invalid data */
#define RNDIS_STATUS_NOT_SUPPORTED	0xc00000bbU /* Unsupported request */
#define RNDIS_STATUS_MEDIA_CONNECT	0x4001000bU /* Device connected */
#define RNDIS_STATUS_MEDIA_DISCONNECT	0x4001000cU /* Device disconnected */

/* Message Set for Connectionless (802.3) Devices */
enum {
    RNDIS_PACKET_MSG		= 1,
    RNDIS_INITIALIZE_MSG	= 2,	/* Initialize device */
    RNDIS_HALT_MSG		= 3,
    RNDIS_QUERY_MSG		= 4,
    RNDIS_SET_MSG		= 5,
    RNDIS_RESET_MSG		= 6,
    RNDIS_INDICATE_STATUS_MSG	= 7,
    RNDIS_KEEPALIVE_MSG		= 8,
};

/* Message completion */
enum {
    RNDIS_INITIALIZE_CMPLT	= 0x80000002U,
    RNDIS_QUERY_CMPLT		= 0x80000004U,
    RNDIS_SET_CMPLT		= 0x80000005U,
    RNDIS_RESET_CMPLT		= 0x80000006U,
    RNDIS_KEEPALIVE_CMPLT	= 0x80000008U,
};

/* Device Flags */
enum {
    RNDIS_DF_CONNECTIONLESS	= 1,
    RNDIS_DF_CONNECTIONORIENTED	= 2,
};

#define RNDIS_MEDIUM_802_3		0x00000000U

/* from drivers/net/sk98lin/h/skgepnmi.h */
#define OID_PNP_CAPABILITIES		0xfd010100
#define OID_PNP_SET_POWER		0xfd010101
#define OID_PNP_QUERY_POWER		0xfd010102
#define OID_PNP_ADD_WAKE_UP_PATTERN	0xfd010103
#define OID_PNP_REMOVE_WAKE_UP_PATTERN	0xfd010104
#define OID_PNP_ENABLE_WAKE_UP		0xfd010106

typedef uint32_t le32;

typedef struct rndis_init_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
    le32 MajorVersion;
    le32 MinorVersion;
    le32 MaxTransferSize;
} rndis_init_msg_type;

typedef struct rndis_init_cmplt_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
    le32 Status;
    le32 MajorVersion;
    le32 MinorVersion;
    le32 DeviceFlags;
    le32 Medium;
    le32 MaxPacketsPerTransfer;
    le32 MaxTransferSize;
    le32 PacketAlignmentFactor;
    le32 AFListOffset;
    le32 AFListSize;
} rndis_init_cmplt_type;

typedef struct rndis_halt_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
} rndis_halt_msg_type;

typedef struct rndis_query_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
    le32 OID;
    le32 InformationBufferLength;
    le32 InformationBufferOffset;
    le32 DeviceVcHandle;
} rndis_query_msg_type;

typedef struct rndis_query_cmplt_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
    le32 Status;
    le32 InformationBufferLength;
    le32 InformationBufferOffset;
} rndis_query_cmplt_type;

typedef struct rndis_set_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
    le32 OID;
    le32 InformationBufferLength;
    le32 InformationBufferOffset;
    le32 DeviceVcHandle;
} rndis_set_msg_type;

typedef struct rndis_set_cmplt_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
    le32 Status;
} rndis_set_cmplt_type;

typedef struct rndis_reset_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 Reserved;
} rndis_reset_msg_type;

typedef struct rndis_reset_cmplt_type {
    le32 MessageType;
    le32 MessageLength;
    le32 Status;
    le32 AddressingReset;
} rndis_reset_cmplt_type;

typedef struct rndis_indicate_status_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 Status;
    le32 StatusBufferLength;
    le32 StatusBufferOffset;
} rndis_indicate_status_msg_type;

typedef struct rndis_keepalive_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
} rndis_keepalive_msg_type;

typedef struct rndis_keepalive_cmplt_type {
    le32 MessageType;
    le32 MessageLength;
    le32 RequestID;
    le32 Status;
} rndis_keepalive_cmplt_type;

struct rndis_packet_msg_type {
    le32 MessageType;
    le32 MessageLength;
    le32 DataOffset;
    le32 DataLength;
    le32 OOBDataOffset;
    le32 OOBDataLength;
    le32 NumOOBDataElements;
    le32 PerPacketInfoOffset;
    le32 PerPacketInfoLength;
    le32 VcHandle;
    le32 Reserved;
};

struct rndis_config_parameter {
    le32 ParameterNameOffset;
    le32 ParameterNameLength;
    le32 ParameterType;
    le32 ParameterValueOffset;
    le32 ParameterValueLength;
};

/* implementation specific */
enum rndis_state
{
    RNDIS_UNINITIALIZED,
    RNDIS_INITIALIZED,
    RNDIS_DATA_INITIALIZED,
};

/* from ndis.h */
enum ndis_oid {
    /* Required Object IDs (OIDs) */
    OID_GEN_SUPPORTED_LIST		= 0x00010101,
    OID_GEN_HARDWARE_STATUS		= 0x00010102,
    OID_GEN_MEDIA_SUPPORTED		= 0x00010103,
    OID_GEN_MEDIA_IN_USE		= 0x00010104,
    OID_GEN_MAXIMUM_LOOKAHEAD		= 0x00010105,
    OID_GEN_MAXIMUM_FRAME_SIZE		= 0x00010106,
    OID_GEN_LINK_SPEED			= 0x00010107,
    OID_GEN_TRANSMIT_BUFFER_SPACE	= 0x00010108,
    OID_GEN_RECEIVE_BUFFER_SPACE	= 0x00010109,
    OID_GEN_TRANSMIT_BLOCK_SIZE		= 0x0001010a,
    OID_GEN_RECEIVE_BLOCK_SIZE		= 0x0001010b,
    OID_GEN_VENDOR_ID			= 0x0001010c,
    OID_GEN_VENDOR_DESCRIPTION		= 0x0001010d,
    OID_GEN_CURRENT_PACKET_FILTER	= 0x0001010e,
    OID_GEN_CURRENT_LOOKAHEAD		= 0x0001010f,
    OID_GEN_DRIVER_VERSION		= 0x00010110,
    OID_GEN_MAXIMUM_TOTAL_SIZE		= 0x00010111,
    OID_GEN_PROTOCOL_OPTIONS		= 0x00010112,
    OID_GEN_MAC_OPTIONS			= 0x00010113,
    OID_GEN_MEDIA_CONNECT_STATUS	= 0x00010114,
    OID_GEN_MAXIMUM_SEND_PACKETS	= 0x00010115,
    OID_GEN_VENDOR_DRIVER_VERSION	= 0x00010116,
    OID_GEN_SUPPORTED_GUIDS		= 0x00010117,
    OID_GEN_NETWORK_LAYER_ADDRESSES	= 0x00010118,
    OID_GEN_TRANSPORT_HEADER_OFFSET	= 0x00010119,
    OID_GEN_MACHINE_NAME		= 0x0001021a,
    OID_GEN_RNDIS_CONFIG_PARAMETER	= 0x0001021b,
    OID_GEN_VLAN_ID			= 0x0001021c,

    /* Optional OIDs */
    OID_GEN_MEDIA_CAPABILITIES		= 0x00010201,
    OID_GEN_PHYSICAL_MEDIUM		= 0x00010202,

    /* Required statistics OIDs */
    OID_GEN_XMIT_OK			= 0x00020101,
    OID_GEN_RCV_OK			= 0x00020102,
    OID_GEN_XMIT_ERROR			= 0x00020103,
    OID_GEN_RCV_ERROR			= 0x00020104,
    OID_GEN_RCV_NO_BUFFER		= 0x00020105,

    /* Optional statistics OIDs */
    OID_GEN_DIRECTED_BYTES_XMIT		= 0x00020201,
    OID_GEN_DIRECTED_FRAMES_XMIT	= 0x00020202,
    OID_GEN_MULTICAST_BYTES_XMIT	= 0x00020203,
    OID_GEN_MULTICAST_FRAMES_XMIT	= 0x00020204,
    OID_GEN_BROADCAST_BYTES_XMIT	= 0x00020205,
    OID_GEN_BROADCAST_FRAMES_XMIT	= 0x00020206,
    OID_GEN_DIRECTED_BYTES_RCV		= 0x00020207,
    OID_GEN_DIRECTED_FRAMES_RCV		= 0x00020208,
    OID_GEN_MULTICAST_BYTES_RCV		= 0x00020209,
    OID_GEN_MULTICAST_FRAMES_RCV	= 0x0002020a,
    OID_GEN_BROADCAST_BYTES_RCV		= 0x0002020b,
    OID_GEN_BROADCAST_FRAMES_RCV	= 0x0002020c,
    OID_GEN_RCV_CRC_ERROR		= 0x0002020d,
    OID_GEN_TRANSMIT_QUEUE_LENGTH	= 0x0002020e,
    OID_GEN_GET_TIME_CAPS		= 0x0002020f,
    OID_GEN_GET_NETCARD_TIME		= 0x00020210,
    OID_GEN_NETCARD_LOAD		= 0x00020211,
    OID_GEN_DEVICE_PROFILE		= 0x00020212,
    OID_GEN_INIT_TIME_MS		= 0x00020213,
    OID_GEN_RESET_COUNTS		= 0x00020214,
    OID_GEN_MEDIA_SENSE_COUNTS		= 0x00020215,
    OID_GEN_FRIENDLY_NAME		= 0x00020216,
    OID_GEN_MINIPORT_INFO		= 0x00020217,
    OID_GEN_RESET_VERIFY_PARAMETERS	= 0x00020218,

    /* IEEE 802.3 (Ethernet) OIDs */
    OID_802_3_PERMANENT_ADDRESS		= 0x01010101,
    OID_802_3_CURRENT_ADDRESS		= 0x01010102,
    OID_802_3_MULTICAST_LIST		= 0x01010103,
    OID_802_3_MAXIMUM_LIST_SIZE		= 0x01010104,
    OID_802_3_MAC_OPTIONS		= 0x01010105,
    OID_802_3_RCV_ERROR_ALIGNMENT	= 0x01020101,
    OID_802_3_XMIT_ONE_COLLISION	= 0x01020102,
    OID_802_3_XMIT_MORE_COLLISIONS	= 0x01020103,
    OID_802_3_XMIT_DEFERRED		= 0x01020201,
    OID_802_3_XMIT_MAX_COLLISIONS	= 0x01020202,
    OID_802_3_RCV_OVERRUN		= 0x01020203,
    OID_802_3_XMIT_UNDERRUN		= 0x01020204,
    OID_802_3_XMIT_HEARTBEAT_FAILURE	= 0x01020205,
    OID_802_3_XMIT_TIMES_CRS_LOST	= 0x01020206,
    OID_802_3_XMIT_LATE_COLLISIONS	= 0x01020207,
};

static const uint32_t oid_supported_list[] =
{
    /* the general stuff */
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_PHYSICAL_MEDIUM,

    /* the statistical stuff */
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,

    /* IEEE 802.3 */
    /* the general stuff */
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAC_OPTIONS,
    OID_802_3_MAXIMUM_LIST_SIZE,

    /* the statistical stuff */
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
};

#define NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA	(1 << 0)
#define NDIS_MAC_OPTION_RECEIVE_SERIALIZED	(1 << 1)
#define NDIS_MAC_OPTION_TRANSFERS_NOT_PEND	(1 << 2)
#define NDIS_MAC_OPTION_NO_LOOPBACK		(1 << 3)
#define NDIS_MAC_OPTION_FULL_DUPLEX		(1 << 4)
#define NDIS_MAC_OPTION_EOTX_INDICATION		(1 << 5)
#define NDIS_MAC_OPTION_8021P_PRIORITY		(1 << 6)

struct rndis_response {
    QTAILQ_ENTRY(rndis_response) entries;
    uint32_t length;
    uint8_t buf[0];
};

typedef struct USBNetState {
    USBDevice dev;

    enum rndis_state rndis_state;
    uint32_t medium;
    uint32_t speed;
    uint32_t media_state;
    uint16_t filter;
    uint32_t vendorid;

    unsigned int out_ptr;
    uint8_t out_buf[2048];

    USBPacket *inpkt;
    unsigned int in_ptr, in_len;
    uint8_t in_buf[2048];

    char usbstring_mac[13];
    NICState *nic;
    NICConf conf;
    QTAILQ_HEAD(rndis_resp_head, rndis_response) rndis_resp;
} USBNetState;

static int is_rndis(USBNetState *s)
{
    return s->dev.config->bConfigurationValue == DEV_RNDIS_CONFIG_VALUE;
}

static int ndis_query(USBNetState *s, uint32_t oid,
                      uint8_t *inbuf, unsigned int inlen, uint8_t *outbuf,
                      size_t outlen)
{
    unsigned int i;

    switch (oid) {
    /* general oids (table 4-1) */
    /* mandatory */
    case OID_GEN_SUPPORTED_LIST:
        for (i = 0; i < ARRAY_SIZE(oid_supported_list); i++)
            ((le32 *) outbuf)[i] = cpu_to_le32(oid_supported_list[i]);
        return sizeof(oid_supported_list);

    /* mandatory */
    case OID_GEN_HARDWARE_STATUS:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_MEDIA_SUPPORTED:
        *((le32 *) outbuf) = cpu_to_le32(s->medium);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_MEDIA_IN_USE:
        *((le32 *) outbuf) = cpu_to_le32(s->medium);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        *((le32 *) outbuf) = cpu_to_le32(ETH_FRAME_LEN);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_LINK_SPEED:
        *((le32 *) outbuf) = cpu_to_le32(s->speed);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
        *((le32 *) outbuf) = cpu_to_le32(ETH_FRAME_LEN);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_RECEIVE_BLOCK_SIZE:
        *((le32 *) outbuf) = cpu_to_le32(ETH_FRAME_LEN);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_VENDOR_ID:
        *((le32 *) outbuf) = cpu_to_le32(s->vendorid);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_VENDOR_DESCRIPTION:
        pstrcpy((char *)outbuf, outlen, "QEMU USB RNDIS Net");
        return strlen((char *)outbuf) + 1;

    case OID_GEN_VENDOR_DRIVER_VERSION:
        *((le32 *) outbuf) = cpu_to_le32(1);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_CURRENT_PACKET_FILTER:
        *((le32 *) outbuf) = cpu_to_le32(s->filter);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        *((le32 *) outbuf) = cpu_to_le32(RNDIS_MAX_TOTAL_SIZE);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_MEDIA_CONNECT_STATUS:
        *((le32 *) outbuf) = cpu_to_le32(s->media_state);
        return sizeof(le32);

    case OID_GEN_PHYSICAL_MEDIUM:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    case OID_GEN_MAC_OPTIONS:
        *((le32 *) outbuf) = cpu_to_le32(
                        NDIS_MAC_OPTION_RECEIVE_SERIALIZED |
                        NDIS_MAC_OPTION_FULL_DUPLEX);
        return sizeof(le32);

    /* statistics OIDs (table 4-2) */
    /* mandatory */
    case OID_GEN_XMIT_OK:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_RCV_OK:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_XMIT_ERROR:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_RCV_ERROR:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* mandatory */
    case OID_GEN_RCV_NO_BUFFER:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* ieee802.3 OIDs (table 4-3) */
    /* mandatory */
    case OID_802_3_PERMANENT_ADDRESS:
        memcpy(outbuf, s->conf.macaddr.a, 6);
        return 6;

    /* mandatory */
    case OID_802_3_CURRENT_ADDRESS:
        memcpy(outbuf, s->conf.macaddr.a, 6);
        return 6;

    /* mandatory */
    case OID_802_3_MULTICAST_LIST:
        *((le32 *) outbuf) = cpu_to_le32(0xe0000000);
        return sizeof(le32);

    /* mandatory */
    case OID_802_3_MAXIMUM_LIST_SIZE:
        *((le32 *) outbuf) = cpu_to_le32(1);
        return sizeof(le32);

    case OID_802_3_MAC_OPTIONS:
        return 0;

    /* ieee802.3 statistics OIDs (table 4-4) */
    /* mandatory */
    case OID_802_3_RCV_ERROR_ALIGNMENT:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* mandatory */
    case OID_802_3_XMIT_ONE_COLLISION:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    /* mandatory */
    case OID_802_3_XMIT_MORE_COLLISIONS:
        *((le32 *) outbuf) = cpu_to_le32(0);
        return sizeof(le32);

    default:
        fprintf(stderr, "usbnet: unknown OID 0x%08x\n", oid);
        return 0;
    }
    return -1;
}

static int ndis_set(USBNetState *s, uint32_t oid,
                uint8_t *inbuf, unsigned int inlen)
{
    switch (oid) {
    case OID_GEN_CURRENT_PACKET_FILTER:
        s->filter = le32_to_cpup((le32 *) inbuf);
        if (s->filter) {
            s->rndis_state = RNDIS_DATA_INITIALIZED;
        } else {
            s->rndis_state = RNDIS_INITIALIZED;
        }
        return 0;

    case OID_802_3_MULTICAST_LIST:
        return 0;
    }
    return -1;
}

static int rndis_get_response(USBNetState *s, uint8_t *buf)
{
    int ret = 0;
    struct rndis_response *r = s->rndis_resp.tqh_first;

    if (!r)
        return ret;

    QTAILQ_REMOVE(&s->rndis_resp, r, entries);
    ret = r->length;
    memcpy(buf, r->buf, r->length);
    g_free(r);

    return ret;
}

static void *rndis_queue_response(USBNetState *s, unsigned int length)
{
    struct rndis_response *r =
            g_malloc0(sizeof(struct rndis_response) + length);

    QTAILQ_INSERT_TAIL(&s->rndis_resp, r, entries);
    r->length = length;

    return &r->buf[0];
}

static void rndis_clear_responsequeue(USBNetState *s)
{
    struct rndis_response *r;

    while ((r = s->rndis_resp.tqh_first)) {
        QTAILQ_REMOVE(&s->rndis_resp, r, entries);
        g_free(r);
    }
}

static int rndis_init_response(USBNetState *s, rndis_init_msg_type *buf)
{
    rndis_init_cmplt_type *resp =
            rndis_queue_response(s, sizeof(rndis_init_cmplt_type));

    if (!resp)
        return USB_RET_STALL;

    resp->MessageType = cpu_to_le32(RNDIS_INITIALIZE_CMPLT);
    resp->MessageLength = cpu_to_le32(sizeof(rndis_init_cmplt_type));
    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
    resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);
    resp->MajorVersion = cpu_to_le32(RNDIS_MAJOR_VERSION);
    resp->MinorVersion = cpu_to_le32(RNDIS_MINOR_VERSION);
    resp->DeviceFlags = cpu_to_le32(RNDIS_DF_CONNECTIONLESS);
    resp->Medium = cpu_to_le32(RNDIS_MEDIUM_802_3);
    resp->MaxPacketsPerTransfer = cpu_to_le32(1);
    resp->MaxTransferSize = cpu_to_le32(ETH_FRAME_LEN +
                    sizeof(struct rndis_packet_msg_type) + 22);
    resp->PacketAlignmentFactor = cpu_to_le32(0);
    resp->AFListOffset = cpu_to_le32(0);
    resp->AFListSize = cpu_to_le32(0);
    return 0;
}

static int rndis_query_response(USBNetState *s,
                rndis_query_msg_type *buf, unsigned int length)
{
    rndis_query_cmplt_type *resp;
    /* oid_supported_list is the largest data reply */
    uint8_t infobuf[sizeof(oid_supported_list)];
    uint32_t bufoffs, buflen;
    int infobuflen;
    unsigned int resplen;

    bufoffs = le32_to_cpu(buf->InformationBufferOffset) + 8;
    buflen = le32_to_cpu(buf->InformationBufferLength);
    if (bufoffs + buflen > length)
        return USB_RET_STALL;

    infobuflen = ndis_query(s, le32_to_cpu(buf->OID),
                            bufoffs + (uint8_t *) buf, buflen, infobuf,
                            sizeof(infobuf));
    resplen = sizeof(rndis_query_cmplt_type) +
            ((infobuflen < 0) ? 0 : infobuflen);
    resp = rndis_queue_response(s, resplen);
    if (!resp)
        return USB_RET_STALL;

    resp->MessageType = cpu_to_le32(RNDIS_QUERY_CMPLT);
    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
    resp->MessageLength = cpu_to_le32(resplen);

    if (infobuflen < 0) {
        /* OID not supported */
        resp->Status = cpu_to_le32(RNDIS_STATUS_NOT_SUPPORTED);
        resp->InformationBufferLength = cpu_to_le32(0);
        resp->InformationBufferOffset = cpu_to_le32(0);
        return 0;
    }

    resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);
    resp->InformationBufferOffset =
            cpu_to_le32(infobuflen ? sizeof(rndis_query_cmplt_type) - 8 : 0);
    resp->InformationBufferLength = cpu_to_le32(infobuflen);
    memcpy(resp + 1, infobuf, infobuflen);

    return 0;
}

static int rndis_set_response(USBNetState *s,
                rndis_set_msg_type *buf, unsigned int length)
{
    rndis_set_cmplt_type *resp =
            rndis_queue_response(s, sizeof(rndis_set_cmplt_type));
    uint32_t bufoffs, buflen;
    int ret;

    if (!resp)
        return USB_RET_STALL;

    bufoffs = le32_to_cpu(buf->InformationBufferOffset) + 8;
    buflen = le32_to_cpu(buf->InformationBufferLength);
    if (bufoffs + buflen > length)
        return USB_RET_STALL;

    ret = ndis_set(s, le32_to_cpu(buf->OID),
                    bufoffs + (uint8_t *) buf, buflen);
    resp->MessageType = cpu_to_le32(RNDIS_SET_CMPLT);
    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
    resp->MessageLength = cpu_to_le32(sizeof(rndis_set_cmplt_type));
    if (ret < 0) {
        /* OID not supported */
        resp->Status = cpu_to_le32(RNDIS_STATUS_NOT_SUPPORTED);
        return 0;
    }
    resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);

    return 0;
}

static int rndis_reset_response(USBNetState *s, rndis_reset_msg_type *buf)
{
    rndis_reset_cmplt_type *resp =
            rndis_queue_response(s, sizeof(rndis_reset_cmplt_type));

    if (!resp)
        return USB_RET_STALL;

    resp->MessageType = cpu_to_le32(RNDIS_RESET_CMPLT);
    resp->MessageLength = cpu_to_le32(sizeof(rndis_reset_cmplt_type));
    resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);
    resp->AddressingReset = cpu_to_le32(1); /* reset information */

    return 0;
}

static int rndis_keepalive_response(USBNetState *s,
                rndis_keepalive_msg_type *buf)
{
    rndis_keepalive_cmplt_type *resp =
            rndis_queue_response(s, sizeof(rndis_keepalive_cmplt_type));

    if (!resp)
        return USB_RET_STALL;

    resp->MessageType = cpu_to_le32(RNDIS_KEEPALIVE_CMPLT);
    resp->MessageLength = cpu_to_le32(sizeof(rndis_keepalive_cmplt_type));
    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
    resp->Status = cpu_to_le32(RNDIS_STATUS_SUCCESS);

    return 0;
}

static int rndis_parse(USBNetState *s, uint8_t *data, int length)
{
    uint32_t msg_type;
    le32 *tmp = (le32 *) data;

    msg_type = le32_to_cpup(tmp);

    switch (msg_type) {
    case RNDIS_INITIALIZE_MSG:
        s->rndis_state = RNDIS_INITIALIZED;
        return rndis_init_response(s, (rndis_init_msg_type *) data);

    case RNDIS_HALT_MSG:
        s->rndis_state = RNDIS_UNINITIALIZED;
        return 0;

    case RNDIS_QUERY_MSG:
        return rndis_query_response(s, (rndis_query_msg_type *) data, length);

    case RNDIS_SET_MSG:
        return rndis_set_response(s, (rndis_set_msg_type *) data, length);

    case RNDIS_RESET_MSG:
        rndis_clear_responsequeue(s);
        s->out_ptr = s->in_ptr = s->in_len = 0;
        return rndis_reset_response(s, (rndis_reset_msg_type *) data);

    case RNDIS_KEEPALIVE_MSG:
        /* For USB: host does this every 5 seconds */
        return rndis_keepalive_response(s, (rndis_keepalive_msg_type *) data);
    }

    return USB_RET_STALL;
}

static void usb_net_handle_reset(USBDevice *dev)
{
}

static int usb_net_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBNetState *s = (USBNetState *) dev;
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        return ret;
    }

    ret = 0;
    switch(request) {
    case ClassInterfaceOutRequest | USB_CDC_SEND_ENCAPSULATED_COMMAND:
        if (!is_rndis(s) || value || index != 0) {
            goto fail;
        }
#ifdef TRAFFIC_DEBUG
        {
            unsigned int i;
            fprintf(stderr, "SEND_ENCAPSULATED_COMMAND:");
            for (i = 0; i < length; i++) {
                if (!(i & 15))
                    fprintf(stderr, "\n%04x:", i);
                fprintf(stderr, " %02x", data[i]);
            }
            fprintf(stderr, "\n\n");
        }
#endif
        ret = rndis_parse(s, data, length);
        break;

    case ClassInterfaceRequest | USB_CDC_GET_ENCAPSULATED_RESPONSE:
        if (!is_rndis(s) || value || index != 0) {
            goto fail;
        }
        ret = rndis_get_response(s, data);
        if (!ret) {
            data[0] = 0;
            ret = 1;
        }
#ifdef TRAFFIC_DEBUG
        {
            unsigned int i;
            fprintf(stderr, "GET_ENCAPSULATED_RESPONSE:");
            for (i = 0; i < ret; i++) {
                if (!(i & 15))
                    fprintf(stderr, "\n%04x:", i);
                fprintf(stderr, " %02x", data[i]);
            }
            fprintf(stderr, "\n\n");
        }
#endif
        break;

    default:
    fail:
        fprintf(stderr, "usbnet: failed control transaction: "
                        "request 0x%x value 0x%x index 0x%x length 0x%x\n",
                        request, value, index, length);
        ret = USB_RET_STALL;
        break;
    }
    return ret;
}

static int usb_net_handle_statusin(USBNetState *s, USBPacket *p)
{
    le32 buf[2];
    int ret = 8;

    if (p->iov.size < 8) {
        return USB_RET_STALL;
    }

    buf[0] = cpu_to_le32(1);
    buf[1] = cpu_to_le32(0);
    usb_packet_copy(p, buf, 8);
    if (!s->rndis_resp.tqh_first)
        ret = USB_RET_NAK;

#ifdef TRAFFIC_DEBUG
    fprintf(stderr, "usbnet: interrupt poll len %zu return %d",
            p->iov.size, ret);
    iov_hexdump(p->iov.iov, p->iov.niov, stderr, "usbnet", ret);
#endif

    return ret;
}

static int usb_net_handle_datain(USBNetState *s, USBPacket *p)
{
    int ret = USB_RET_NAK;

    if (s->in_ptr > s->in_len) {
        s->in_ptr = s->in_len = 0;
        ret = USB_RET_NAK;
        return ret;
    }
    if (!s->in_len) {
        ret = USB_RET_NAK;
        return ret;
    }
    ret = s->in_len - s->in_ptr;
    if (ret > p->iov.size) {
        ret = p->iov.size;
    }
    usb_packet_copy(p, &s->in_buf[s->in_ptr], ret);
    s->in_ptr += ret;
    if (s->in_ptr >= s->in_len &&
                    (is_rndis(s) || (s->in_len & (64 - 1)) || !ret)) {
        /* no short packet necessary */
        s->in_ptr = s->in_len = 0;
    }

#ifdef TRAFFIC_DEBUG
    fprintf(stderr, "usbnet: data in len %zu return %d", p->iov.size, ret);
    iov_hexdump(p->iov.iov, p->iov.niov, stderr, "usbnet", ret);
#endif

    return ret;
}

static int usb_net_handle_dataout(USBNetState *s, USBPacket *p)
{
    int ret = p->iov.size;
    int sz = sizeof(s->out_buf) - s->out_ptr;
    struct rndis_packet_msg_type *msg =
            (struct rndis_packet_msg_type *) s->out_buf;
    uint32_t len;

#ifdef TRAFFIC_DEBUG
    fprintf(stderr, "usbnet: data out len %zu\n", p->iov.size);
    iov_hexdump(p->iov.iov, p->iov.niov, stderr, "usbnet", p->iov.size);
#endif

    if (sz > ret)
        sz = ret;
    usb_packet_copy(p, &s->out_buf[s->out_ptr], sz);
    s->out_ptr += sz;

    if (!is_rndis(s)) {
        if (ret < 64) {
            qemu_send_packet(&s->nic->nc, s->out_buf, s->out_ptr);
            s->out_ptr = 0;
        }
        return ret;
    }
    len = le32_to_cpu(msg->MessageLength);
    if (s->out_ptr < 8 || s->out_ptr < len)
        return ret;
    if (le32_to_cpu(msg->MessageType) == RNDIS_PACKET_MSG) {
        uint32_t offs = 8 + le32_to_cpu(msg->DataOffset);
        uint32_t size = le32_to_cpu(msg->DataLength);
        if (offs + size <= len)
            qemu_send_packet(&s->nic->nc, s->out_buf + offs, size);
    }
    s->out_ptr -= len;
    memmove(s->out_buf, &s->out_buf[len], s->out_ptr);

    return ret;
}

static int usb_net_handle_data(USBDevice *dev, USBPacket *p)
{
    USBNetState *s = (USBNetState *) dev;
    int ret = 0;

    switch(p->pid) {
    case USB_TOKEN_IN:
        switch (p->ep->nr) {
        case 1:
            ret = usb_net_handle_statusin(s, p);
            break;

        case 2:
            ret = usb_net_handle_datain(s, p);
            break;

        default:
            goto fail;
        }
        break;

    case USB_TOKEN_OUT:
        switch (p->ep->nr) {
        case 2:
            ret = usb_net_handle_dataout(s, p);
            break;

        default:
            goto fail;
        }
        break;

    default:
    fail:
        ret = USB_RET_STALL;
        break;
    }
    if (ret == USB_RET_STALL)
        fprintf(stderr, "usbnet: failed data transaction: "
                        "pid 0x%x ep 0x%x len 0x%zx\n",
                        p->pid, p->ep->nr, p->iov.size);
    return ret;
}

static ssize_t usbnet_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    USBNetState *s = DO_UPCAST(NICState, nc, nc)->opaque;
    struct rndis_packet_msg_type *msg;

    if (is_rndis(s)) {
        msg = (struct rndis_packet_msg_type *) s->in_buf;
        if (s->rndis_state != RNDIS_DATA_INITIALIZED) {
            return -1;
        }
        if (size + sizeof(struct rndis_packet_msg_type) > sizeof(s->in_buf))
            return -1;

        memset(msg, 0, sizeof(struct rndis_packet_msg_type));
        msg->MessageType = cpu_to_le32(RNDIS_PACKET_MSG);
        msg->MessageLength = cpu_to_le32(size + sizeof(struct rndis_packet_msg_type));
        msg->DataOffset = cpu_to_le32(sizeof(struct rndis_packet_msg_type) - 8);
        msg->DataLength = cpu_to_le32(size);
        /* msg->OOBDataOffset;
         * msg->OOBDataLength;
         * msg->NumOOBDataElements;
         * msg->PerPacketInfoOffset;
         * msg->PerPacketInfoLength;
         * msg->VcHandle;
         * msg->Reserved;
         */
        memcpy(msg + 1, buf, size);
        s->in_len = size + sizeof(struct rndis_packet_msg_type);
    } else {
        if (size > sizeof(s->in_buf))
            return -1;
        memcpy(s->in_buf, buf, size);
        s->in_len = size;
    }
    s->in_ptr = 0;
    return size;
}

static int usbnet_can_receive(VLANClientState *nc)
{
    USBNetState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    if (is_rndis(s) && s->rndis_state != RNDIS_DATA_INITIALIZED) {
        return 1;
    }

    return !s->in_len;
}

static void usbnet_cleanup(VLANClientState *nc)
{
    USBNetState *s = DO_UPCAST(NICState, nc, nc)->opaque;

    s->nic = NULL;
}

static void usb_net_handle_destroy(USBDevice *dev)
{
    USBNetState *s = (USBNetState *) dev;

    /* TODO: remove the nd_table[] entry */
    rndis_clear_responsequeue(s);
    qemu_del_vlan_client(&s->nic->nc);
}

static NetClientInfo net_usbnet_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = usbnet_can_receive,
    .receive = usbnet_receive,
    .cleanup = usbnet_cleanup,
};

static int usb_net_initfn(USBDevice *dev)
{
    USBNetState *s = DO_UPCAST(USBNetState, dev, dev);

    usb_desc_init(dev);

    s->rndis_state = RNDIS_UNINITIALIZED;
    QTAILQ_INIT(&s->rndis_resp);

    s->medium = 0;	/* NDIS_MEDIUM_802_3 */
    s->speed = 1000000; /* 100MBps, in 100Bps units */
    s->media_state = 0;	/* NDIS_MEDIA_STATE_CONNECTED */;
    s->filter = 0;
    s->vendorid = 0x1234;

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_usbnet_info, &s->conf,
                          object_get_typename(OBJECT(s)), s->dev.qdev.id, s);
    qemu_format_nic_info_str(&s->nic->nc, s->conf.macaddr.a);
    snprintf(s->usbstring_mac, sizeof(s->usbstring_mac),
             "%02x%02x%02x%02x%02x%02x",
             0x40,
             s->conf.macaddr.a[1],
             s->conf.macaddr.a[2],
             s->conf.macaddr.a[3],
             s->conf.macaddr.a[4],
             s->conf.macaddr.a[5]);
    usb_desc_set_string(dev, STRING_ETHADDR, s->usbstring_mac);

    add_boot_device_path(s->conf.bootindex, &dev->qdev, "/ethernet@0");
    return 0;
}

static USBDevice *usb_net_init(USBBus *bus, const char *cmdline)
{
    USBDevice *dev;
    QemuOpts *opts;
    int idx;

    opts = qemu_opts_parse(qemu_find_opts("net"), cmdline, 0);
    if (!opts) {
        return NULL;
    }
    qemu_opt_set(opts, "type", "nic");
    qemu_opt_set(opts, "model", "usb");

    idx = net_client_init(NULL, opts, 0);
    if (idx == -1) {
        return NULL;
    }

    dev = usb_create(bus, "usb-net");
    if (!dev) {
        return NULL;
    }
    qdev_set_nic_properties(&dev->qdev, &nd_table[idx]);
    qdev_init_nofail(&dev->qdev);
    return dev;
}

static const VMStateDescription vmstate_usb_net = {
    .name = "usb-net",
    .unmigratable = 1,
};

static Property net_properties[] = {
    DEFINE_NIC_PROPERTIES(USBNetState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_net_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->init           = usb_net_initfn;
    uc->product_desc   = "QEMU USB Network Interface";
    uc->usb_desc       = &desc_net;
    uc->handle_reset   = usb_net_handle_reset;
    uc->handle_control = usb_net_handle_control;
    uc->handle_data    = usb_net_handle_data;
    uc->handle_destroy = usb_net_handle_destroy;
    dc->fw_name = "network";
    dc->vmsd = &vmstate_usb_net;
    dc->props = net_properties;
}

static TypeInfo net_info = {
    .name          = "usb-net",
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBNetState),
    .class_init    = usb_net_class_initfn,
};

static void usb_net_register_types(void)
{
    type_register_static(&net_info);
    usb_legacy_register("usb-net", "net", usb_net_init);
}

type_init(usb_net_register_types)
