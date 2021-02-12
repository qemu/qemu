#ifndef HW_SPAPR_H
#define HW_SPAPR_H

#include "qemu/units.h"
#include "sysemu/dma.h"
#include "hw/boards.h"
#include "hw/ppc/spapr_drc.h"
#include "hw/mem/pc-dimm.h"
#include "hw/ppc/spapr_ovec.h"
#include "hw/ppc/spapr_irq.h"
#include "qom/object.h"
#include "hw/ppc/spapr_xive.h"  /* For SpaprXive */
#include "hw/ppc/xics.h"        /* For ICSState */
#include "hw/ppc/spapr_tpm_proxy.h"

struct SpaprVioBus;
struct SpaprPhbState;
struct SpaprNvram;

typedef struct SpaprEventLogEntry SpaprEventLogEntry;
typedef struct SpaprEventSource SpaprEventSource;
typedef struct SpaprPendingHpt SpaprPendingHpt;

#define HPTE64_V_HPTE_DIRTY     0x0000000000000040ULL
#define SPAPR_ENTRY_POINT       0x100

#define SPAPR_TIMEBASE_FREQ     512000000ULL

#define TYPE_SPAPR_RTC "spapr-rtc"

OBJECT_DECLARE_SIMPLE_TYPE(SpaprRtcState, SPAPR_RTC)

struct SpaprRtcState {
    /*< private >*/
    DeviceState parent_obj;
    int64_t ns_offset;
};

typedef struct SpaprDimmState SpaprDimmState;

#define TYPE_SPAPR_MACHINE      "spapr-machine"
OBJECT_DECLARE_TYPE(SpaprMachineState, SpaprMachineClass, SPAPR_MACHINE)

typedef enum {
    SPAPR_RESIZE_HPT_DEFAULT = 0,
    SPAPR_RESIZE_HPT_DISABLED,
    SPAPR_RESIZE_HPT_ENABLED,
    SPAPR_RESIZE_HPT_REQUIRED,
} SpaprResizeHpt;

/**
 * Capabilities
 */

/* Hardware Transactional Memory */
#define SPAPR_CAP_HTM                   0x00
/* Vector Scalar Extensions */
#define SPAPR_CAP_VSX                   0x01
/* Decimal Floating Point */
#define SPAPR_CAP_DFP                   0x02
/* Cache Flush on Privilege Change */
#define SPAPR_CAP_CFPC                  0x03
/* Speculation Barrier Bounds Checking */
#define SPAPR_CAP_SBBC                  0x04
/* Indirect Branch Serialisation */
#define SPAPR_CAP_IBS                   0x05
/* HPT Maximum Page Size (encoded as a shift) */
#define SPAPR_CAP_HPT_MAXPAGESIZE       0x06
/* Nested KVM-HV */
#define SPAPR_CAP_NESTED_KVM_HV         0x07
/* Large Decrementer */
#define SPAPR_CAP_LARGE_DECREMENTER     0x08
/* Count Cache Flush Assist HW Instruction */
#define SPAPR_CAP_CCF_ASSIST            0x09
/* Implements PAPR FWNMI option */
#define SPAPR_CAP_FWNMI                 0x0A
/* Num Caps */
#define SPAPR_CAP_NUM                   (SPAPR_CAP_FWNMI + 1)

/*
 * Capability Values
 */
/* Bool Caps */
#define SPAPR_CAP_OFF                   0x00
#define SPAPR_CAP_ON                    0x01

/* Custom Caps */

/* Generic */
#define SPAPR_CAP_BROKEN                0x00
#define SPAPR_CAP_WORKAROUND            0x01
#define SPAPR_CAP_FIXED                 0x02
/* SPAPR_CAP_IBS (cap-ibs) */
#define SPAPR_CAP_FIXED_IBS             0x02
#define SPAPR_CAP_FIXED_CCD             0x03
#define SPAPR_CAP_FIXED_NA              0x10 /* Lets leave a bit of a gap... */

#define FDT_MAX_SIZE                    0x100000

/*
 * NUMA related macros. MAX_DISTANCE_REF_POINTS was taken
 * from Linux kernel arch/powerpc/mm/numa.h. It represents the
 * amount of associativity domains for non-CPU resources.
 *
 * NUMA_ASSOC_SIZE is the base array size of an ibm,associativity
 * array for any non-CPU resource.
 *
 * VCPU_ASSOC_SIZE represents the size of ibm,associativity array
 * for CPUs, which has an extra element (vcpu_id) in the end.
 */
#define MAX_DISTANCE_REF_POINTS    4
#define NUMA_ASSOC_SIZE            (MAX_DISTANCE_REF_POINTS + 1)
#define VCPU_ASSOC_SIZE            (NUMA_ASSOC_SIZE + 1)

/* Max number of these GPUsper a physical box */
#define NVGPU_MAX_NUM                6

typedef struct SpaprCapabilities SpaprCapabilities;
struct SpaprCapabilities {
    uint8_t caps[SPAPR_CAP_NUM];
};

/**
 * SpaprMachineClass:
 */
struct SpaprMachineClass {
    /*< private >*/
    MachineClass parent_class;

    /*< public >*/
    bool dr_lmb_enabled;       /* enable dynamic-reconfig/hotplug of LMBs */
    bool dr_phb_enabled;       /* enable dynamic-reconfig/hotplug of PHBs */
    bool update_dt_enabled;    /* enable KVMPPC_H_UPDATE_DT */
    bool use_ohci_by_default;  /* use USB-OHCI instead of XHCI */
    bool pre_2_10_has_unused_icps;
    bool legacy_irq_allocation;
    uint32_t nr_xirqs;
    bool broken_host_serial_model; /* present real host info to the guest */
    bool pre_4_1_migration; /* don't migrate hpt-max-page-size */
    bool linux_pci_probe;
    bool smp_threads_vsmt; /* set VSMT to smp_threads by default */
    hwaddr rma_limit;          /* clamp the RMA to this size */
    bool pre_5_1_assoc_refpoints;
    bool pre_5_2_numa_associativity;

