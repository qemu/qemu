/*
 * Definitions for Hyper-V guest/hypervisor interaction - x86-specific part
 *
 * Copyright (c) 2017-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef TARGET_I386_HYPERV_PROTO_H
#define TARGET_I386_HYPERV_PROTO_H

#include "hw/hyperv/hyperv-proto.h"

#define HV_CPUID_VENDOR_AND_MAX_FUNCTIONS     0x40000000
#define HV_CPUID_INTERFACE                    0x40000001
#define HV_CPUID_VERSION                      0x40000002
#define HV_CPUID_FEATURES                     0x40000003
#define HV_CPUID_ENLIGHTMENT_INFO             0x40000004
#define HV_CPUID_IMPLEMENT_LIMITS             0x40000005
#define HV_CPUID_NESTED_FEATURES              0x4000000A
#define HV_CPUID_SYNDBG_VENDOR_AND_MAX_FUNCTIONS    0x40000080
#define HV_CPUID_SYNDBG_INTERFACE                   0x40000081
#define HV_CPUID_SYNDBG_PLATFORM_CAPABILITIES       0x40000082
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
#define HV_ACCESS_REENLIGHTENMENTS_CONTROL  (1u << 13)

/*
 * HV_CPUID_FEATURES.EBX bits
 */
#define HV_POST_MESSAGES             (1u << 4)
#define HV_SIGNAL_EVENTS             (1u << 5)

/*
 * HV_CPUID_FEATURES.EDX bits
 */
#define HV_MWAIT_AVAILABLE                      (1u << 0)
#define HV_GUEST_DEBUGGING_AVAILABLE            (1u << 1)
#define HV_PERF_MONITOR_AVAILABLE               (1u << 2)
#define HV_CPU_DYNAMIC_PARTITIONING_AVAILABLE   (1u << 3)
#define HV_HYPERCALL_XMM_INPUT_AVAILABLE        (1u << 4)
#define HV_GUEST_IDLE_STATE_AVAILABLE           (1u << 5)
#define HV_FREQUENCY_MSRS_AVAILABLE             (1u << 8)
#define HV_GUEST_CRASH_MSR_AVAILABLE            (1u << 10)
#define HV_FEATURE_DEBUG_MSRS_AVAILABLE         (1u << 11)
#define HV_EXT_GVA_RANGES_FLUSH_AVAILABLE       (1u << 14)
#define HV_STIMER_DIRECT_MODE_AVAILABLE         (1u << 19)

/*
 * HV_CPUID_FEATURES.EBX bits
 */
#define HV_PARTITION_DEBUGGING_ALLOWED          (1u << 12)

/*
 * HV_CPUID_ENLIGHTMENT_INFO.EAX bits
 */
#define HV_AS_SWITCH_RECOMMENDED            (1u << 0)
#define HV_LOCAL_TLB_FLUSH_RECOMMENDED      (1u << 1)
#define HV_REMOTE_TLB_FLUSH_RECOMMENDED     (1u << 2)
#define HV_APIC_ACCESS_RECOMMENDED          (1u << 3)
#define HV_SYSTEM_RESET_RECOMMENDED         (1u << 4)
#define HV_RELAXED_TIMING_RECOMMENDED       (1u << 5)
#define HV_DEPRECATING_AEOI_RECOMMENDED     (1u << 9)
#define HV_CLUSTER_IPI_RECOMMENDED          (1u << 10)
#define HV_EX_PROCESSOR_MASKS_RECOMMENDED   (1u << 11)
#define HV_ENLIGHTENED_VMCS_RECOMMENDED     (1u << 14)
#define HV_NO_NONARCH_CORESHARING           (1u << 18)

/*
 * HV_CPUID_SYNDBG_PLATFORM_CAPABILITIES.EAX bits
 */
#define HV_SYNDBG_CAP_ALLOW_KERNEL_DEBUGGING    (1u << 1)

/*
 * HV_CPUID_NESTED_FEATURES.EAX bits
 */
#define HV_NESTED_DIRECT_FLUSH              (1u << 17)
#define HV_NESTED_MSR_BITMAP                (1u << 19)

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
 * Reenlightenment notification MSRs
 */
#define HV_X64_MSR_REENLIGHTENMENT_CONTROL      0x40000106
#define HV_REENLIGHTENMENT_ENABLE_BIT           (1u << 16)
#define HV_X64_MSR_TSC_EMULATION_CONTROL        0x40000107
#define HV_X64_MSR_TSC_EMULATION_STATUS         0x40000108

/*
 * Hypercall MSR bits
 */
#define HV_HYPERCALL_ENABLE                   (1u << 0)

/*
 * Synthetic interrupt controller definitions
 */
#define HV_SYNIC_VERSION                      1
#define HV_SYNIC_ENABLE                       (1u << 0)
#define HV_SIMP_ENABLE                        (1u << 0)
#define HV_SIEFP_ENABLE                       (1u << 0)
#define HV_SINT_MASKED                        (1u << 16)
#define HV_SINT_AUTO_EOI                      (1u << 17)
#define HV_SINT_VECTOR_MASK                   0xff

#define HV_STIMER_COUNT                       4

/*
 * Synthetic debugger control definitions
 */
#define HV_SYNDBG_CONTROL_SEND              (1u << 0)
#define HV_SYNDBG_CONTROL_RECV              (1u << 1)
#define HV_SYNDBG_CONTROL_SEND_SIZE(ctl)    ((ctl >> 16) & 0xffff)
#define HV_SYNDBG_STATUS_INVALID            (0)
#define HV_SYNDBG_STATUS_SEND_SUCCESS       (1u << 0)
#define HV_SYNDBG_STATUS_RECV_SUCCESS       (1u << 2)
#define HV_SYNDBG_STATUS_RESET              (1u << 3)
#define HV_SYNDBG_STATUS_SET_SIZE(st, sz)   (st | (sz << 16))

#endif
