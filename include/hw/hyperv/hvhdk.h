/*
 * Type definitions for the mshv host.
 *
 * Copyright Microsoft, Corp. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HYPERV_HVHDK_H
#define HW_HYPERV_HVHDK_H

#define HV_PARTITION_SYNTHETIC_PROCESSOR_FEATURES_BANKS 1

struct hv_input_set_partition_property {
    uint64_t partition_id;
    uint32_t property_code; /* enum hv_partition_property_code */
    uint32_t padding;
    uint64_t property_value;
};

union hv_partition_synthetic_processor_features {
    uint64_t as_uint64[HV_PARTITION_SYNTHETIC_PROCESSOR_FEATURES_BANKS];

    struct {
        /*
         * Report a hypervisor is present. CPUID leaves
         * 0x40000000 and 0x40000001 are supported.
         */
        uint64_t hypervisor_present:1;

        /*
         * Features associated with HV#1:
         */

        /* Report support for Hv1 (CPUID leaves 0x40000000 - 0x40000006). */
        uint64_t hv1:1;

        /*
         * Access to HV_X64_MSR_VP_RUNTIME.
         * Corresponds to access_vp_run_time_reg privilege.
         */
        uint64_t access_vp_run_time_reg:1;

        /*
         * Access to HV_X64_MSR_TIME_REF_COUNT.
         * Corresponds to access_partition_reference_counter privilege.
         */
        uint64_t access_partition_reference_counter:1;

        /*
         * Access to SINT-related registers (HV_X64_MSR_SCONTROL through
         * HV_X64_MSR_EOM and HV_X64_MSR_SINT0 through HV_X64_MSR_SINT15).
         * Corresponds to access_synic_regs privilege.
         */
        uint64_t access_synic_regs:1;

        /*
         * Access to synthetic timers and associated MSRs
         * (HV_X64_MSR_STIMER0_CONFIG through HV_X64_MSR_STIMER3_COUNT).
         * Corresponds to access_synthetic_timer_regs privilege.
         */
        uint64_t access_synthetic_timer_regs:1;

        /*
         * Access to APIC MSRs (HV_X64_MSR_EOI, HV_X64_MSR_ICR and
         * HV_X64_MSR_TPR) as well as the VP assist page.
         * Corresponds to access_intr_ctrl_regs privilege.
         */
        uint64_t access_intr_ctrl_regs:1;

        /*
         * Access to registers associated with hypercalls
         * (HV_X64_MSR_GUEST_OS_ID and HV_X64_MSR_HYPERCALL).
         * Corresponds to access_hypercall_msrs privilege.
         */
        uint64_t access_hypercall_regs:1;

        /* VP index can be queried. corresponds to access_vp_index privilege. */
        uint64_t access_vp_index:1;

        /*
         * Access to the reference TSC. Corresponds to
         * access_partition_reference_tsc privilege.
         */
        uint64_t access_partition_reference_tsc:1;

        /*
         * Partition has access to the guest idle reg. Corresponds to
         * access_guest_idle_reg privilege.
         */
        uint64_t access_guest_idle_reg:1;

        /*
         * Partition has access to frequency regs. corresponds to
         * access_frequency_regs privilege.
         */
        uint64_t access_frequency_regs:1;

        uint64_t reserved_z12:1; /* Reserved for access_reenlightenment_controls */
        uint64_t reserved_z13:1; /* Reserved for access_root_scheduler_reg */
        uint64_t reserved_z14:1; /* Reserved for access_tsc_invariant_controls */

        /*
         * Extended GVA ranges for HvCallFlushVirtualAddressList hypercall.
         * Corresponds to privilege.
         */
        uint64_t enable_extended_gva_ranges_for_flush_virtual_address_list:1;

        uint64_t reserved_z16:1; /* Reserved for access_vsm. */
        uint64_t reserved_z17:1; /* Reserved for access_vp_registers. */

        /* Use fast hypercall output. Corresponds to privilege. */
        uint64_t fast_hypercall_output:1;

        uint64_t reserved_z19:1; /* Reserved for enable_extended_hypercalls. */

        /*
         * HvStartVirtualProcessor can be used to start virtual processors.
         * Corresponds to privilege.
         */
        uint64_t start_virtual_processor:1;

        uint64_t reserved_z21:1; /* Reserved for Isolation. */

