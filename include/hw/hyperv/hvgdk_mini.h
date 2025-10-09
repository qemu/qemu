/*
 * Userspace interfaces for /dev/mshv* devices and derived fds
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_HYPERV_HVGDK_MINI_H
#define HW_HYPERV_HVGDK_MINI_H

#define MSHV_IOCTL  0xB8

typedef enum hv_register_name {
    /* Pending Interruption Register */
    HV_REGISTER_PENDING_INTERRUPTION = 0x00010002,

    /* X64 User-Mode Registers */
    HV_X64_REGISTER_RAX     = 0x00020000,
    HV_X64_REGISTER_RCX     = 0x00020001,
    HV_X64_REGISTER_RDX     = 0x00020002,
    HV_X64_REGISTER_RBX     = 0x00020003,
    HV_X64_REGISTER_RSP     = 0x00020004,
    HV_X64_REGISTER_RBP     = 0x00020005,
    HV_X64_REGISTER_RSI     = 0x00020006,
    HV_X64_REGISTER_RDI     = 0x00020007,
    HV_X64_REGISTER_R8      = 0x00020008,
    HV_X64_REGISTER_R9      = 0x00020009,
    HV_X64_REGISTER_R10     = 0x0002000A,
    HV_X64_REGISTER_R11     = 0x0002000B,
    HV_X64_REGISTER_R12     = 0x0002000C,
    HV_X64_REGISTER_R13     = 0x0002000D,
    HV_X64_REGISTER_R14     = 0x0002000E,
    HV_X64_REGISTER_R15     = 0x0002000F,
    HV_X64_REGISTER_RIP     = 0x00020010,
    HV_X64_REGISTER_RFLAGS  = 0x00020011,

    /* X64 Floating Point and Vector Registers */
    HV_X64_REGISTER_XMM0                = 0x00030000,
    HV_X64_REGISTER_XMM1                = 0x00030001,
    HV_X64_REGISTER_XMM2                = 0x00030002,
    HV_X64_REGISTER_XMM3                = 0x00030003,
    HV_X64_REGISTER_XMM4                = 0x00030004,
    HV_X64_REGISTER_XMM5                = 0x00030005,
    HV_X64_REGISTER_XMM6                = 0x00030006,
    HV_X64_REGISTER_XMM7                = 0x00030007,
    HV_X64_REGISTER_XMM8                = 0x00030008,
    HV_X64_REGISTER_XMM9                = 0x00030009,
    HV_X64_REGISTER_XMM10               = 0x0003000A,
    HV_X64_REGISTER_XMM11               = 0x0003000B,
    HV_X64_REGISTER_XMM12               = 0x0003000C,
    HV_X64_REGISTER_XMM13               = 0x0003000D,
    HV_X64_REGISTER_XMM14               = 0x0003000E,
    HV_X64_REGISTER_XMM15               = 0x0003000F,
    HV_X64_REGISTER_FP_MMX0             = 0x00030010,
    HV_X64_REGISTER_FP_MMX1             = 0x00030011,
    HV_X64_REGISTER_FP_MMX2             = 0x00030012,
    HV_X64_REGISTER_FP_MMX3             = 0x00030013,
    HV_X64_REGISTER_FP_MMX4             = 0x00030014,
    HV_X64_REGISTER_FP_MMX5             = 0x00030015,
    HV_X64_REGISTER_FP_MMX6             = 0x00030016,
    HV_X64_REGISTER_FP_MMX7             = 0x00030017,
    HV_X64_REGISTER_FP_CONTROL_STATUS   = 0x00030018,
    HV_X64_REGISTER_XMM_CONTROL_STATUS  = 0x00030019,

    /* X64 Control Registers */
    HV_X64_REGISTER_CR0     = 0x00040000,
    HV_X64_REGISTER_CR2     = 0x00040001,
    HV_X64_REGISTER_CR3     = 0x00040002,
    HV_X64_REGISTER_CR4     = 0x00040003,
    HV_X64_REGISTER_CR8     = 0x00040004,
    HV_X64_REGISTER_XFEM    = 0x00040005,

    /* X64 Segment Registers */
    HV_X64_REGISTER_ES      = 0x00060000,
    HV_X64_REGISTER_CS      = 0x00060001,
    HV_X64_REGISTER_SS      = 0x00060002,
    HV_X64_REGISTER_DS      = 0x00060003,
    HV_X64_REGISTER_FS      = 0x00060004,
    HV_X64_REGISTER_GS      = 0x00060005,
    HV_X64_REGISTER_LDTR    = 0x00060006,
    HV_X64_REGISTER_TR      = 0x00060007,

    /* X64 Table Registers */
    HV_X64_REGISTER_IDTR    = 0x00070000,
    HV_X64_REGISTER_GDTR    = 0x00070001,

    /* X64 Virtualized MSRs */
    HV_X64_REGISTER_TSC             = 0x00080000,
    HV_X64_REGISTER_EFER            = 0x00080001,
    HV_X64_REGISTER_KERNEL_GS_BASE  = 0x00080002,
    HV_X64_REGISTER_APIC_BASE       = 0x00080003,
    HV_X64_REGISTER_PAT             = 0x00080004,
    HV_X64_REGISTER_SYSENTER_CS     = 0x00080005,
    HV_X64_REGISTER_SYSENTER_EIP    = 0x00080006,
    HV_X64_REGISTER_SYSENTER_ESP    = 0x00080007,
    HV_X64_REGISTER_STAR            = 0x00080008,
    HV_X64_REGISTER_LSTAR           = 0x00080009,
    HV_X64_REGISTER_CSTAR           = 0x0008000A,
    HV_X64_REGISTER_SFMASK          = 0x0008000B,
    HV_X64_REGISTER_INITIAL_APIC_ID = 0x0008000C,

    /* X64 Cache control MSRs */
    HV_X64_REGISTER_MSR_MTRR_CAP            = 0x0008000D,
    HV_X64_REGISTER_MSR_MTRR_DEF_TYPE       = 0x0008000E,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE0     = 0x00080010,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE1     = 0x00080011,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE2     = 0x00080012,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE3     = 0x00080013,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE4     = 0x00080014,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE5     = 0x00080015,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE6     = 0x00080016,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE7     = 0x00080017,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE8     = 0x00080018,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASE9     = 0x00080019,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEA     = 0x0008001A,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEB     = 0x0008001B,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEC     = 0x0008001C,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASED     = 0x0008001D,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEE     = 0x0008001E,
    HV_X64_REGISTER_MSR_MTRR_PHYS_BASEF     = 0x0008001F,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK0     = 0x00080040,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK1     = 0x00080041,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK2     = 0x00080042,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK3     = 0x00080043,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK4     = 0x00080044,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK5     = 0x00080045,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK6     = 0x00080046,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK7     = 0x00080047,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK8     = 0x00080048,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASK9     = 0x00080049,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKA     = 0x0008004A,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKB     = 0x0008004B,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKC     = 0x0008004C,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKD     = 0x0008004D,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKE     = 0x0008004E,
    HV_X64_REGISTER_MSR_MTRR_PHYS_MASKF     = 0x0008004F,
    HV_X64_REGISTER_MSR_MTRR_FIX64K00000    = 0x00080070,
    HV_X64_REGISTER_MSR_MTRR_FIX16K80000    = 0x00080071,
    HV_X64_REGISTER_MSR_MTRR_FIX16KA0000    = 0x00080072,
    HV_X64_REGISTER_MSR_MTRR_FIX4KC0000     = 0x00080073,
    HV_X64_REGISTER_MSR_MTRR_FIX4KC8000     = 0x00080074,
    HV_X64_REGISTER_MSR_MTRR_FIX4KD0000     = 0x00080075,
    HV_X64_REGISTER_MSR_MTRR_FIX4KD8000     = 0x00080076,
    HV_X64_REGISTER_MSR_MTRR_FIX4KE0000     = 0x00080077,
    HV_X64_REGISTER_MSR_MTRR_FIX4KE8000     = 0x00080078,
    HV_X64_REGISTER_MSR_MTRR_FIX4KF0000     = 0x00080079,
    HV_X64_REGISTER_MSR_MTRR_FIX4KF8000     = 0x0008007A,

    HV_X64_REGISTER_TSC_AUX     = 0x0008007B,
    HV_X64_REGISTER_BNDCFGS     = 0x0008007C,
    HV_X64_REGISTER_DEBUG_CTL   = 0x0008007D,

    /* Available */

    HV_X64_REGISTER_SPEC_CTRL       = 0x00080084,
    HV_X64_REGISTER_TSC_ADJUST      = 0x00080096,

    /* Other MSRs */
    HV_X64_REGISTER_MSR_IA32_MISC_ENABLE = 0x000800A0,

    /* Misc */
    HV_REGISTER_GUEST_OS_ID         = 0x00090002,
    HV_REGISTER_REFERENCE_TSC       = 0x00090017,

    /* Hypervisor-defined Registers (Synic) */
    HV_REGISTER_SINT0       = 0x000A0000,
    HV_REGISTER_SINT1       = 0x000A0001,
    HV_REGISTER_SINT2       = 0x000A0002,
    HV_REGISTER_SINT3       = 0x000A0003,
    HV_REGISTER_SINT4       = 0x000A0004,
    HV_REGISTER_SINT5       = 0x000A0005,
    HV_REGISTER_SINT6       = 0x000A0006,
    HV_REGISTER_SINT7       = 0x000A0007,
    HV_REGISTER_SINT8       = 0x000A0008,
    HV_REGISTER_SINT9       = 0x000A0009,
    HV_REGISTER_SINT10      = 0x000A000A,
    HV_REGISTER_SINT11      = 0x000A000B,
    HV_REGISTER_SINT12      = 0x000A000C,
    HV_REGISTER_SINT13      = 0x000A000D,
    HV_REGISTER_SINT14      = 0x000A000E,
    HV_REGISTER_SINT15      = 0x000A000F,
    HV_REGISTER_SCONTROL    = 0x000A0010,
    HV_REGISTER_SVERSION    = 0x000A0011,
    HV_REGISTER_SIEFP       = 0x000A0012,
    HV_REGISTER_SIMP        = 0x000A0013,
    HV_REGISTER_EOM         = 0x000A0014,
    HV_REGISTER_SIRBP       = 0x000A0015,
} hv_register_name;

