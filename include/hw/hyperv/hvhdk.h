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

struct hv_input_get_partition_property {
    uint64_t partition_id;
    uint32_t property_code; /* enum hv_partition_property_code */
    uint32_t padding;
} QEMU_PACKED;

struct hv_output_get_partition_property {
    uint64_t property_value;
} QEMU_PACKED;

struct hv_input_set_partition_property {
    uint64_t partition_id;
    uint32_t property_code; /* enum hv_partition_property_code */
    uint32_t padding;
    uint64_t property_value;
} QEMU_PACKED;

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

union hv_partition_processor_xsave_features {
    struct {
        uint64_t xsave_support:1;
        uint64_t xsaveopt_support:1;
        uint64_t avx_support:1;
        uint64_t avx2_support:1;
        uint64_t fma_support:1;
        uint64_t mpx_support:1;
        uint64_t avx512_support:1;
        uint64_t avx512_dq_support:1;
        uint64_t avx512_cd_support:1;
        uint64_t avx512_bw_support:1;
        uint64_t avx512_vl_support:1;
        uint64_t xsave_comp_support:1;
        uint64_t xsave_supervisor_support:1;
        uint64_t xcr1_support:1;
        uint64_t avx512_bitalg_support:1;
        uint64_t avx512_i_fma_support:1;
        uint64_t avx512_v_bmi_support:1;
        uint64_t avx512_v_bmi2_support:1;
        uint64_t avx512_vnni_support:1;
        uint64_t gfni_support:1;
        uint64_t vaes_support:1;
        uint64_t avx512_v_popcntdq_support:1;
        uint64_t vpclmulqdq_support:1;
        uint64_t avx512_bf16_support:1;
        uint64_t avx512_vp2_intersect_support:1;
        uint64_t avx512_fp16_support:1;
        uint64_t xfd_support:1;
        uint64_t amx_tile_support:1;
        uint64_t amx_bf16_support:1;
        uint64_t amx_int8_support:1;
        uint64_t avx_vnni_support:1;
        uint64_t avx_ifma_support:1;
        uint64_t avx_ne_convert_support:1;
        uint64_t avx_vnni_int8_support:1;
        uint64_t avx_vnni_int16_support:1;
        uint64_t avx10_1_256_support:1;
        uint64_t avx10_1_512_support:1;
        uint64_t amx_fp16_support:1;
        uint64_t reserved1:26;
    };
    uint64_t as_uint64;
};

#define HV_PARTITION_PROCESSOR_FEATURES_BANKS 2
#define HV_PARTITION_PROCESSOR_FEATURES_RESERVEDBANK1_BITFIELD_COUNT 4


union hv_partition_processor_features {
    uint64_t as_uint64[HV_PARTITION_PROCESSOR_FEATURES_BANKS];
    struct {
        uint64_t sse3_support:1;
        uint64_t lahf_sahf_support:1;
        uint64_t ssse3_support:1;
        uint64_t sse4_1_support:1;
        uint64_t sse4_2_support:1;
        uint64_t sse4a_support:1;
        uint64_t xop_support:1;
        uint64_t pop_cnt_support:1;
        uint64_t cmpxchg16b_support:1;
        uint64_t altmovcr8_support:1;
        uint64_t lzcnt_support:1;
        uint64_t mis_align_sse_support:1;
        uint64_t mmx_ext_support:1;
        uint64_t amd3dnow_support:1;
        uint64_t extended_amd3dnow_support:1;
        uint64_t page_1gb_support:1;
        uint64_t aes_support:1;
        uint64_t pclmulqdq_support:1;
        uint64_t pcid_support:1;
        uint64_t fma4_support:1;
        uint64_t f16c_support:1;
        uint64_t rd_rand_support:1;
        uint64_t rd_wr_fs_gs_support:1;
        uint64_t smep_support:1;
        uint64_t enhanced_fast_string_support:1;
        uint64_t bmi1_support:1;
        uint64_t bmi2_support:1;
        uint64_t hle_support_deprecated:1;
        uint64_t rtm_support_deprecated:1;
        uint64_t movbe_support:1;
        uint64_t npiep1_support:1;
        uint64_t dep_x87_fpu_save_support:1;
        uint64_t rd_seed_support:1;
        uint64_t adx_support:1;
        uint64_t intel_prefetch_support:1;
        uint64_t smap_support:1;
        uint64_t hle_support:1;
        uint64_t rtm_support:1;
        uint64_t rdtscp_support:1;
        uint64_t clflushopt_support:1;
        uint64_t clwb_support:1;
        uint64_t sha_support:1;
        uint64_t x87_pointers_saved_support:1;
        uint64_t invpcid_support:1;
        uint64_t ibrs_support:1;
        uint64_t stibp_support:1;
        uint64_t ibpb_support:1;
        uint64_t unrestricted_guest_support:1;
        uint64_t mdd_support:1;
        uint64_t fast_short_rep_mov_support:1;
        uint64_t l1dcache_flush_support:1;
        uint64_t rdcl_no_support:1;
        uint64_t ibrs_all_support:1;
        uint64_t skip_l1df_support:1;
        uint64_t ssb_no_support:1;
        uint64_t rsb_a_no_support:1;
        uint64_t virt_spec_ctrl_support:1;
        uint64_t rd_pid_support:1;
        uint64_t umip_support:1;
        uint64_t mbs_no_support:1;
        uint64_t mb_clear_support:1;
        uint64_t taa_no_support:1;
        uint64_t tsx_ctrl_support:1;
        uint64_t reserved_bank0:1;