    bool (*phb_placement)(SpaprMachineState *spapr, uint32_t index,
                          uint64_t *buid, hwaddr *pio,
                          hwaddr *mmio32, hwaddr *mmio64,
                          unsigned n_dma, uint32_t *liobns, hwaddr *nv2gpa,
                          hwaddr *nv2atsd, Error **errp);
    SpaprResizeHpt resize_hpt_default;
    SpaprCapabilities default_caps;
    SpaprIrq *irq;
};

/**
 * SpaprMachineState:
 */
struct SpaprMachineState {
    /*< private >*/
    MachineState parent_obj;

    struct SpaprVioBus *vio_bus;
    QLIST_HEAD(, SpaprPhbState) phbs;
    struct SpaprNvram *nvram;
    SpaprRtcState rtc;

    SpaprResizeHpt resize_hpt;
    void *htab;
    uint32_t htab_shift;
    uint64_t patb_entry; /* Process tbl registed in H_REGISTER_PROCESS_TABLE */
    SpaprPendingHpt *pending_hpt; /* in-progress resize */

    hwaddr rma_size;
    uint32_t fdt_size;
    uint32_t fdt_initial_size;
    void *fdt_blob;
    long kernel_size;
    bool kernel_le;
    uint64_t kernel_addr;
    uint32_t initrd_base;
    long initrd_size;
    uint64_t rtc_offset; /* Now used only during incoming migration */
    struct PPCTimebase tb;
    bool has_graphics;
    uint32_t vsmt;       /* Virtual SMT mode (KVM's "core stride") */

    Notifier epow_notifier;
    QTAILQ_HEAD(, SpaprEventLogEntry) pending_events;
    bool use_hotplug_event_source;
    SpaprEventSource *event_sources;

    /* ibm,client-architecture-support option negotiation */
    bool cas_pre_isa3_guest;
    SpaprOptionVector *ov5;         /* QEMU-supported option vectors */
    SpaprOptionVector *ov5_cas;     /* negotiated (via CAS) option vectors */
    uint32_t max_compat_pvr;

    /* Migration state */
    int htab_save_index;
    bool htab_first_pass;
    int htab_fd;

    /* Pending DIMM unplug cache. It is populated when a LMB
     * unplug starts. It can be regenerated if a migration
     * occurs during the unplug process. */
    QTAILQ_HEAD(, SpaprDimmState) pending_dimm_unplugs;

    /* State related to FWNMI option */

    /* System Reset and Machine Check Notification Routine addresses
     * registered by "ibm,nmi-register" RTAS call.
     */
    target_ulong fwnmi_system_reset_addr;
    target_ulong fwnmi_machine_check_addr;

    /* Machine Check FWNMI synchronization, fwnmi_machine_check_interlock is
     * set to -1 if a FWNMI machine check is not in progress, else is set to
     * the CPU that was delivered the machine check, and is set back to -1
     * when that CPU makes an "ibm,nmi-interlock" RTAS call. The cond is used
     * to synchronize other CPUs.
     */
    int fwnmi_machine_check_interlock;
    QemuCond fwnmi_machine_check_interlock_cond;

    /*< public >*/
    char *kvm_type;
    char *host_model;
    char *host_serial;

    int32_t irq_map_nr;
    unsigned long *irq_map;
    SpaprIrq *irq;
    qemu_irq *qirqs;
    SpaprInterruptController *active_intc;
    ICSState *ics;
    SpaprXive *xive;

    bool cmd_line_caps[SPAPR_CAP_NUM];
    SpaprCapabilities def, eff, mig;

    unsigned gpu_numa_id;
    SpaprTpmProxy *tpm_proxy;

    uint32_t numa_assoc_array[MAX_NODES + NVGPU_MAX_NUM][NUMA_ASSOC_SIZE];

    Error *fwnmi_migration_blocker;
};

#define H_SUCCESS         0
#define H_BUSY            1        /* Hardware busy -- retry later */
#define H_CLOSED          2        /* Resource closed */
#define H_NOT_AVAILABLE   3
#define H_CONSTRAINED     4        /* Resource request constrained to max allowed */
#define H_PARTIAL         5
#define H_IN_PROGRESS     14       /* Kind of like busy */
#define H_PAGE_REGISTERED 15
#define H_PARTIAL_STORE   16
#define H_PENDING         17       /* returned from H_POLL_PENDING */
#define H_CONTINUE        18       /* Returned from H_Join on success */
#define H_LONG_BUSY_START_RANGE         9900  /* Start of long busy range */
#define H_LONG_BUSY_ORDER_1_MSEC        9900  /* Long busy, hint that 1msec \
                                                 is a good time to retry */
#define H_LONG_BUSY_ORDER_10_MSEC       9901  /* Long busy, hint that 10msec \
                                                 is a good time to retry */
#define H_LONG_BUSY_ORDER_100_MSEC      9902  /* Long busy, hint that 100msec \
                                                 is a good time to retry */
#define H_LONG_BUSY_ORDER_1_SEC         9903  /* Long busy, hint that 1sec \
                                                 is a good time to retry */
#define H_LONG_BUSY_ORDER_10_SEC        9904  /* Long busy, hint that 10sec \
                                                 is a good time to retry */
#define H_LONG_BUSY_ORDER_100_SEC       9905  /* Long busy, hint that 100sec \
                                                 is a good time to retry */