enum hv_intercept_type {
    HV_INTERCEPT_TYPE_X64_IO_PORT       = 0X00000000,
    HV_INTERCEPT_TYPE_X64_MSR           = 0X00000001,
    HV_INTERCEPT_TYPE_X64_CPUID         = 0X00000002,
    HV_INTERCEPT_TYPE_EXCEPTION         = 0X00000003,

    /* Used to be HV_INTERCEPT_TYPE_REGISTER */
    HV_INTERCEPT_TYPE_RESERVED0         = 0X00000004,
    HV_INTERCEPT_TYPE_MMIO              = 0X00000005,
    HV_INTERCEPT_TYPE_X64_GLOBAL_CPUID  = 0X00000006,
    HV_INTERCEPT_TYPE_X64_APIC_SMI      = 0X00000007,
    HV_INTERCEPT_TYPE_HYPERCALL         = 0X00000008,

    HV_INTERCEPT_TYPE_X64_APIC_INIT_SIPI        = 0X00000009,
    HV_INTERCEPT_MC_UPDATE_PATCH_LEVEL_MSR_READ = 0X0000000A,

    HV_INTERCEPT_TYPE_X64_APIC_WRITE        = 0X0000000B,
    HV_INTERCEPT_TYPE_X64_MSR_INDEX         = 0X0000000C,
    HV_INTERCEPT_TYPE_MAX,
    HV_INTERCEPT_TYPE_INVALID               = 0XFFFFFFFF,
};