        /* N.B. Begin bank 1 processor features. */
        uint64_t a_count_m_count_support:1;
        uint64_t tsc_invariant_support:1;
        uint64_t cl_zero_support:1;
        uint64_t rdpru_support:1;
        uint64_t la57_support:1;
        uint64_t mbec_support:1;
        uint64_t nested_virt_support:1;
        uint64_t psfd_support:1;
        uint64_t cet_ss_support:1;
        uint64_t cet_ibt_support:1;
        uint64_t vmx_exception_inject_support:1;
        uint64_t enqcmd_support:1;
        uint64_t umwait_tpause_support:1;
        uint64_t movdiri_support:1;
        uint64_t movdir64b_support:1;
        uint64_t cldemote_support:1;
        uint64_t serialize_support:1;
        uint64_t tsc_deadline_tmr_support:1;
        uint64_t tsc_adjust_support:1;
        uint64_t fzl_rep_movsb:1;
        uint64_t fs_rep_stosb:1;
        uint64_t fs_rep_cmpsb:1;
        uint64_t tsx_ld_trk_support:1;
        uint64_t vmx_ins_outs_exit_info_support:1;
        uint64_t hlat_support:1;
        uint64_t sbdr_ssdp_no_support:1;
        uint64_t fbsdp_no_support:1;
        uint64_t psdp_no_support:1;
        uint64_t fb_clear_support:1;
        uint64_t btc_no_support:1;
        uint64_t ibpb_rsb_flush_support:1;
        uint64_t stibp_always_on_support:1;
        uint64_t perf_global_ctrl_support:1;
        uint64_t npt_execute_only_support:1;
        uint64_t npt_ad_flags_support:1;
        uint64_t npt1_gb_page_support:1;
        uint64_t amd_processor_topology_node_id_support:1;
        uint64_t local_machine_check_support:1;
        uint64_t extended_topology_leaf_fp256_amd_support:1;
        uint64_t gds_no_support:1;
        uint64_t cmpccxadd_support:1;
        uint64_t tsc_aux_virtualization_support:1;
        uint64_t rmp_query_support:1;
        uint64_t bhi_no_support:1;
        uint64_t bhi_dis_support:1;
        uint64_t prefetch_i_support:1;
        uint64_t sha512_support:1;
        uint64_t mitigation_ctrl_support:1;
        uint64_t rfds_no_support:1;
        uint64_t rfds_clear_support:1;
        uint64_t sm3_support:1;
        uint64_t sm4_support:1;
        uint64_t secure_avic_support:1;
        uint64_t guest_intercept_ctrl_support:1;
        uint64_t sbpb_supported:1;
        uint64_t ibpb_br_type_supported:1;
        uint64_t srso_no_supported:1;
        uint64_t srso_user_kernel_no_supported:1;
        uint64_t vrew_clear_supported:1;
        uint64_t tsa_l1_no_supported:1;
        uint64_t tsa_sq_no_supported:1;
        uint64_t lass_support:1;
        uint64_t idle_hlt_intercept_support:1;
        uint64_t msr_list_support:1;
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
} QEMU_PACKED hv_input_translate_virtual_address;

typedef struct hv_output_translate_virtual_address {
    union hv_translate_gva_result translation_result;
    uint64_t gpa_page;
} QEMU_PACKED hv_output_translate_virtual_address;

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
} QEMU_PACKED hv_register_x64_cpuid_result_parameters;

typedef struct hv_register_x64_msr_result_parameters {
    uint32_t msr_index;
    uint32_t access_type;
    uint32_t action; /* enum hv_unimplemented_msr_action */
} QEMU_PACKED hv_register_x64_msr_result_parameters;

union hv_register_intercept_result_parameters {
    struct hv_register_x64_cpuid_result_parameters cpuid;
    struct hv_register_x64_msr_result_parameters msr;
};

typedef struct hv_input_register_intercept_result {
    uint64_t partition_id;
    uint32_t vp_index;
    uint32_t intercept_type; /* enum hv_intercept_type */
    union hv_register_intercept_result_parameters parameters;
} QEMU_PACKED hv_input_register_intercept_result;

#endif /* HW_HYPERV_HVHDK_H */