#define H_LONG_BUSY_END_RANGE           9905  /* End of long busy range */
#define H_HARDWARE        -1       /* Hardware error */
#define H_FUNCTION        -2       /* Function not supported */
#define H_PRIVILEGE       -3       /* Caller not privileged */
#define H_PARAMETER       -4       /* Parameter invalid, out-of-range or conflicting */
#define H_BAD_MODE        -5       /* Illegal msr value */
#define H_PTEG_FULL       -6       /* PTEG is full */
#define H_NOT_FOUND       -7       /* PTE was not found" */
#define H_RESERVED_DABR   -8       /* DABR address is reserved by the hypervisor on this processor" */
#define H_NO_MEM          -9
#define H_AUTHORITY       -10
#define H_PERMISSION      -11
#define H_DROPPED         -12
#define H_SOURCE_PARM     -13
#define H_DEST_PARM       -14
#define H_REMOTE_PARM     -15
#define H_RESOURCE        -16
#define H_ADAPTER_PARM    -17
#define H_RH_PARM         -18
#define H_RCQ_PARM        -19
#define H_SCQ_PARM        -20
#define H_EQ_PARM         -21
#define H_RT_PARM         -22
#define H_ST_PARM         -23
#define H_SIGT_PARM       -24
#define H_TOKEN_PARM      -25
#define H_MLENGTH_PARM    -27
#define H_MEM_PARM        -28
#define H_MEM_ACCESS_PARM -29
#define H_ATTR_PARM       -30
#define H_PORT_PARM       -31
#define H_MCG_PARM        -32
#define H_VL_PARM         -33
#define H_TSIZE_PARM      -34
#define H_TRACE_PARM      -35

#define H_MASK_PARM       -37
#define H_MCG_FULL        -38
#define H_ALIAS_EXIST     -39
#define H_P_COUNTER       -40
#define H_TABLE_FULL      -41
#define H_ALT_TABLE       -42
#define H_MR_CONDITION    -43
#define H_NOT_ENOUGH_RESOURCES -44
#define H_R_STATE         -45
#define H_RESCINDEND      -46
#define H_P2              -55
#define H_P3              -56
#define H_P4              -57
#define H_P5              -58
#define H_P6              -59
#define H_P7              -60
#define H_P8              -61
#define H_P9              -62
#define H_OVERLAP         -68
#define H_UNSUPPORTED_FLAG -256
#define H_MULTI_THREADS_ACTIVE -9005


/* Long Busy is a condition that can be returned by the firmware
 * when a call cannot be completed now, but the identical call
 * should be retried later.  This prevents calls blocking in the
 * firmware for long periods of time.  Annoyingly the firmware can return
 * a range of return codes, hinting at how long we should wait before
 * retrying.  If you don't care for the hint, the macro below is a good
 * way to check for the long_busy return codes
 */
#define H_IS_LONG_BUSY(x)  ((x >= H_LONG_BUSY_START_RANGE) \
                            && (x <= H_LONG_BUSY_END_RANGE))

/* Flags */
#define H_LARGE_PAGE      (1ULL<<(63-16))
#define H_EXACT           (1ULL<<(63-24))       /* Use exact PTE or return H_PTEG_FULL */
#define H_R_XLATE         (1ULL<<(63-25))       /* include a valid logical page num in the pte if the valid bit is set */
#define H_READ_4          (1ULL<<(63-26))       /* Return 4 PTEs */
#define H_PAGE_STATE_CHANGE (1ULL<<(63-28))
#define H_PAGE_UNUSED     ((1ULL<<(63-29)) | (1ULL<<(63-30)))
#define H_PAGE_SET_UNUSED (H_PAGE_STATE_CHANGE | H_PAGE_UNUSED)
#define H_PAGE_SET_LOANED (H_PAGE_SET_UNUSED | (1ULL<<(63-31)))
#define H_PAGE_SET_ACTIVE H_PAGE_STATE_CHANGE
#define H_AVPN            (1ULL<<(63-32))       /* An avpn is provided as a sanity test */
#define H_ANDCOND         (1ULL<<(63-33))
#define H_ICACHE_INVALIDATE (1ULL<<(63-40))     /* icbi, etc.  (ignored for IO pages) */
#define H_ICACHE_SYNCHRONIZE (1ULL<<(63-41))    /* dcbst, icbi, etc (ignored for IO pages */
#define H_ZERO_PAGE       (1ULL<<(63-48))       /* zero the page before mapping (ignored for IO pages) */
#define H_COPY_PAGE       (1ULL<<(63-49))
#define H_N               (1ULL<<(63-61))
#define H_PP1             (1ULL<<(63-62))
#define H_PP2             (1ULL<<(63-63))

/* Values for 2nd argument to H_SET_MODE */
#define H_SET_MODE_RESOURCE_SET_CIABR           1
#define H_SET_MODE_RESOURCE_SET_DAWR            2
#define H_SET_MODE_RESOURCE_ADDR_TRANS_MODE     3
#define H_SET_MODE_RESOURCE_LE                  4

/* Flags for H_SET_MODE_RESOURCE_LE */
#define H_SET_MODE_ENDIAN_BIG    0
#define H_SET_MODE_ENDIAN_LITTLE 1

/* VASI States */
#define H_VASI_INVALID    0
#define H_VASI_ENABLED    1
#define H_VASI_ABORTED    2
#define H_VASI_SUSPENDING 3
#define H_VASI_SUSPENDED  4
#define H_VASI_RESUMED    5
#define H_VASI_COMPLETED  6

/* DABRX flags */
#define H_DABRX_HYPERVISOR (1ULL<<(63-61))
#define H_DABRX_KERNEL     (1ULL<<(63-62))
#define H_DABRX_USER       (1ULL<<(63-63))

/* Values for KVM_PPC_GET_CPU_CHAR & H_GET_CPU_CHARACTERISTICS */
#define H_CPU_CHAR_SPEC_BAR_ORI31               PPC_BIT(0)
#define H_CPU_CHAR_BCCTRL_SERIALISED            PPC_BIT(1)
#define H_CPU_CHAR_L1D_FLUSH_ORI30              PPC_BIT(2)
#define H_CPU_CHAR_L1D_FLUSH_TRIG2              PPC_BIT(3)
#define H_CPU_CHAR_L1D_THREAD_PRIV              PPC_BIT(4)
#define H_CPU_CHAR_HON_BRANCH_HINTS             PPC_BIT(5)
#define H_CPU_CHAR_THR_RECONF_TRIG              PPC_BIT(6)
#define H_CPU_CHAR_CACHE_COUNT_DIS              PPC_BIT(7)
#define H_CPU_CHAR_BCCTR_FLUSH_ASSIST           PPC_BIT(9)
#define H_CPU_BEHAV_FAVOUR_SECURITY             PPC_BIT(0)
#define H_CPU_BEHAV_L1D_FLUSH_PR                PPC_BIT(1)
#define H_CPU_BEHAV_BNDS_CHK_SPEC_BAR           PPC_BIT(2)
#define H_CPU_BEHAV_FLUSH_COUNT_CACHE           PPC_BIT(5)