struct hv_u128 {
    uint64_t low_part;
    uint64_t high_part;
};

union hv_x64_xmm_control_status_register {
    struct hv_u128 as_uint128;
    struct {
        union {
            /* long mode */
            uint64_t last_fp_rdp;
            /* 32 bit mode */
            struct {
                uint32_t last_fp_dp;
                uint16_t last_fp_ds;
                uint16_t padding;
            };
        };
        uint32_t xmm_status_control;
        uint32_t xmm_status_control_mask;
    };
};

union hv_x64_fp_register {
    struct hv_u128 as_uint128;
    struct {
        uint64_t mantissa;
        uint64_t biased_exponent:15;
        uint64_t sign:1;
        uint64_t reserved:48;
    };
};

union hv_x64_pending_exception_event {
    uint64_t as_uint64[2];
    struct {
        uint32_t event_pending:1;
        uint32_t event_type:3;
        uint32_t reserved0:4;
        uint32_t deliver_error_code:1;
        uint32_t reserved1:7;
        uint32_t vector:16;
        uint32_t error_code;
        uint64_t exception_parameter;
    };
};

union hv_x64_pending_virtualization_fault_event {
    uint64_t as_uint64[2];
    struct {
        uint32_t event_pending:1;
        uint32_t event_type:3;
        uint32_t reserved0:4;
        uint32_t reserved1:8;
        uint32_t parameter0:16;
        uint32_t code;
        uint64_t parameter1;
    };
};

