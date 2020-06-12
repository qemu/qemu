/*
 * QEMU Hyper-V VMBus support
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_VMBUS_PROTO_H
#define HW_HYPERV_VMBUS_PROTO_H

#define VMBUS_VERSION_WS2008                    ((0 << 16) | (13))
#define VMBUS_VERSION_WIN7                      ((1 << 16) | (1))
#define VMBUS_VERSION_WIN8                      ((2 << 16) | (4))
#define VMBUS_VERSION_WIN8_1                    ((3 << 16) | (0))
#define VMBUS_VERSION_WIN10                     ((4 << 16) | (0))
#define VMBUS_VERSION_INVAL                     -1
#define VMBUS_VERSION_CURRENT                   VMBUS_VERSION_WIN10

#define VMBUS_MESSAGE_CONNECTION_ID             1
#define VMBUS_EVENT_CONNECTION_ID               2
#define VMBUS_MONITOR_CONNECTION_ID             3
#define VMBUS_SINT                              2

#define VMBUS_MSG_INVALID               0
#define VMBUS_MSG_OFFERCHANNEL          1
#define VMBUS_MSG_RESCIND_CHANNELOFFER  2
#define VMBUS_MSG_REQUESTOFFERS         3
#define VMBUS_MSG_ALLOFFERS_DELIVERED   4
#define VMBUS_MSG_OPENCHANNEL           5
#define VMBUS_MSG_OPENCHANNEL_RESULT    6
#define VMBUS_MSG_CLOSECHANNEL          7
#define VMBUS_MSG_GPADL_HEADER          8
#define VMBUS_MSG_GPADL_BODY            9
#define VMBUS_MSG_GPADL_CREATED         10
#define VMBUS_MSG_GPADL_TEARDOWN        11
#define VMBUS_MSG_GPADL_TORNDOWN        12
#define VMBUS_MSG_RELID_RELEASED        13
#define VMBUS_MSG_INITIATE_CONTACT      14
#define VMBUS_MSG_VERSION_RESPONSE      15
#define VMBUS_MSG_UNLOAD                16
#define VMBUS_MSG_UNLOAD_RESPONSE       17
#define VMBUS_MSG_COUNT                 18

#define VMBUS_MESSAGE_SIZE_ALIGN        sizeof(uint64_t)

#define VMBUS_PACKET_INVALID                    0x0
#define VMBUS_PACKET_SYNCH                      0x1
#define VMBUS_PACKET_ADD_XFER_PAGESET           0x2
#define VMBUS_PACKET_RM_XFER_PAGESET            0x3
#define VMBUS_PACKET_ESTABLISH_GPADL            0x4
#define VMBUS_PACKET_TEARDOWN_GPADL             0x5
#define VMBUS_PACKET_DATA_INBAND                0x6
#define VMBUS_PACKET_DATA_USING_XFER_PAGES      0x7
#define VMBUS_PACKET_DATA_USING_GPADL           0x8
#define VMBUS_PACKET_DATA_USING_GPA_DIRECT      0x9
#define VMBUS_PACKET_CANCEL_REQUEST             0xa
#define VMBUS_PACKET_COMP                       0xb
#define VMBUS_PACKET_DATA_USING_ADDITIONAL_PKT  0xc
#define VMBUS_PACKET_ADDITIONAL_DATA            0xd

#define VMBUS_CHANNEL_USER_DATA_SIZE            120

#define VMBUS_OFFER_MONITOR_ALLOCATED           0x1
#define VMBUS_OFFER_INTERRUPT_DEDICATED         0x1

#define VMBUS_RING_BUFFER_FEAT_PENDING_SZ       (1ul << 0)

#define VMBUS_CHANNEL_ENUMERATE_DEVICE_INTERFACE      0x1
#define VMBUS_CHANNEL_SERVER_SUPPORTS_TRANSFER_PAGES  0x2
#define VMBUS_CHANNEL_SERVER_SUPPORTS_GPADLS          0x4
#define VMBUS_CHANNEL_NAMED_PIPE_MODE                 0x10
#define VMBUS_CHANNEL_LOOPBACK_OFFER                  0x100
#define VMBUS_CHANNEL_PARENT_OFFER                    0x200
#define VMBUS_CHANNEL_REQUEST_MONITORED_NOTIFICATION  0x400
#define VMBUS_CHANNEL_TLNPI_PROVIDER_OFFER            0x2000

#define VMBUS_PACKET_FLAG_REQUEST_COMPLETION    1

typedef struct vmbus_message_header {
    uint32_t message_type;
    uint32_t _padding;
} vmbus_message_header;

typedef struct vmbus_message_initiate_contact {
    vmbus_message_header header;
    uint32_t version_requested;
    uint32_t target_vcpu;
    uint64_t interrupt_page;
    uint64_t monitor_page1;
    uint64_t monitor_page2;
} vmbus_message_initiate_contact;

typedef struct vmbus_message_version_response {
    vmbus_message_header header;
    uint8_t version_supported;
    uint8_t status;
} vmbus_message_version_response;

typedef struct vmbus_message_offer_channel {
    vmbus_message_header header;
    uint8_t  type_uuid[16];
    uint8_t  instance_uuid[16];
    uint64_t _reserved1;
    uint64_t _reserved2;
    uint16_t channel_flags;
    uint16_t mmio_size_mb;
    uint8_t  user_data[VMBUS_CHANNEL_USER_DATA_SIZE];
    uint16_t sub_channel_index;
    uint16_t _reserved3;
    uint32_t child_relid;
    uint8_t  monitor_id;
    uint8_t  monitor_flags;
    uint16_t interrupt_flags;
    uint32_t connection_id;
} vmbus_message_offer_channel;

typedef struct vmbus_message_rescind_channel_offer {
    vmbus_message_header header;
    uint32_t child_relid;
} vmbus_message_rescind_channel_offer;

typedef struct vmbus_gpa_range {
    uint32_t byte_count;
    uint32_t byte_offset;
    uint64_t pfn_array[];
} vmbus_gpa_range;

typedef struct vmbus_message_gpadl_header {
    vmbus_message_header header;
    uint32_t child_relid;
    uint32_t gpadl_id;
    uint16_t range_buflen;
    uint16_t rangecount;
    vmbus_gpa_range range[];
} QEMU_PACKED vmbus_message_gpadl_header;

typedef struct vmbus_message_gpadl_body {
    vmbus_message_header header;
    uint32_t message_number;
    uint32_t gpadl_id;
    uint64_t pfn_array[];
} vmbus_message_gpadl_body;

typedef struct vmbus_message_gpadl_created {
    vmbus_message_header header;
    uint32_t child_relid;
    uint32_t gpadl_id;
    uint32_t status;
} vmbus_message_gpadl_created;

typedef struct vmbus_message_gpadl_teardown {
    vmbus_message_header header;
    uint32_t child_relid;
    uint32_t gpadl_id;
} vmbus_message_gpadl_teardown;

typedef struct vmbus_message_gpadl_torndown {
    vmbus_message_header header;
    uint32_t gpadl_id;
} vmbus_message_gpadl_torndown;

typedef struct vmbus_message_open_channel {
    vmbus_message_header header;
    uint32_t child_relid;
    uint32_t open_id;
    uint32_t ring_buffer_gpadl_id;
    uint32_t target_vp;
    uint32_t ring_buffer_offset;
    uint8_t  user_data[VMBUS_CHANNEL_USER_DATA_SIZE];
} vmbus_message_open_channel;

typedef struct vmbus_message_open_result {
    vmbus_message_header header;
    uint32_t child_relid;
    uint32_t open_id;
    uint32_t status;
} vmbus_message_open_result;

typedef struct vmbus_message_close_channel {
    vmbus_message_header header;
    uint32_t child_relid;
} vmbus_message_close_channel;

typedef struct vmbus_ring_buffer {
    uint32_t write_index;
    uint32_t read_index;
    uint32_t interrupt_mask;
    uint32_t pending_send_sz;
    uint32_t _reserved1[12];
    uint32_t feature_bits;
} vmbus_ring_buffer;

typedef struct vmbus_packet_hdr {
    uint16_t type;
    uint16_t offset_qwords;
    uint16_t len_qwords;
    uint16_t flags;
    uint64_t transaction_id;
} vmbus_packet_hdr;

typedef struct vmbus_pkt_gpa_direct {
    uint32_t _reserved;
    uint32_t rangecount;
    vmbus_gpa_range range[];
} vmbus_pkt_gpa_direct;

typedef struct vmbus_xferpg_range {
    uint32_t byte_count;
    uint32_t byte_offset;
} vmbus_xferpg_range;

typedef struct vmbus_pkt_xferpg {
    uint16_t buffer_id;
    uint8_t sender_owns_set;
    uint8_t _reserved;
    uint32_t rangecount;
    vmbus_xferpg_range range[];
} vmbus_pkt_xferpg;

#endif