/* Each control block has to be on a 4K boundary */
#define H_CB_ALIGNMENT     4096

/* pSeries hypervisor opcodes */
#define H_REMOVE                0x04
#define H_ENTER                 0x08
#define H_READ                  0x0c
#define H_CLEAR_MOD             0x10
#define H_CLEAR_REF             0x14
#define H_PROTECT               0x18
#define H_GET_TCE               0x1c
#define H_PUT_TCE               0x20
#define H_SET_SPRG0             0x24
#define H_SET_DABR              0x28
#define H_PAGE_INIT             0x2c
#define H_SET_ASR               0x30
#define H_ASR_ON                0x34
#define H_ASR_OFF               0x38
#define H_LOGICAL_CI_LOAD       0x3c
#define H_LOGICAL_CI_STORE      0x40
#define H_LOGICAL_CACHE_LOAD    0x44
#define H_LOGICAL_CACHE_STORE   0x48
#define H_LOGICAL_ICBI          0x4c
#define H_LOGICAL_DCBF          0x50
#define H_GET_TERM_CHAR         0x54
#define H_PUT_TERM_CHAR         0x58
#define H_REAL_TO_LOGICAL       0x5c
#define H_HYPERVISOR_DATA       0x60
#define H_EOI                   0x64
#define H_CPPR                  0x68
#define H_IPI                   0x6c
#define H_IPOLL                 0x70
#define H_XIRR                  0x74
#define H_PERFMON               0x7c
#define H_MIGRATE_DMA           0x78
#define H_REGISTER_VPA          0xDC
#define H_CEDE                  0xE0
#define H_CONFER                0xE4
#define H_PROD                  0xE8
#define H_GET_PPP               0xEC
#define H_SET_PPP               0xF0
#define H_PURR                  0xF4
#define H_PIC                   0xF8
#define H_REG_CRQ               0xFC
#define H_FREE_CRQ              0x100
#define H_VIO_SIGNAL            0x104
#define H_SEND_CRQ              0x108
#define H_COPY_RDMA             0x110
#define H_REGISTER_LOGICAL_LAN  0x114
#define H_FREE_LOGICAL_LAN      0x118
#define H_ADD_LOGICAL_LAN_BUFFER 0x11C
#define H_SEND_LOGICAL_LAN      0x120
#define H_BULK_REMOVE           0x124
#define H_MULTICAST_CTRL        0x130
#define H_SET_XDABR             0x134
#define H_STUFF_TCE             0x138
#define H_PUT_TCE_INDIRECT      0x13C
#define H_CHANGE_LOGICAL_LAN_MAC 0x14C
#define H_VTERM_PARTNER_INFO    0x150
#define H_REGISTER_VTERM        0x154
#define H_FREE_VTERM            0x158
#define H_RESET_EVENTS          0x15C
#define H_ALLOC_RESOURCE        0x160
#define H_FREE_RESOURCE         0x164
#define H_MODIFY_QP             0x168
#define H_QUERY_QP              0x16C
#define H_REREGISTER_PMR        0x170
#define H_REGISTER_SMR          0x174
#define H_QUERY_MR              0x178
#define H_QUERY_MW              0x17C
#define H_QUERY_HCA             0x180
#define H_QUERY_PORT            0x184
#define H_MODIFY_PORT           0x188
#define H_DEFINE_AQP1           0x18C
#define H_GET_TRACE_BUFFER      0x190
#define H_DEFINE_AQP0           0x194
#define H_RESIZE_MR             0x198
#define H_ATTACH_MCQP           0x19C
#define H_DETACH_MCQP           0x1A0
#define H_CREATE_RPT            0x1A4
#define H_REMOVE_RPT            0x1A8
#define H_REGISTER_RPAGES       0x1AC
#define H_DISABLE_AND_GETC      0x1B0
#define H_ERROR_DATA            0x1B4
#define H_GET_HCA_INFO          0x1B8
#define H_GET_PERF_COUNT        0x1BC
#define H_MANAGE_TRACE          0x1C0
#define H_GET_CPU_CHARACTERISTICS 0x1C8
#define H_FREE_LOGICAL_LAN_BUFFER 0x1D4
#define H_QUERY_INT_STATE       0x1E4
#define H_POLL_PENDING          0x1D8
#define H_ILLAN_ATTRIBUTES      0x244
#define H_MODIFY_HEA_QP         0x250
#define H_QUERY_HEA_QP          0x254
#define H_QUERY_HEA             0x258
#define H_QUERY_HEA_PORT        0x25C
#define H_MODIFY_HEA_PORT       0x260
#define H_REG_BCMC              0x264
#define H_DEREG_BCMC            0x268
#define H_REGISTER_HEA_RPAGES   0x26C
#define H_DISABLE_AND_GET_HEA   0x270
#define H_GET_HEA_INFO          0x274
#define H_ALLOC_HEA_RESOURCE    0x278
#define H_ADD_CONN              0x284
#define H_DEL_CONN              0x288
#define H_JOIN                  0x298
#define H_VASI_STATE            0x2A4
#define H_ENABLE_CRQ            0x2B0
#define H_GET_EM_PARMS          0x2B8
#define H_SET_MPP               0x2D0
#define H_GET_MPP               0x2D4
#define H_HOME_NODE_ASSOCIATIVITY 0x2EC
#define H_XIRR_X                0x2FC
#define H_RANDOM                0x300
#define H_SET_MODE              0x31C
#define H_RESIZE_HPT_PREPARE    0x36C
#define H_RESIZE_HPT_COMMIT     0x370
#define H_CLEAN_SLB             0x374
#define H_INVALIDATE_PID        0x378
#define H_REGISTER_PROC_TBL     0x37C
#define H_SIGNAL_SYS_RESET      0x380