union hv_x64_pending_interruption_register {
    uint64_t as_uint64;
    struct {
        uint32_t interruption_pending:1;
        uint32_t interruption_type:3;
        uint32_t deliver_error_code:1;
        uint32_t instruction_length:4;
        uint32_t nested_event:1;
        uint32_t reserved:6;
        uint32_t interruption_vector:16;
        uint32_t error_code;
    };
};

union hv_x64_register_sev_control {
    uint64_t as_uint64;
    struct {
        uint64_t enable_encrypted_state:1;
        uint64_t reserved_z:11;
        uint64_t vmsa_gpa_page_number:52;
    };
};

union hv_x64_msr_npiep_config_contents {
    uint64_t as_uint64;
    struct {
        /*
         * These bits enable instruction execution prevention for
         * specific instructions.
         */
        uint64_t prevents_gdt:1;
        uint64_t prevents_idt:1;
        uint64_t prevents_ldt:1;
        uint64_t prevents_tr:1;

        /* The reserved bits must always be 0. */
        uint64_t reserved:60;
    };
};

typedef struct hv_x64_segment_register {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    union {
        struct {
            uint16_t segment_type:4;
            uint16_t non_system_segment:1;
            uint16_t descriptor_privilege_level:2;
            uint16_t present:1;
            uint16_t reserved:4;
            uint16_t available:1;
            uint16_t _long:1;
            uint16_t _default:1;
            uint16_t granularity:1;
        };
        uint16_t attributes;
    };
} hv_x64_segment_register;

typedef struct hv_x64_table_register {
    uint16_t pad[3];
    uint16_t limit;
    uint64_t base;
} hv_x64_table_register;

union hv_x64_fp_control_status_register {
    struct hv_u128 as_uint128;
    struct {
        uint16_t fp_control;
        uint16_t fp_status;
        uint8_t fp_tag;
        uint8_t reserved;
        uint16_t last_fp_op;
        union {
            /* long mode */
            uint64_t last_fp_rip;
            /* 32 bit mode */
            struct {
                uint32_t last_fp_eip;
                uint16_t last_fp_cs;
                uint16_t padding;
            };
        };
    };
};

/* General Hypervisor Register Content Definitions */

union hv_explicit_suspend_register {
    uint64_t as_uint64;
    struct {
        uint64_t suspended:1;
        uint64_t reserved:63;
    };
};

union hv_internal_activity_register {
    uint64_t as_uint64;

    struct {
        uint64_t startup_suspend:1;
        uint64_t halt_suspend:1;
        uint64_t idle_suspend:1;
        uint64_t rsvd_z:61;
    };
};

union hv_x64_interrupt_state_register {
    uint64_t as_uint64;
    struct {
        uint64_t interrupt_shadow:1;
        uint64_t nmi_masked:1;
        uint64_t reserved:62;
    };
};

union hv_intercept_suspend_register {
    uint64_t as_uint64;
    struct {
        uint64_t suspended:1;
        uint64_t reserved:63;
    };
};

typedef union hv_register_value {
    struct hv_u128 reg128;
    uint64_t reg64;
    uint32_t reg32;
    uint16_t reg16;
    uint8_t reg8;
    union hv_x64_fp_register fp;
    union hv_x64_fp_control_status_register fp_control_status;
    union hv_x64_xmm_control_status_register xmm_control_status;
    struct hv_x64_segment_register segment;
    struct hv_x64_table_register table;
    union hv_explicit_suspend_register explicit_suspend;
    union hv_intercept_suspend_register intercept_suspend;
    union hv_internal_activity_register internal_activity;
    union hv_x64_interrupt_state_register interrupt_state;
    union hv_x64_pending_interruption_register pending_interruption;
    union hv_x64_msr_npiep_config_contents npiep_config;
    union hv_x64_pending_exception_event pending_exception_event;
    union hv_x64_pending_virtualization_fault_event
        pending_virtualization_fault_event;
    union hv_x64_register_sev_control sev_control;
} hv_register_value;

