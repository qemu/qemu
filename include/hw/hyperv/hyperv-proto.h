/*
 * Definitions for Hyper-V guest/hypervisor interaction
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_HYPERV_PROTO_H
#define HW_HYPERV_HYPERV_PROTO_H

#include "qemu/bitmap.h"

/*
 * Hypercall status code
 */
#define HV_STATUS_SUCCESS                     0
#define HV_STATUS_INVALID_HYPERCALL_CODE      2
#define HV_STATUS_INVALID_HYPERCALL_INPUT     3
#define HV_STATUS_INVALID_ALIGNMENT           4
#define HV_STATUS_INVALID_PARAMETER           5
#define HV_STATUS_INSUFFICIENT_MEMORY         11
#define HV_STATUS_INVALID_PORT_ID             17
#define HV_STATUS_INVALID_CONNECTION_ID       18
#define HV_STATUS_INSUFFICIENT_BUFFERS        19
#define HV_STATUS_NOT_ACKNOWLEDGED            20
#define HV_STATUS_NO_DATA                     27

/*
 * Hypercall numbers
 */
#define HV_POST_MESSAGE                       0x005c
#define HV_SIGNAL_EVENT                       0x005d
#define HV_POST_DEBUG_DATA                    0x0069
#define HV_RETRIEVE_DEBUG_DATA                0x006a
#define HV_RESET_DEBUG_SESSION                0x006b
#define HV_HYPERCALL_FAST                     (1u << 16)

/*
 * Message size
 */
#define HV_MESSAGE_PAYLOAD_SIZE               240

/*
 * Message types
 */
#define HV_MESSAGE_NONE                       0x00000000
#define HV_MESSAGE_VMBUS                      0x00000001
#define HV_MESSAGE_UNMAPPED_GPA               0x80000000
#define HV_MESSAGE_GPA_INTERCEPT              0x80000001
#define HV_MESSAGE_TIMER_EXPIRED              0x80000010
#define HV_MESSAGE_INVALID_VP_REGISTER_VALUE  0x80000020
#define HV_MESSAGE_UNRECOVERABLE_EXCEPTION    0x80000021
#define HV_MESSAGE_UNSUPPORTED_FEATURE        0x80000022
#define HV_MESSAGE_EVENTLOG_BUFFERCOMPLETE    0x80000040
#define HV_MESSAGE_X64_IOPORT_INTERCEPT       0x80010000
#define HV_MESSAGE_X64_MSR_INTERCEPT          0x80010001
#define HV_MESSAGE_X64_CPUID_INTERCEPT        0x80010002
#define HV_MESSAGE_X64_EXCEPTION_INTERCEPT    0x80010003
#define HV_MESSAGE_X64_APIC_EOI               0x80010004
#define HV_MESSAGE_X64_LEGACY_FP_ERROR        0x80010005

/*
 * Hyper-V Synthetic debug options MSR
 */
#define HV_X64_MSR_SYNDBG_CONTROL               0x400000F1
#define HV_X64_MSR_SYNDBG_STATUS                0x400000F2
#define HV_X64_MSR_SYNDBG_SEND_BUFFER           0x400000F3
#define HV_X64_MSR_SYNDBG_RECV_BUFFER           0x400000F4
#define HV_X64_MSR_SYNDBG_PENDING_BUFFER        0x400000F5
#define HV_X64_MSR_SYNDBG_OPTIONS               0x400000FF

#define HV_X64_SYNDBG_OPTION_USE_HCALLS         BIT(2)

/*
 * Message flags
 */
#define HV_MESSAGE_FLAG_PENDING               0x1

/*
 * Number of synthetic interrupts
 */
#define HV_SINT_COUNT                         16

/*
 * Event flags number per SINT
 */
#define HV_EVENT_FLAGS_COUNT                  (256 * 8)

/*
 * Connection id valid bits
 */
#define HV_CONNECTION_ID_MASK                 0x00ffffff

/*
 * Input structure for POST_MESSAGE hypercall
 */
struct hyperv_post_message_input {
    uint32_t connection_id;
    uint32_t _reserved;
    uint32_t message_type;
    uint32_t payload_size;
    uint8_t  payload[HV_MESSAGE_PAYLOAD_SIZE];
};

/*
 * Input structure for SIGNAL_EVENT hypercall
 */
struct hyperv_signal_event_input {
    uint32_t connection_id;
    uint16_t flag_number;
    uint16_t _reserved_zero;
};

/*
 * SynIC message structures
 */
struct hyperv_message_header {
    uint32_t message_type;
    uint8_t  payload_size;
    uint8_t  message_flags; /* HV_MESSAGE_FLAG_XX */
    uint8_t  _reserved[2];
    uint64_t sender;
};

struct hyperv_message {
    struct hyperv_message_header header;
    uint8_t payload[HV_MESSAGE_PAYLOAD_SIZE];
};

struct hyperv_message_page {
    struct hyperv_message slot[HV_SINT_COUNT];
};

/*
 * SynIC event flags structures
 */
struct hyperv_event_flags {
    DECLARE_BITMAP(flags, HV_EVENT_FLAGS_COUNT);
};

struct hyperv_event_flags_page {
    struct hyperv_event_flags slot[HV_SINT_COUNT];
};

/*
 * Kernel debugger structures
 */

/* Options flags for hyperv_reset_debug_session */
#define HV_DEBUG_PURGE_INCOMING_DATA        0x00000001
#define HV_DEBUG_PURGE_OUTGOING_DATA        0x00000002
struct hyperv_reset_debug_session_input {
    uint32_t options;
} __attribute__ ((__packed__));

struct hyperv_reset_debug_session_output {
    uint32_t host_ip;
    uint32_t target_ip;
    uint16_t host_port;
    uint16_t target_port;
    uint8_t host_mac[6];
    uint8_t target_mac[6];
} __attribute__ ((__packed__));

/* Options for hyperv_post_debug_data */
#define HV_DEBUG_POST_LOOP                  0x00000001

struct hyperv_post_debug_data_input {
    uint32_t count;
    uint32_t options;
    /*uint8_t data[HV_HYP_PAGE_SIZE - 2 * sizeof(uint32_t)];*/
} __attribute__ ((__packed__));

struct hyperv_post_debug_data_output {
    uint32_t pending_count;
} __attribute__ ((__packed__));

/* Options for hyperv_retrieve_debug_data */
#define HV_DEBUG_RETRIEVE_LOOP              0x00000001
#define HV_DEBUG_RETRIEVE_TEST_ACTIVITY     0x00000002

struct hyperv_retrieve_debug_data_input {
    uint32_t count;
    uint32_t options;
    uint64_t timeout;
} __attribute__ ((__packed__));

struct hyperv_retrieve_debug_data_output {
    uint32_t retrieved_count;
    uint32_t remaining_count;
} __attribute__ ((__packed__));
#endif
