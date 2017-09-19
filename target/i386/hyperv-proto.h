/*
 * Definitions for Hyper-V guest/hypervisor interaction
 *
 * Copyright (C) 2017 Parallels International GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TARGET_I386_HYPERV_PROTO_H
#define TARGET_I386_HYPERV_PROTO_H

#include "qemu/bitmap.h"

#define HV_CPUID_VENDOR_AND_MAX_FUNCTIONS     0x40000000
#define HV_CPUID_INTERFACE                    0x40000001
#define HV_CPUID_VERSION                      0x40000002
#define HV_CPUID_FEATURES                     0x40000003
#define HV_CPUID_ENLIGHTMENT_INFO             0x40000004
#define HV_CPUID_IMPLEMENT_LIMITS             0x40000005
#define HV_CPUID_MIN                          0x40000005
#define HV_CPUID_MAX                          0x4000ffff
#define HV_HYPERVISOR_PRESENT_BIT             0x80000000

/*
 * HV_CPUID_FEATURES.EAX bits
 */
#define HV_VP_RUNTIME_AVAILABLE      (1u << 0)
#define HV_TIME_REF_COUNT_AVAILABLE  (1u << 1)
#define HV_SYNIC_AVAILABLE           (1u << 2)
#define HV_SYNTIMERS_AVAILABLE       (1u << 3)
#define HV_APIC_ACCESS_AVAILABLE     (1u << 4)
#define HV_HYPERCALL_AVAILABLE       (1u << 5)
#define HV_VP_INDEX_AVAILABLE        (1u << 6)
#define HV_RESET_AVAILABLE           (1u << 7)
#define HV_REFERENCE_TSC_AVAILABLE   (1u << 9)
#define HV_ACCESS_FREQUENCY_MSRS     (1u << 11)


/*
 * HV_CPUID_FEATURES.EDX bits
 */
#define HV_MWAIT_AVAILABLE                      (1u << 0)
#define HV_GUEST_DEBUGGING_AVAILABLE            (1u << 1)
#define HV_PERF_MONITOR_AVAILABLE               (1u << 2)
#define HV_CPU_DYNAMIC_PARTITIONING_AVAILABLE   (1u << 3)
#define HV_HYPERCALL_PARAMS_XMM_AVAILABLE       (1u << 4)
#define HV_GUEST_IDLE_STATE_AVAILABLE           (1u << 5)
#define HV_FREQUENCY_MSRS_AVAILABLE             (1u << 8)
#define HV_GUEST_CRASH_MSR_AVAILABLE            (1u << 10)

/*
 * HV_CPUID_ENLIGHTMENT_INFO.EAX bits
 */
#define HV_AS_SWITCH_RECOMMENDED            (1u << 0)
#define HV_LOCAL_TLB_FLUSH_RECOMMENDED      (1u << 1)
#define HV_REMOTE_TLB_FLUSH_RECOMMENDED     (1u << 2)
#define HV_APIC_ACCESS_RECOMMENDED          (1u << 3)
#define HV_SYSTEM_RESET_RECOMMENDED         (1u << 4)
#define HV_RELAXED_TIMING_RECOMMENDED       (1u << 5)

/*
 * Basic virtualized MSRs
 */
#define HV_X64_MSR_GUEST_OS_ID                0x40000000
#define HV_X64_MSR_HYPERCALL                  0x40000001
#define HV_X64_MSR_VP_INDEX                   0x40000002
#define HV_X64_MSR_RESET                      0x40000003
#define HV_X64_MSR_VP_RUNTIME                 0x40000010
#define HV_X64_MSR_TIME_REF_COUNT             0x40000020
#define HV_X64_MSR_REFERENCE_TSC              0x40000021
#define HV_X64_MSR_TSC_FREQUENCY              0x40000022
#define HV_X64_MSR_APIC_FREQUENCY             0x40000023

/*
 * Virtual APIC MSRs
 */
#define HV_X64_MSR_EOI                        0x40000070
#define HV_X64_MSR_ICR                        0x40000071
#define HV_X64_MSR_TPR                        0x40000072
#define HV_X64_MSR_APIC_ASSIST_PAGE           0x40000073