        /* Synthetic timers in direct mode. */
        uint64_t direct_synthetic_timers:1;

        uint64_t reserved_z23:1; /* Reserved for synthetic time unhalted timer */

        /* Use extended processor masks. */
        uint64_t extended_processor_masks:1;

        /*
         * HvCallFlushVirtualAddressSpace / HvCallFlushVirtualAddressList are
         * supported.
         */
        uint64_t tb_flush_hypercalls:1;

        /* HvCallSendSyntheticClusterIpi is supported. */
        uint64_t synthetic_cluster_ipi:1;

        /* HvCallNotifyLongSpinWait is supported. */
        uint64_t notify_long_spin_wait:1;

        /* HvCallQueryNumaDistance is supported. */
        uint64_t query_numa_distance:1;

        /* HvCallSignalEvent is supported. Corresponds to privilege. */
        uint64_t signal_events:1;

        /* HvCallRetargetDeviceInterrupt is supported. */
        uint64_t retarget_device_interrupt:1;

        /* HvCallRestorePartitionTime is supported. */
        uint64_t restore_time:1;

        /* EnlightenedVmcs nested enlightenment is supported. */
        uint64_t enlightened_vmcs:1;

        uint64_t reserved:30;
    };
};

enum hv_translate_gva_result_code {
    HV_TRANSLATE_GVA_SUCCESS                    = 0,

    /* Translation failures. */
    HV_TRANSLATE_GVA_PAGE_NOT_PRESENT           = 1,
    HV_TRANSLATE_GVA_PRIVILEGE_VIOLATION        = 2,
    HV_TRANSLATE_GVA_INVALIDE_PAGE_TABLE_FLAGS  = 3,

    /* GPA access failures. */
    HV_TRANSLATE_GVA_GPA_UNMAPPED               = 4,
    HV_TRANSLATE_GVA_GPA_NO_READ_ACCESS         = 5,
    HV_TRANSLATE_GVA_GPA_NO_WRITE_ACCESS        = 6,
    HV_TRANSLATE_GVA_GPA_ILLEGAL_OVERLAY_ACCESS = 7,

    /*
     * Intercept for memory access by either
     *  - a higher VTL
     *  - a nested hypervisor (due to a violation of the nested page table)
     */
    HV_TRANSLATE_GVA_INTERCEPT                  = 8,

    HV_TRANSLATE_GVA_GPA_UNACCEPTED             = 9,
};

union hv_translate_gva_result {
    uint64_t as_uint64;
    struct {
        uint32_t result_code; /* enum hv_translate_hva_result_code */
        uint32_t cache_type:8;
        uint32_t overlay_page:1;
        uint32_t reserved:23;
    };
};

typedef struct hv_input_translate_virtual_address {
    uint64_t partition_id;
    uint32_t vp_index;
    uint32_t padding;
    uint64_t control_flags;
    uint64_t gva_page;
} hv_input_translate_virtual_address;

typedef struct hv_output_translate_virtual_address {
    union hv_translate_gva_result translation_result;
    uint64_t gpa_page;
} hv_output_translate_virtual_address;

typedef struct hv_register_x64_cpuid_result_parameters {
    struct {
        uint32_t eax;
        uint32_t ecx;
        uint8_t subleaf_specific;
        uint8_t always_override;
        uint16_t padding;
    } input;
    struct {
        uint32_t eax;
        uint32_t eax_mask;
        uint32_t ebx;
        uint32_t ebx_mask;
        uint32_t ecx;
        uint32_t ecx_mask;
        uint32_t edx;
        uint32_t edx_mask;
    } result;
} hv_register_x64_cpuid_result_parameters;

typedef struct hv_register_x64_msr_result_parameters {
    uint32_t msr_index;
    uint32_t access_type;
    uint32_t action; /* enum hv_unimplemented_msr_action */
} hv_register_x64_msr_result_parameters;

union hv_register_intercept_result_parameters {
    struct hv_register_x64_cpuid_result_parameters cpuid;
    struct hv_register_x64_msr_result_parameters msr;
};

typedef struct hv_input_register_intercept_result {
    uint64_t partition_id;
    uint32_t vp_index;
    uint32_t intercept_type; /* enum hv_intercept_type */
    union hv_register_intercept_result_parameters parameters;
} hv_input_register_intercept_result;

#endif /* HW_HYPERV_HVHDK_H */
