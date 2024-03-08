#ifndef HW_SPAPR_NESTED_H
#define HW_SPAPR_NESTED_H

#include "target/ppc/cpu.h"

typedef struct SpaprMachineStateNested {
    uint64_t ptcr;
    uint8_t api;
#define NESTED_API_KVM_HV  1
    bool capabilities_set;
    uint32_t pvr_base;
} SpaprMachineStateNested;

/* Nested PAPR API related macros */
#define H_GUEST_CAPABILITIES_COPY_MEM 0x8000000000000000
#define H_GUEST_CAPABILITIES_P9_MODE  0x4000000000000000
#define H_GUEST_CAPABILITIES_P10_MODE 0x2000000000000000
#define H_GUEST_CAP_VALID_MASK        (H_GUEST_CAPABILITIES_P10_MODE | \
                                       H_GUEST_CAPABILITIES_P9_MODE)
#define H_GUEST_CAP_COPY_MEM_BMAP     0
#define H_GUEST_CAP_P9_MODE_BMAP      1
#define H_GUEST_CAP_P10_MODE_BMAP     2

/*
 * Register state for entering a nested guest with H_ENTER_NESTED.
 * New member must be added at the end.
 */
struct kvmppc_hv_guest_state {
    uint64_t version;      /* version of this structure layout, must be first */
    uint32_t lpid;
    uint32_t vcpu_token;
    /* These registers are hypervisor privileged (at least for writing) */
    uint64_t lpcr;
    uint64_t pcr;
    uint64_t amor;
    uint64_t dpdes;
    uint64_t hfscr;
    int64_t tb_offset;
    uint64_t dawr0;
    uint64_t dawrx0;
    uint64_t ciabr;
    uint64_t hdec_expiry;
    uint64_t purr;
    uint64_t spurr;
    uint64_t ic;
    uint64_t vtb;
    uint64_t hdar;
    uint64_t hdsisr;
    uint64_t heir;
    uint64_t asdr;
    /* These are OS privileged but need to be set late in guest entry */
    uint64_t srr0;
    uint64_t srr1;
    uint64_t sprg[4];
    uint64_t pidr;
    uint64_t cfar;
    uint64_t ppr;
    /* Version 1 ends here */
    uint64_t dawr1;
    uint64_t dawrx1;
    /* Version 2 ends here */
};

/* Latest version of hv_guest_state structure */
#define HV_GUEST_STATE_VERSION  2

/* Linux 64-bit powerpc pt_regs struct, used by nested HV */
struct kvmppc_pt_regs {
    uint64_t gpr[32];
    uint64_t nip;
    uint64_t msr;
    uint64_t orig_gpr3;    /* Used for restarting system calls */
    uint64_t ctr;
    uint64_t link;
    uint64_t xer;
    uint64_t ccr;
    uint64_t softe;        /* Soft enabled/disabled */
    uint64_t trap;         /* Reason for being here */
    uint64_t dar;          /* Fault registers */
    uint64_t dsisr;        /* on 4xx/Book-E used for ESR */
    uint64_t result;       /* Result of a system call */
};

/*
 * nested_ppc_state is used to save the host CPU state before switching it to
 * the guest CPU state, to be restored on H_ENTER_NESTED exit.
 */
struct nested_ppc_state {
    uint64_t gpr[32];
    uint64_t lr;
    uint64_t ctr;
    uint64_t cfar;
    uint64_t msr;
    uint64_t nip;
    uint32_t cr;

    uint64_t xer;

    uint64_t lpcr;
    uint64_t lpidr;
    uint64_t pidr;
    uint64_t pcr;
    uint64_t dpdes;
    uint64_t hfscr;
    uint64_t srr0;
    uint64_t srr1;
    uint64_t sprg0;
    uint64_t sprg1;
    uint64_t sprg2;
    uint64_t sprg3;
    uint64_t ppr;

    int64_t tb_offset;
};

void spapr_exit_nested(PowerPCCPU *cpu, int excp);
typedef struct SpaprMachineState SpaprMachineState;
bool spapr_get_pate_nested_hv(SpaprMachineState *spapr, PowerPCCPU *cpu,
                              target_ulong lpid, ppc_v3_pate_t *entry);
uint8_t spapr_nested_api(SpaprMachineState *spapr);
#endif /* HW_SPAPR_NESTED_H */