typedef struct hv_register_assoc {
    uint32_t name;         /* enum hv_register_name */
    uint32_t reserved1;
    uint64_t reserved2;
    union hv_register_value value;
} hv_register_assoc;

union hv_input_vtl {
    uint8_t as_uint8;
    struct {
        uint8_t target_vtl:4;
        uint8_t use_target_vtl:1;
        uint8_t reserved_z:3;
    };
};

typedef struct hv_input_get_vp_registers {
    uint64_t partition_id;
    uint32_t vp_index;
    union hv_input_vtl input_vtl;
    uint8_t  rsvd_z8;
    uint16_t rsvd_z16;
    uint32_t names[];
} hv_input_get_vp_registers;

typedef struct hv_input_set_vp_registers {
    uint64_t partition_id;
    uint32_t vp_index;
    union hv_input_vtl input_vtl;
    uint8_t  rsvd_z8;
    uint16_t rsvd_z16;
    struct hv_register_assoc elements[];
} hv_input_set_vp_registers;

#define MSHV_VP_MAX_REGISTERS   128

struct mshv_vp_registers {
    int count; /* at most MSHV_VP_MAX_REGISTERS */
    struct hv_register_assoc *regs;
};

union hv_interrupt_control {
    uint64_t as_uint64;
    struct {
        uint32_t interrupt_type; /* enum hv_interrupt type */
        uint32_t level_triggered:1;
        uint32_t logical_dest_mode:1;
        uint32_t rsvd:30;
    };
};

struct hv_input_assert_virtual_interrupt {
    uint64_t partition_id;
    union hv_interrupt_control control;
    uint64_t dest_addr; /* cpu's apic id */
    uint32_t vector;
    uint8_t target_vtl;
    uint8_t rsvd_z0;
    uint16_t rsvd_z1;
};

/* /dev/mshv */
#define MSHV_CREATE_PARTITION   _IOW(MSHV_IOCTL, 0x00, struct mshv_create_partition)
#define MSHV_CREATE_VP          _IOW(MSHV_IOCTL, 0x01, struct mshv_create_vp)

/* Partition fds created with MSHV_CREATE_PARTITION */
#define MSHV_INITIALIZE_PARTITION   _IO(MSHV_IOCTL, 0x00)
#define MSHV_SET_GUEST_MEMORY       _IOW(MSHV_IOCTL, 0x02, struct mshv_user_mem_region)
#define MSHV_IRQFD                  _IOW(MSHV_IOCTL, 0x03, struct mshv_user_irqfd)
#define MSHV_IOEVENTFD              _IOW(MSHV_IOCTL, 0x04, struct mshv_user_ioeventfd)
#define MSHV_SET_MSI_ROUTING        _IOW(MSHV_IOCTL, 0x05, struct mshv_user_irq_table)

/*
 ********************************
 * VP APIs for child partitions *
 ********************************
 */

struct hv_local_interrupt_controller_state {
    /* HV_X64_INTERRUPT_CONTROLLER_STATE */
    uint32_t apic_id;
    uint32_t apic_version;
    uint32_t apic_ldr;
    uint32_t apic_dfr;
    uint32_t apic_spurious;
    uint32_t apic_isr[8];
    uint32_t apic_tmr[8];
    uint32_t apic_irr[8];
    uint32_t apic_esr;
    uint32_t apic_icr_high;
    uint32_t apic_icr_low;
    uint32_t apic_lvt_timer;
    uint32_t apic_lvt_thermal;
    uint32_t apic_lvt_perfmon;
    uint32_t apic_lvt_lint0;
    uint32_t apic_lvt_lint1;
    uint32_t apic_lvt_error;
    uint32_t apic_lvt_cmci;
    uint32_t apic_error_status;
    uint32_t apic_initial_count;
    uint32_t apic_counter_value;
    uint32_t apic_divide_configuration;
    uint32_t apic_remote_read;
};