#define H_INT_GET_SOURCE_INFO   0x3A8
#define H_INT_SET_SOURCE_CONFIG 0x3AC
#define H_INT_GET_SOURCE_CONFIG 0x3B0
#define H_INT_GET_QUEUE_INFO    0x3B4
#define H_INT_SET_QUEUE_CONFIG  0x3B8
#define H_INT_GET_QUEUE_CONFIG  0x3BC
#define H_INT_SET_OS_REPORTING_LINE 0x3C0
#define H_INT_GET_OS_REPORTING_LINE 0x3C4
#define H_INT_ESB               0x3C8
#define H_INT_SYNC              0x3CC
#define H_INT_RESET             0x3D0
#define H_SCM_READ_METADATA     0x3E4
#define H_SCM_WRITE_METADATA    0x3E8
#define H_SCM_BIND_MEM          0x3EC
#define H_SCM_UNBIND_MEM        0x3F0
#define H_SCM_UNBIND_ALL        0x3FC

#define MAX_HCALL_OPCODE        H_SCM_UNBIND_ALL

/* The hcalls above are standardized in PAPR and implemented by pHyp
 * as well.
 *
 * We also need some hcalls which are specific to qemu / KVM-on-POWER.
 * We put those into the 0xf000-0xfffc range which is reserved by PAPR
 * for "platform-specific" hcalls.
 */
#define KVMPPC_HCALL_BASE       0xf000
#define KVMPPC_H_RTAS           (KVMPPC_HCALL_BASE + 0x0)
#define KVMPPC_H_LOGICAL_MEMOP  (KVMPPC_HCALL_BASE + 0x1)
/* Client Architecture support */
#define KVMPPC_H_CAS            (KVMPPC_HCALL_BASE + 0x2)
#define KVMPPC_H_UPDATE_DT      (KVMPPC_HCALL_BASE + 0x3)
#define KVMPPC_HCALL_MAX        KVMPPC_H_UPDATE_DT

/*
 * The hcall range 0xEF00 to 0xEF80 is reserved for use in facilitating
 * Secure VM mode via an Ultravisor / Protected Execution Facility
 */
#define SVM_HCALL_BASE              0xEF00
#define SVM_H_TPM_COMM              0xEF10
#define SVM_HCALL_MAX               SVM_H_TPM_COMM


typedef struct SpaprDeviceTreeUpdateHeader {
    uint32_t version_id;
} SpaprDeviceTreeUpdateHeader;