/*
 * Synthetic interrupt controller MSRs
 */
#define HV_X64_MSR_SCONTROL                   0x40000080
#define HV_X64_MSR_SVERSION                   0x40000081
#define HV_X64_MSR_SIEFP                      0x40000082
#define HV_X64_MSR_SIMP                       0x40000083
#define HV_X64_MSR_EOM                        0x40000084
#define HV_X64_MSR_SINT0                      0x40000090
#define HV_X64_MSR_SINT1                      0x40000091
#define HV_X64_MSR_SINT2                      0x40000092
#define HV_X64_MSR_SINT3                      0x40000093
#define HV_X64_MSR_SINT4                      0x40000094
#define HV_X64_MSR_SINT5                      0x40000095
#define HV_X64_MSR_SINT6                      0x40000096
#define HV_X64_MSR_SINT7                      0x40000097
#define HV_X64_MSR_SINT8                      0x40000098
#define HV_X64_MSR_SINT9                      0x40000099
#define HV_X64_MSR_SINT10                     0x4000009A
#define HV_X64_MSR_SINT11                     0x4000009B
#define HV_X64_MSR_SINT12                     0x4000009C
#define HV_X64_MSR_SINT13                     0x4000009D
#define HV_X64_MSR_SINT14                     0x4000009E
#define HV_X64_MSR_SINT15                     0x4000009F

/*
 * Synthetic timer MSRs
 */
#define HV_X64_MSR_STIMER0_CONFIG               0x400000B0
#define HV_X64_MSR_STIMER0_COUNT                0x400000B1
#define HV_X64_MSR_STIMER1_CONFIG               0x400000B2
#define HV_X64_MSR_STIMER1_COUNT                0x400000B3
#define HV_X64_MSR_STIMER2_CONFIG               0x400000B4
#define HV_X64_MSR_STIMER2_COUNT                0x400000B5
#define HV_X64_MSR_STIMER3_CONFIG               0x400000B6
#define HV_X64_MSR_STIMER3_COUNT                0x400000B7

/*
 * Guest crash notification MSRs
 */
#define HV_X64_MSR_CRASH_P0                     0x40000100
#define HV_X64_MSR_CRASH_P1                     0x40000101
#define HV_X64_MSR_CRASH_P2                     0x40000102
#define HV_X64_MSR_CRASH_P3                     0x40000103
#define HV_X64_MSR_CRASH_P4                     0x40000104
#define HV_CRASH_PARAMS    (HV_X64_MSR_CRASH_P4 - HV_X64_MSR_CRASH_P0 + 1)
#define HV_X64_MSR_CRASH_CTL                    0x40000105
#define HV_CRASH_CTL_NOTIFY                     (1ull << 63)

/*
 * Hypercall status code
 */
#define HV_STATUS_SUCCESS                     0
#define HV_STATUS_INVALID_HYPERCALL_CODE      2
#define HV_STATUS_INVALID_HYPERCALL_INPUT     3
#define HV_STATUS_INVALID_ALIGNMENT           4
#define HV_STATUS_INVALID_PARAMETER           5
#define HV_STATUS_INSUFFICIENT_MEMORY         11
#define HV_STATUS_INVALID_CONNECTION_ID       18
#define HV_STATUS_INSUFFICIENT_BUFFERS        19

/*
 * Hypercall numbers
 */
#define HV_POST_MESSAGE                       0x005c
#define HV_SIGNAL_EVENT                       0x005d
#define HV_HYPERCALL_FAST                     (1u << 16)

/*
 * Hypercall MSR bits
 */
#define HV_HYPERCALL_ENABLE                   (1u << 0)

/*
 * Synthetic interrupt controller definitions
 */
#define HV_SYNIC_VERSION                      1
#define HV_SINT_COUNT                         16
#define HV_SYNIC_ENABLE                       (1u << 0)
#define HV_SIMP_ENABLE                        (1u << 0)
#define HV_SIEFP_ENABLE                       (1u << 0)
#define HV_SINT_MASKED                        (1u << 16)
#define HV_SINT_AUTO_EOI                      (1u << 17)
#define HV_SINT_VECTOR_MASK                   0xff

#define HV_STIMER_COUNT                       4

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