/* Generic hypercall */
#define MSHV_ROOT_HVCALL        _IOWR(MSHV_IOCTL, 0x07, struct mshv_root_hvcall)

/* From hvgdk_mini.h */

#define HV_X64_MSR_GUEST_OS_ID      0x40000000
#define HV_X64_MSR_SINT0            0x40000090
#define HV_X64_MSR_SINT1            0x40000091
#define HV_X64_MSR_SINT2            0x40000092
#define HV_X64_MSR_SINT3            0x40000093
#define HV_X64_MSR_SINT4            0x40000094
#define HV_X64_MSR_SINT5            0x40000095
#define HV_X64_MSR_SINT6            0x40000096
#define HV_X64_MSR_SINT7            0x40000097
#define HV_X64_MSR_SINT8            0x40000098
#define HV_X64_MSR_SINT9            0x40000099
#define HV_X64_MSR_SINT10           0x4000009A
#define HV_X64_MSR_SINT11           0x4000009B
#define HV_X64_MSR_SINT12           0x4000009C
#define HV_X64_MSR_SINT13           0x4000009D
#define HV_X64_MSR_SINT14           0x4000009E
#define HV_X64_MSR_SINT15           0x4000009F
#define HV_X64_MSR_SCONTROL         0x40000080
#define HV_X64_MSR_SIEFP            0x40000082
#define HV_X64_MSR_SIMP             0x40000083
#define HV_X64_MSR_REFERENCE_TSC    0x40000021
#define HV_X64_MSR_EOM              0x40000084

/* Define port identifier type. */
union hv_port_id {
    uint32_t asuint32_t;
    struct {
        uint32_t id:24;
        uint32_t reserved:8;
    };
};

#define HV_MESSAGE_SIZE                 (256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT   (240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT  (30)

/* Define hypervisor message types. */
enum hv_message_type {
    HVMSG_NONE                          = 0x00000000,

    /* Memory access messages. */
    HVMSG_UNMAPPED_GPA                  = 0x80000000,
    HVMSG_GPA_INTERCEPT                 = 0x80000001,
    HVMSG_UNACCEPTED_GPA                = 0x80000003,
    HVMSG_GPA_ATTRIBUTE_INTERCEPT       = 0x80000004,

    /* Timer notification messages. */
    HVMSG_TIMER_EXPIRED                 = 0x80000010,

    /* Error messages. */
    HVMSG_INVALID_VP_REGISTER_VALUE     = 0x80000020,
    HVMSG_UNRECOVERABLE_EXCEPTION       = 0x80000021,
    HVMSG_UNSUPPORTED_FEATURE           = 0x80000022,

    /*
     * Opaque intercept message. The original intercept message is only
     * accessible from the mapped intercept message page.
     */
    HVMSG_OPAQUE_INTERCEPT              = 0x8000003F,

    /* Trace buffer complete messages. */
    HVMSG_EVENTLOG_BUFFERCOMPLETE       = 0x80000040,

    /* Hypercall intercept */
    HVMSG_HYPERCALL_INTERCEPT           = 0x80000050,

    /* SynIC intercepts */
    HVMSG_SYNIC_EVENT_INTERCEPT         = 0x80000060,
    HVMSG_SYNIC_SINT_INTERCEPT          = 0x80000061,
    HVMSG_SYNIC_SINT_DELIVERABLE        = 0x80000062,

    /* Async call completion intercept */
    HVMSG_ASYNC_CALL_COMPLETION         = 0x80000070,

    /* Root scheduler messages */
    HVMSG_SCHEDULER_VP_SIGNAL_BITSE     = 0x80000100,
    HVMSG_SCHEDULER_VP_SIGNAL_PAIR      = 0x80000101,