#define hcall_dprintf(fmt, ...) \
    do { \
        qemu_log_mask(LOG_GUEST_ERROR, "%s: " fmt, __func__, ## __VA_ARGS__); \
    } while (0)

typedef target_ulong (*spapr_hcall_fn)(PowerPCCPU *cpu, SpaprMachineState *sm,
                                       target_ulong opcode,
                                       target_ulong *args);

void spapr_register_hypercall(target_ulong opcode, spapr_hcall_fn fn);
target_ulong spapr_hypercall(PowerPCCPU *cpu, target_ulong opcode,
                             target_ulong *args);

/* Virtual Processor Area structure constants */
#define VPA_MIN_SIZE           640
#define VPA_SIZE_OFFSET        0x4
#define VPA_SHARED_PROC_OFFSET 0x9
#define VPA_SHARED_PROC_VAL    0x2
#define VPA_DISPATCH_COUNTER   0x100

/* ibm,set-eeh-option */
#define RTAS_EEH_DISABLE                 0
#define RTAS_EEH_ENABLE                  1
#define RTAS_EEH_THAW_IO                 2
#define RTAS_EEH_THAW_DMA                3

/* ibm,get-config-addr-info2 */
#define RTAS_GET_PE_ADDR                 0
#define RTAS_GET_PE_MODE                 1
#define RTAS_PE_MODE_NONE                0
#define RTAS_PE_MODE_NOT_SHARED          1
#define RTAS_PE_MODE_SHARED              2

/* ibm,read-slot-reset-state2 */
#define RTAS_EEH_PE_STATE_NORMAL         0
#define RTAS_EEH_PE_STATE_RESET          1
#define RTAS_EEH_PE_STATE_STOPPED_IO_DMA 2
#define RTAS_EEH_PE_STATE_STOPPED_DMA    4
#define RTAS_EEH_PE_STATE_UNAVAIL        5
#define RTAS_EEH_NOT_SUPPORT             0
#define RTAS_EEH_SUPPORT                 1
#define RTAS_EEH_PE_UNAVAIL_INFO         1000
#define RTAS_EEH_PE_RECOVER_INFO         0

/* ibm,set-slot-reset */
#define RTAS_SLOT_RESET_DEACTIVATE       0
#define RTAS_SLOT_RESET_HOT              1
#define RTAS_SLOT_RESET_FUNDAMENTAL      3

/* ibm,slot-error-detail */
#define RTAS_SLOT_TEMP_ERR_LOG           1
#define RTAS_SLOT_PERM_ERR_LOG           2

/* RTAS return codes */
#define RTAS_OUT_SUCCESS                        0
#define RTAS_OUT_NO_ERRORS_FOUND                1
#define RTAS_OUT_HW_ERROR                       -1
#define RTAS_OUT_BUSY                           -2
#define RTAS_OUT_PARAM_ERROR                    -3
#define RTAS_OUT_NOT_SUPPORTED                  -3
#define RTAS_OUT_NO_SUCH_INDICATOR              -3
#define RTAS_OUT_NOT_AUTHORIZED                 -9002
#define RTAS_OUT_SYSPARM_PARAM_ERROR            -9999

/* DDW pagesize mask values from ibm,query-pe-dma-window */
#define RTAS_DDW_PGSIZE_4K       0x01
#define RTAS_DDW_PGSIZE_64K      0x02
#define RTAS_DDW_PGSIZE_16M      0x04
#define RTAS_DDW_PGSIZE_32M      0x08
#define RTAS_DDW_PGSIZE_64M      0x10
#define RTAS_DDW_PGSIZE_128M     0x20
#define RTAS_DDW_PGSIZE_256M     0x40
#define RTAS_DDW_PGSIZE_16G      0x80

/* RTAS tokens */
#define RTAS_TOKEN_BASE      0x2000

#define RTAS_DISPLAY_CHARACTER                  (RTAS_TOKEN_BASE + 0x00)
#define RTAS_GET_TIME_OF_DAY                    (RTAS_TOKEN_BASE + 0x01)
#define RTAS_SET_TIME_OF_DAY                    (RTAS_TOKEN_BASE + 0x02)
#define RTAS_POWER_OFF                          (RTAS_TOKEN_BASE + 0x03)
#define RTAS_SYSTEM_REBOOT                      (RTAS_TOKEN_BASE + 0x04)
#define RTAS_QUERY_CPU_STOPPED_STATE            (RTAS_TOKEN_BASE + 0x05)
#define RTAS_START_CPU                          (RTAS_TOKEN_BASE + 0x06)
#define RTAS_STOP_SELF                          (RTAS_TOKEN_BASE + 0x07)
#define RTAS_IBM_GET_SYSTEM_PARAMETER           (RTAS_TOKEN_BASE + 0x08)
#define RTAS_IBM_SET_SYSTEM_PARAMETER           (RTAS_TOKEN_BASE + 0x09)
#define RTAS_IBM_SET_XIVE                       (RTAS_TOKEN_BASE + 0x0A)
#define RTAS_IBM_GET_XIVE                       (RTAS_TOKEN_BASE + 0x0B)
#define RTAS_IBM_INT_OFF                        (RTAS_TOKEN_BASE + 0x0C)
#define RTAS_IBM_INT_ON                         (RTAS_TOKEN_BASE + 0x0D)
#define RTAS_CHECK_EXCEPTION                    (RTAS_TOKEN_BASE + 0x0E)
#define RTAS_EVENT_SCAN                         (RTAS_TOKEN_BASE + 0x0F)
#define RTAS_IBM_SET_TCE_BYPASS                 (RTAS_TOKEN_BASE + 0x10)
#define RTAS_QUIESCE                            (RTAS_TOKEN_BASE + 0x11)
#define RTAS_NVRAM_FETCH                        (RTAS_TOKEN_BASE + 0x12)
#define RTAS_NVRAM_STORE                        (RTAS_TOKEN_BASE + 0x13)
#define RTAS_READ_PCI_CONFIG                    (RTAS_TOKEN_BASE + 0x14)
#define RTAS_WRITE_PCI_CONFIG                   (RTAS_TOKEN_BASE + 0x15)
#define RTAS_IBM_READ_PCI_CONFIG                (RTAS_TOKEN_BASE + 0x16)
#define RTAS_IBM_WRITE_PCI_CONFIG               (RTAS_TOKEN_BASE + 0x17)
#define RTAS_IBM_QUERY_INTERRUPT_SOURCE_NUMBER  (RTAS_TOKEN_BASE + 0x18)
#define RTAS_IBM_CHANGE_MSI                     (RTAS_TOKEN_BASE + 0x19)
#define RTAS_SET_INDICATOR                      (RTAS_TOKEN_BASE + 0x1A)
#define RTAS_SET_POWER_LEVEL                    (RTAS_TOKEN_BASE + 0x1B)
#define RTAS_GET_POWER_LEVEL                    (RTAS_TOKEN_BASE + 0x1C)
#define RTAS_GET_SENSOR_STATE                   (RTAS_TOKEN_BASE + 0x1D)
#define RTAS_IBM_CONFIGURE_CONNECTOR            (RTAS_TOKEN_BASE + 0x1E)
#define RTAS_IBM_OS_TERM                        (RTAS_TOKEN_BASE + 0x1F)
#define RTAS_IBM_SET_EEH_OPTION                 (RTAS_TOKEN_BASE + 0x20)
#define RTAS_IBM_GET_CONFIG_ADDR_INFO2          (RTAS_TOKEN_BASE + 0x21)
#define RTAS_IBM_READ_SLOT_RESET_STATE2         (RTAS_TOKEN_BASE + 0x22)
#define RTAS_IBM_SET_SLOT_RESET                 (RTAS_TOKEN_BASE + 0x23)
#define RTAS_IBM_CONFIGURE_PE                   (RTAS_TOKEN_BASE + 0x24)
#define RTAS_IBM_SLOT_ERROR_DETAIL              (RTAS_TOKEN_BASE + 0x25)
#define RTAS_IBM_QUERY_PE_DMA_WINDOW            (RTAS_TOKEN_BASE + 0x26)
#define RTAS_IBM_CREATE_PE_DMA_WINDOW           (RTAS_TOKEN_BASE + 0x27)
#define RTAS_IBM_REMOVE_PE_DMA_WINDOW           (RTAS_TOKEN_BASE + 0x28)
#define RTAS_IBM_RESET_PE_DMA_WINDOW            (RTAS_TOKEN_BASE + 0x29)
#define RTAS_IBM_SUSPEND_ME                     (RTAS_TOKEN_BASE + 0x2A)
#define RTAS_IBM_NMI_REGISTER                   (RTAS_TOKEN_BASE + 0x2B)
#define RTAS_IBM_NMI_INTERLOCK                  (RTAS_TOKEN_BASE + 0x2C)

#define RTAS_TOKEN_MAX                          (RTAS_TOKEN_BASE + 0x2D)

/* RTAS ibm,get-system-parameter token values */
#define RTAS_SYSPARM_SPLPAR_CHARACTERISTICS      20
#define RTAS_SYSPARM_DIAGNOSTICS_RUN_MODE        42
#define RTAS_SYSPARM_UUID                        48

/* RTAS indicator/sensor types
 *
 * as defined by PAPR+ 2.7 7.3.5.4, Table 41
 *
 * NOTE: currently only DR-related sensors are implemented here
 */
#define RTAS_SENSOR_TYPE_ISOLATION_STATE        9001
#define RTAS_SENSOR_TYPE_DR                     9002
#define RTAS_SENSOR_TYPE_ALLOCATION_STATE       9003
#define RTAS_SENSOR_TYPE_ENTITY_SENSE RTAS_SENSOR_TYPE_ALLOCATION_STATE

/* Possible values for the platform-processor-diagnostics-run-mode parameter
 * of the RTAS ibm,get-system-parameter call.
 */
#define DIAGNOSTICS_RUN_MODE_DISABLED  0
#define DIAGNOSTICS_RUN_MODE_STAGGERED 1
#define DIAGNOSTICS_RUN_MODE_IMMEDIATE 2
#define DIAGNOSTICS_RUN_MODE_PERIODIC  3

static inline uint64_t ppc64_phys_to_real(uint64_t addr)
{
    return addr & ~0xF000000000000000ULL;
}

static inline uint32_t rtas_ld(target_ulong phys, int n)
{
    return ldl_be_phys(&address_space_memory, ppc64_phys_to_real(phys + 4*n));
}

static inline uint64_t rtas_ldq(target_ulong phys, int n)
{
    return (uint64_t)rtas_ld(phys, n) << 32 | rtas_ld(phys, n + 1);
}

static inline void rtas_st(target_ulong phys, int n, uint32_t val)
{
    stl_be_phys(&address_space_memory, ppc64_phys_to_real(phys + 4*n), val);
}

typedef void (*spapr_rtas_fn)(PowerPCCPU *cpu, SpaprMachineState *sm,
                              uint32_t token,
                              uint32_t nargs, target_ulong args,
                              uint32_t nret, target_ulong rets);
void spapr_rtas_register(int token, const char *name, spapr_rtas_fn fn);
target_ulong spapr_rtas_call(PowerPCCPU *cpu, SpaprMachineState *sm,
                             uint32_t token, uint32_t nargs, target_ulong args,
                             uint32_t nret, target_ulong rets);
void spapr_dt_rtas_tokens(void *fdt, int rtas);
void spapr_load_rtas(SpaprMachineState *spapr, void *fdt, hwaddr addr);

#define SPAPR_TCE_PAGE_SHIFT   12
#define SPAPR_TCE_PAGE_SIZE    (1ULL << SPAPR_TCE_PAGE_SHIFT)
#define SPAPR_TCE_PAGE_MASK    (SPAPR_TCE_PAGE_SIZE - 1)

#define SPAPR_VIO_BASE_LIOBN    0x00000000
#define SPAPR_VIO_LIOBN(reg)    (0x00000000 | (reg))
#define SPAPR_PCI_LIOBN(phb_index, window_num) \
    (0x80000000 | ((phb_index) << 8) | (window_num))
#define SPAPR_IS_PCI_LIOBN(liobn)   (!!((liobn) & 0x80000000))
#define SPAPR_PCI_DMA_WINDOW_NUM(liobn) ((liobn) & 0xff)

#define RTAS_SIZE               2048
#define RTAS_ERROR_LOG_MAX      2048

/* Offset from rtas-base where error log is placed */
#define RTAS_ERROR_LOG_OFFSET       0x30

#define RTAS_EVENT_SCAN_RATE    1

/* This helper should be used to encode interrupt specifiers when the related
 * "interrupt-controller" node has its "#interrupt-cells" property set to 2 (ie,
 * VIO devices, RTAS event sources and PHBs).
 */
static inline void spapr_dt_irq(uint32_t *intspec, int irq, bool is_lsi)
{
    intspec[0] = cpu_to_be32(irq);
    intspec[1] = is_lsi ? cpu_to_be32(1) : 0;
}


#define TYPE_SPAPR_TCE_TABLE "spapr-tce-table"
OBJECT_DECLARE_SIMPLE_TYPE(SpaprTceTable, SPAPR_TCE_TABLE)

#define TYPE_SPAPR_IOMMU_MEMORY_REGION "spapr-iommu-memory-region"
DECLARE_INSTANCE_CHECKER(IOMMUMemoryRegion, SPAPR_IOMMU_MEMORY_REGION,
                         TYPE_SPAPR_IOMMU_MEMORY_REGION)

struct SpaprTceTable {
    DeviceState parent;
    uint32_t liobn;
    uint32_t nb_table;
    uint64_t bus_offset;
    uint32_t page_shift;
    uint64_t *table;
    uint32_t mig_nb_table;
    uint64_t *mig_table;
    bool bypass;
    bool need_vfio;
    bool skipping_replay;
    int fd;
    MemoryRegion root;
    IOMMUMemoryRegion iommu;
    struct SpaprVioDevice *vdev; /* for @bypass migration compatibility only */
    QLIST_ENTRY(SpaprTceTable) list;
};

SpaprTceTable *spapr_tce_find_by_liobn(target_ulong liobn);

struct SpaprEventLogEntry {
    uint32_t summary;
    uint32_t extended_length;
    void *extended_log;
    QTAILQ_ENTRY(SpaprEventLogEntry) next;
};

void *spapr_build_fdt(SpaprMachineState *spapr, bool reset, size_t space);
void spapr_events_init(SpaprMachineState *sm);
void spapr_dt_events(SpaprMachineState *sm, void *fdt);
void close_htab_fd(SpaprMachineState *spapr);
void spapr_setup_hpt(SpaprMachineState *spapr);
void spapr_free_hpt(SpaprMachineState *spapr);
SpaprTceTable *spapr_tce_new_table(DeviceState *owner, uint32_t liobn);
void spapr_tce_table_enable(SpaprTceTable *tcet,
                            uint32_t page_shift, uint64_t bus_offset,
                            uint32_t nb_table);
void spapr_tce_table_disable(SpaprTceTable *tcet);
void spapr_tce_set_need_vfio(SpaprTceTable *tcet, bool need_vfio);

MemoryRegion *spapr_tce_get_iommu(SpaprTceTable *tcet);
int spapr_dma_dt(void *fdt, int node_off, const char *propname,
                 uint32_t liobn, uint64_t window, uint32_t size);
int spapr_tcet_dma_dt(void *fdt, int node_off, const char *propname,
                      SpaprTceTable *tcet);
void spapr_pci_switch_vga(SpaprMachineState *spapr, bool big_endian);
void spapr_hotplug_req_add_by_index(SpaprDrc *drc);
void spapr_hotplug_req_remove_by_index(SpaprDrc *drc);
void spapr_hotplug_req_add_by_count(SpaprDrcType drc_type,
                                       uint32_t count);
void spapr_hotplug_req_remove_by_count(SpaprDrcType drc_type,
                                          uint32_t count);
void spapr_hotplug_req_add_by_count_indexed(SpaprDrcType drc_type,
                                            uint32_t count, uint32_t index);
void spapr_hotplug_req_remove_by_count_indexed(SpaprDrcType drc_type,
                                               uint32_t count, uint32_t index);
int spapr_hpt_shift_for_ramsize(uint64_t ramsize);
int spapr_reallocate_hpt(SpaprMachineState *spapr, int shift, Error **errp);
void spapr_clear_pending_events(SpaprMachineState *spapr);
void spapr_clear_pending_hotplug_events(SpaprMachineState *spapr);
int spapr_max_server_number(SpaprMachineState *spapr);
void spapr_store_hpte(PowerPCCPU *cpu, hwaddr ptex,
                      uint64_t pte0, uint64_t pte1);
void spapr_mce_req_event(PowerPCCPU *cpu, bool recovered);

/* DRC callbacks. */
void spapr_core_release(DeviceState *dev);
int spapr_core_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                           void *fdt, int *fdt_start_offset, Error **errp);
void spapr_lmb_release(DeviceState *dev);
int spapr_lmb_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                          void *fdt, int *fdt_start_offset, Error **errp);
void spapr_phb_release(DeviceState *dev);
int spapr_phb_dt_populate(SpaprDrc *drc, SpaprMachineState *spapr,
                          void *fdt, int *fdt_start_offset, Error **errp);

