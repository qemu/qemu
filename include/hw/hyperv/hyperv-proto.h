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

/*
 * Hypercall numbers
 */
#define HV_POST_MESSAGE                       0x005c
#define HV_SIGNAL_EVENT                       0x005d
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

#endif