    /* Platform-specific processor intercept messages. */
    HVMSG_X64_IO_PORT_INTERCEPT         = 0x80010000,
    HVMSG_X64_MSR_INTERCEPT             = 0x80010001,
    HVMSG_X64_CPUID_INTERCEPT           = 0x80010002,
    HVMSG_X64_EXCEPTION_INTERCEPT       = 0x80010003,
    HVMSG_X64_APIC_EOI                  = 0x80010004,
    HVMSG_X64_LEGACY_FP_ERROR           = 0x80010005,
    HVMSG_X64_IOMMU_PRQ                 = 0x80010006,
    HVMSG_X64_HALT                      = 0x80010007,
    HVMSG_X64_INTERRUPTION_DELIVERABLE  = 0x80010008,
    HVMSG_X64_SIPI_INTERCEPT            = 0x80010009,
    HVMSG_X64_SEV_VMGEXIT_INTERCEPT     = 0x80010013,
};

union hv_x64_vp_execution_state {
    uint16_t as_uint16;
    struct {
        uint16_t cpl:2;
        uint16_t cr0_pe:1;
        uint16_t cr0_am:1;
        uint16_t efer_lma:1;
        uint16_t debug_active:1;
        uint16_t interruption_pending:1;
        uint16_t vtl:4;
        uint16_t enclave_mode:1;
        uint16_t interrupt_shadow:1;
        uint16_t virtualization_fault_active:1;
        uint16_t reserved:2;
    };
};

/* From openvmm::hvdef */
enum hv_x64_intercept_access_type {
    HV_X64_INTERCEPT_ACCESS_TYPE_READ = 0,
    HV_X64_INTERCEPT_ACCESS_TYPE_WRITE = 1,
    HV_X64_INTERCEPT_ACCESS_TYPE_EXECUTE = 2,
};

struct hv_x64_intercept_message_header {
    uint32_t vp_index;
    uint8_t instruction_length:4;
    uint8_t cr8:4; /* Only set for exo partitions */
    uint8_t intercept_access_type;
    union hv_x64_vp_execution_state execution_state;
    struct hv_x64_segment_register cs_segment;
    uint64_t rip;
    uint64_t rflags;
};

union hv_x64_io_port_access_info {
    uint8_t as_uint8;
    struct {
        uint8_t access_size:3;
        uint8_t string_op:1;
        uint8_t rep_prefix:1;
        uint8_t reserved:3;
    };
};

typedef struct hv_x64_io_port_intercept_message {
    struct hv_x64_intercept_message_header header;
    uint16_t port_number;
    union hv_x64_io_port_access_info access_info;
    uint8_t instruction_byte_count;
    uint32_t reserved;
    uint64_t rax;
    uint8_t instruction_bytes[16];
    struct hv_x64_segment_register ds_segment;
    struct hv_x64_segment_register es_segment;
    uint64_t rcx;
    uint64_t rsi;
    uint64_t rdi;
} hv_x64_io_port_intercept_message;

union hv_x64_memory_access_info {
    uint8_t as_uint8;
    struct {
        uint8_t gva_valid:1;
        uint8_t gva_gpa_valid:1;
        uint8_t hypercall_output_pending:1;
        uint8_t tlb_locked_no_overlay:1;
        uint8_t reserved:4;
    };
};

struct hv_x64_memory_intercept_message {
    struct hv_x64_intercept_message_header header;
    uint32_t cache_type; /* enum hv_cache_type */
    uint8_t instruction_byte_count;
    union hv_x64_memory_access_info memory_access_info;
    uint8_t tpr_priority;
    uint8_t reserved1;
    uint64_t guest_virtual_address;
    uint64_t guest_physical_address;
    uint8_t instruction_bytes[16];
};

union hv_message_flags {
    uint8_t asu8;
    struct {
        uint8_t msg_pending:1;
        uint8_t reserved:7;
    };
};

struct hv_message_header {
    uint32_t message_type;
    uint8_t payload_size;
    union hv_message_flags message_flags;
    uint8_t reserved[2];
    union {
        uint64_t sender;
        union hv_port_id port;
    };
};

struct hv_message {
    struct hv_message_header header;
    union {
        uint64_t payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
    } u;
};

/* From  github.com/rust-vmm/mshv-bindings/src/x86_64/regs.rs */

struct hv_cpuid_entry {
    uint32_t function;
    uint32_t index;
    uint32_t flags;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t padding[3];
};

struct hv_cpuid {
    uint32_t nent;
    uint32_t padding;
    struct hv_cpuid_entry entries[0];
};