void spapr_rtc_read(SpaprRtcState *rtc, struct tm *tm, uint32_t *ns);
int spapr_rtc_import_offset(SpaprRtcState *rtc, int64_t legacy_offset);

#define TYPE_SPAPR_RNG "spapr-rng"

#define SPAPR_MEMORY_BLOCK_SIZE ((hwaddr)1 << 28) /* 256MB */

/*
 * This defines the maximum number of DIMM slots we can have for sPAPR
 * guest. This is not defined by sPAPR but we are defining it to 32 slots
 * based on default number of slots provided by PowerPC kernel.
 */
#define SPAPR_MAX_RAM_SLOTS     32

/* 1GB alignment for hotplug memory region */
#define SPAPR_DEVICE_MEM_ALIGN (1 * GiB)

/*
 * Number of 32 bit words in each LMB list entry in ibm,dynamic-memory
 * property under ibm,dynamic-reconfiguration-memory node.
 */
#define SPAPR_DR_LMB_LIST_ENTRY_SIZE 6

/*
 * Defines for flag value in ibm,dynamic-memory property under
 * ibm,dynamic-reconfiguration-memory node.
 */
#define SPAPR_LMB_FLAGS_ASSIGNED 0x00000008
#define SPAPR_LMB_FLAGS_DRC_INVALID 0x00000020
#define SPAPR_LMB_FLAGS_RESERVED 0x00000080
#define SPAPR_LMB_FLAGS_HOTREMOVABLE 0x00000100