#define IA32_MSR_TSC            0x00000010
#define IA32_MSR_EFER           0xC0000080
#define IA32_MSR_KERNEL_GS_BASE 0xC0000102
#define IA32_MSR_APIC_BASE      0x0000001B
#define IA32_MSR_PAT            0x0277
#define IA32_MSR_SYSENTER_CS    0x00000174
#define IA32_MSR_SYSENTER_ESP   0x00000175
#define IA32_MSR_SYSENTER_EIP   0x00000176
#define IA32_MSR_STAR           0xC0000081
#define IA32_MSR_LSTAR          0xC0000082
#define IA32_MSR_CSTAR          0xC0000083
#define IA32_MSR_SFMASK         0xC0000084

#define IA32_MSR_MTRR_CAP       0x00FE
#define IA32_MSR_MTRR_DEF_TYPE  0x02FF
#define IA32_MSR_MTRR_PHYSBASE0 0x0200
#define IA32_MSR_MTRR_PHYSMASK0 0x0201
#define IA32_MSR_MTRR_PHYSBASE1 0x0202
#define IA32_MSR_MTRR_PHYSMASK1 0x0203
#define IA32_MSR_MTRR_PHYSBASE2 0x0204
#define IA32_MSR_MTRR_PHYSMASK2 0x0205
#define IA32_MSR_MTRR_PHYSBASE3 0x0206
#define IA32_MSR_MTRR_PHYSMASK3 0x0207
#define IA32_MSR_MTRR_PHYSBASE4 0x0208
#define IA32_MSR_MTRR_PHYSMASK4 0x0209
#define IA32_MSR_MTRR_PHYSBASE5 0x020A
#define IA32_MSR_MTRR_PHYSMASK5 0x020B
#define IA32_MSR_MTRR_PHYSBASE6 0x020C
#define IA32_MSR_MTRR_PHYSMASK6 0x020D
#define IA32_MSR_MTRR_PHYSBASE7 0x020E
#define IA32_MSR_MTRR_PHYSMASK7 0x020F

#define IA32_MSR_MTRR_FIX64K_00000 0x0250
#define IA32_MSR_MTRR_FIX16K_80000 0x0258
#define IA32_MSR_MTRR_FIX16K_A0000 0x0259
#define IA32_MSR_MTRR_FIX4K_C0000 0x0268
#define IA32_MSR_MTRR_FIX4K_C8000 0x0269
#define IA32_MSR_MTRR_FIX4K_D0000 0x026A
#define IA32_MSR_MTRR_FIX4K_D8000 0x026B
#define IA32_MSR_MTRR_FIX4K_E0000 0x026C
#define IA32_MSR_MTRR_FIX4K_E8000 0x026D
#define IA32_MSR_MTRR_FIX4K_F0000 0x026E
#define IA32_MSR_MTRR_FIX4K_F8000 0x026F

#define IA32_MSR_TSC_AUX          0xC0000103
#define IA32_MSR_BNDCFGS          0x00000d90
#define IA32_MSR_DEBUG_CTL        0x1D9
#define IA32_MSR_SPEC_CTRL        0x00000048
#define IA32_MSR_TSC_ADJUST       0x0000003b

#define IA32_MSR_MISC_ENABLE 0x000001a0

#define HV_TRANSLATE_GVA_VALIDATE_READ       (0x0001)
#define HV_TRANSLATE_GVA_VALIDATE_WRITE      (0x0002)
#define HV_TRANSLATE_GVA_VALIDATE_EXECUTE    (0x0004)

#define HV_HYP_PAGE_SHIFT       12
#define HV_HYP_PAGE_SIZE        BIT(HV_HYP_PAGE_SHIFT)
#define HV_HYP_PAGE_MASK        (~(HV_HYP_PAGE_SIZE - 1))

#define HVCALL_GET_PARTITION_PROPERTY    0x0044
#define HVCALL_SET_PARTITION_PROPERTY    0x0045
#define HVCALL_GET_VP_REGISTERS          0x0050
#define HVCALL_SET_VP_REGISTERS          0x0051
#define HVCALL_TRANSLATE_VIRTUAL_ADDRESS 0x0052
#define HVCALL_REGISTER_INTERCEPT_RESULT 0x0091
#define HVCALL_ASSERT_VIRTUAL_INTERRUPT  0x0094

#endif /* HW_HYPERV_HVGDK_MINI_H */