void spapr_do_system_reset_on_cpu(CPUState *cs, run_on_cpu_data arg);

#define HTAB_SIZE(spapr)        (1ULL << ((spapr)->htab_shift))

int spapr_get_vcpu_id(PowerPCCPU *cpu);
bool spapr_set_vcpu_id(PowerPCCPU *cpu, int cpu_index, Error **errp);
PowerPCCPU *spapr_find_cpu(int vcpu_id);

int spapr_caps_pre_load(void *opaque);
int spapr_caps_pre_save(void *opaque);

/*
 * Handling of optional capabilities
 */
extern const VMStateDescription vmstate_spapr_cap_htm;
extern const VMStateDescription vmstate_spapr_cap_vsx;
extern const VMStateDescription vmstate_spapr_cap_dfp;
extern const VMStateDescription vmstate_spapr_cap_cfpc;
extern const VMStateDescription vmstate_spapr_cap_sbbc;
extern const VMStateDescription vmstate_spapr_cap_ibs;
extern const VMStateDescription vmstate_spapr_cap_hpt_maxpagesize;
extern const VMStateDescription vmstate_spapr_cap_nested_kvm_hv;
extern const VMStateDescription vmstate_spapr_cap_large_decr;
extern const VMStateDescription vmstate_spapr_cap_ccf_assist;
extern const VMStateDescription vmstate_spapr_cap_fwnmi;

static inline uint8_t spapr_get_cap(SpaprMachineState *spapr, int cap)
{
    return spapr->eff.caps[cap];
}

void spapr_caps_init(SpaprMachineState *spapr);
void spapr_caps_apply(SpaprMachineState *spapr);
void spapr_caps_cpu_apply(SpaprMachineState *spapr, PowerPCCPU *cpu);
void spapr_caps_add_properties(SpaprMachineClass *smc);
int spapr_caps_post_migration(SpaprMachineState *spapr);

bool spapr_check_pagesize(SpaprMachineState *spapr, hwaddr pagesize,
                          Error **errp);
/*
 * XIVE definitions
 */
#define SPAPR_OV5_XIVE_LEGACY   0x0
#define SPAPR_OV5_XIVE_EXPLOIT  0x40
#define SPAPR_OV5_XIVE_BOTH     0x80 /* Only to advertise on the platform */

void spapr_set_all_lpcrs(target_ulong value, target_ulong mask);
hwaddr spapr_get_rtas_addr(void);
bool spapr_memory_hot_unplug_supported(SpaprMachineState *spapr);
#endif /* HW_SPAPR_H */
