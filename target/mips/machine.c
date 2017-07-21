#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "migration/cpu.h"

static int cpu_post_load(void *opaque, int version_id)
{
    MIPSCPU *cpu = opaque;
    CPUMIPSState *env = &cpu->env;

    restore_fp_status(env);
    restore_msa_fp_status(env);
    compute_hflags(env);
    restore_pamask(env);

    return 0;
}

/* FPU state */

static int get_fpr(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    int i;
    fpr_t *v = pv;
    /* Restore entire MSA vector register */
    for (i = 0; i < MSA_WRLEN/64; i++) {
        qemu_get_sbe64s(f, &v->wr.d[i]);
    }
    return 0;
}

static int put_fpr(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                   QJSON *vmdesc)
{
    int i;
    fpr_t *v = pv;
    /* Save entire MSA vector register */
    for (i = 0; i < MSA_WRLEN/64; i++) {
        qemu_put_sbe64s(f, &v->wr.d[i]);
    }

    return 0;
}

const VMStateInfo vmstate_info_fpr = {
    .name = "fpr",
    .get  = get_fpr,
    .put  = put_fpr,
};

#define VMSTATE_FPR_ARRAY_V(_f, _s, _n, _v)                     \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_fpr, fpr_t)

#define VMSTATE_FPR_ARRAY(_f, _s, _n)                           \
    VMSTATE_FPR_ARRAY_V(_f, _s, _n, 0)

static VMStateField vmstate_fpu_fields[] = {
    VMSTATE_FPR_ARRAY(fpr, CPUMIPSFPUContext, 32),
    VMSTATE_UINT32(fcr0, CPUMIPSFPUContext),
    VMSTATE_UINT32(fcr31, CPUMIPSFPUContext),
    VMSTATE_END_OF_LIST()
};

const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = vmstate_fpu_fields
};

const VMStateDescription vmstate_inactive_fpu = {
    .name = "cpu/inactive_fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = vmstate_fpu_fields
};

/* TC state */

static VMStateField vmstate_tc_fields[] = {
    VMSTATE_UINTTL_ARRAY(gpr, TCState, 32),
    VMSTATE_UINTTL(PC, TCState),
    VMSTATE_UINTTL_ARRAY(HI, TCState, MIPS_DSP_ACC),
    VMSTATE_UINTTL_ARRAY(LO, TCState, MIPS_DSP_ACC),
    VMSTATE_UINTTL_ARRAY(ACX, TCState, MIPS_DSP_ACC),
    VMSTATE_UINTTL(DSPControl, TCState),
    VMSTATE_INT32(CP0_TCStatus, TCState),
    VMSTATE_INT32(CP0_TCBind, TCState),
    VMSTATE_UINTTL(CP0_TCHalt, TCState),
    VMSTATE_UINTTL(CP0_TCContext, TCState),
    VMSTATE_UINTTL(CP0_TCSchedule, TCState),
    VMSTATE_UINTTL(CP0_TCScheFBack, TCState),
    VMSTATE_INT32(CP0_Debug_tcstatus, TCState),
    VMSTATE_UINTTL(CP0_UserLocal, TCState),
    VMSTATE_INT32(msacsr, TCState),
    VMSTATE_END_OF_LIST()
};

const VMStateDescription vmstate_tc = {
    .name = "cpu/tc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = vmstate_tc_fields
};

const VMStateDescription vmstate_inactive_tc = {
    .name = "cpu/inactive_tc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = vmstate_tc_fields
};

/* MVP state */

const VMStateDescription vmstate_mvp = {
    .name = "cpu/mvp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(CP0_MVPControl, CPUMIPSMVPContext),
        VMSTATE_INT32(CP0_MVPConf0, CPUMIPSMVPContext),
        VMSTATE_INT32(CP0_MVPConf1, CPUMIPSMVPContext),
        VMSTATE_END_OF_LIST()
    }
};

/* TLB state */

static int get_tlb(QEMUFile *f, void *pv, size_t size, VMStateField *field)
{
    r4k_tlb_t *v = pv;
    uint16_t flags;

    qemu_get_betls(f, &v->VPN);
    qemu_get_be32s(f, &v->PageMask);
    qemu_get_be16s(f, &v->ASID);
    qemu_get_be16s(f, &flags);
    v->G = (flags >> 10) & 1;
    v->C0 = (flags >> 7) & 3;
    v->C1 = (flags >> 4) & 3;
    v->V0 = (flags >> 3) & 1;
    v->V1 = (flags >> 2) & 1;
    v->D0 = (flags >> 1) & 1;
    v->D1 = (flags >> 0) & 1;
    v->EHINV = (flags >> 15) & 1;
    v->RI1 = (flags >> 14) & 1;
    v->RI0 = (flags >> 13) & 1;
    v->XI1 = (flags >> 12) & 1;
    v->XI0 = (flags >> 11) & 1;
    qemu_get_be64s(f, &v->PFN[0]);
    qemu_get_be64s(f, &v->PFN[1]);

    return 0;
}

static int put_tlb(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                   QJSON *vmdesc)
{
    r4k_tlb_t *v = pv;

    uint16_t asid = v->ASID;
    uint16_t flags = ((v->EHINV << 15) |
                      (v->RI1 << 14) |
                      (v->RI0 << 13) |
                      (v->XI1 << 12) |
                      (v->XI0 << 11) |
                      (v->G << 10) |
                      (v->C0 << 7) |
                      (v->C1 << 4) |
                      (v->V0 << 3) |
                      (v->V1 << 2) |
                      (v->D0 << 1) |
                      (v->D1 << 0));

    qemu_put_betls(f, &v->VPN);
    qemu_put_be32s(f, &v->PageMask);
    qemu_put_be16s(f, &asid);
    qemu_put_be16s(f, &flags);
    qemu_put_be64s(f, &v->PFN[0]);
    qemu_put_be64s(f, &v->PFN[1]);

    return 0;
}

const VMStateInfo vmstate_info_tlb = {
    .name = "tlb_entry",
    .get  = get_tlb,
    .put  = put_tlb,
};

#define VMSTATE_TLB_ARRAY_V(_f, _s, _n, _v)                     \
    VMSTATE_ARRAY(_f, _s, _n, _v, vmstate_info_tlb, r4k_tlb_t)

#define VMSTATE_TLB_ARRAY(_f, _s, _n)                           \
    VMSTATE_TLB_ARRAY_V(_f, _s, _n, 0)

const VMStateDescription vmstate_tlb = {
    .name = "cpu/tlb",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(nb_tlb, CPUMIPSTLBContext),
        VMSTATE_UINT32(tlb_in_use, CPUMIPSTLBContext),
        VMSTATE_TLB_ARRAY(mmu.r4k.tlb, CPUMIPSTLBContext, MIPS_TLB_MAX),
        VMSTATE_END_OF_LIST()
    }
};

/* MIPS CPU state */

const VMStateDescription vmstate_mips_cpu = {
    .name = "cpu",
    .version_id = 10,
    .minimum_version_id = 10,
    .post_load = cpu_post_load,
    .fields = (VMStateField[]) {
        /* Active TC */
        VMSTATE_STRUCT(env.active_tc, MIPSCPU, 1, vmstate_tc, TCState),

        /* Active FPU */
        VMSTATE_STRUCT(env.active_fpu, MIPSCPU, 1, vmstate_fpu,
                       CPUMIPSFPUContext),

        /* MVP */
        VMSTATE_STRUCT_POINTER(env.mvp, MIPSCPU, vmstate_mvp,
                               CPUMIPSMVPContext),

        /* TLB */
        VMSTATE_STRUCT_POINTER(env.tlb, MIPSCPU, vmstate_tlb,
                               CPUMIPSTLBContext),

        /* CPU metastate */
        VMSTATE_UINT32(env.current_tc, MIPSCPU),
        VMSTATE_UINT32(env.current_fpu, MIPSCPU),
        VMSTATE_INT32(env.error_code, MIPSCPU),
        VMSTATE_UINTTL(env.btarget, MIPSCPU),
        VMSTATE_UINTTL(env.bcond, MIPSCPU),

        /* Remaining CP0 registers */
        VMSTATE_INT32(env.CP0_Index, MIPSCPU),
        VMSTATE_INT32(env.CP0_Random, MIPSCPU),
        VMSTATE_INT32(env.CP0_VPEControl, MIPSCPU),
        VMSTATE_INT32(env.CP0_VPEConf0, MIPSCPU),
        VMSTATE_INT32(env.CP0_VPEConf1, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_YQMask, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_VPESchedule, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_VPEScheFBack, MIPSCPU),
        VMSTATE_INT32(env.CP0_VPEOpt, MIPSCPU),
        VMSTATE_UINT64(env.CP0_EntryLo0, MIPSCPU),
        VMSTATE_UINT64(env.CP0_EntryLo1, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_Context, MIPSCPU),
        VMSTATE_INT32(env.CP0_PageMask, MIPSCPU),
        VMSTATE_INT32(env.CP0_PageGrain, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_SegCtl0, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_SegCtl1, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_SegCtl2, MIPSCPU),
        VMSTATE_INT32(env.CP0_Wired, MIPSCPU),
        VMSTATE_INT32(env.CP0_SRSConf0, MIPSCPU),
        VMSTATE_INT32(env.CP0_SRSConf1, MIPSCPU),
        VMSTATE_INT32(env.CP0_SRSConf2, MIPSCPU),
        VMSTATE_INT32(env.CP0_SRSConf3, MIPSCPU),
        VMSTATE_INT32(env.CP0_SRSConf4, MIPSCPU),
        VMSTATE_INT32(env.CP0_HWREna, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_BadVAddr, MIPSCPU),
        VMSTATE_UINT32(env.CP0_BadInstr, MIPSCPU),
        VMSTATE_UINT32(env.CP0_BadInstrP, MIPSCPU),
        VMSTATE_INT32(env.CP0_Count, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_EntryHi, MIPSCPU),
        VMSTATE_INT32(env.CP0_Compare, MIPSCPU),
        VMSTATE_INT32(env.CP0_Status, MIPSCPU),
        VMSTATE_INT32(env.CP0_IntCtl, MIPSCPU),
        VMSTATE_INT32(env.CP0_SRSCtl, MIPSCPU),
        VMSTATE_INT32(env.CP0_SRSMap, MIPSCPU),
        VMSTATE_INT32(env.CP0_Cause, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_EPC, MIPSCPU),
        VMSTATE_INT32(env.CP0_PRid, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_EBase, MIPSCPU),
        VMSTATE_INT32(env.CP0_Config0, MIPSCPU),
        VMSTATE_INT32(env.CP0_Config1, MIPSCPU),
        VMSTATE_INT32(env.CP0_Config2, MIPSCPU),
        VMSTATE_INT32(env.CP0_Config3, MIPSCPU),
        VMSTATE_INT32(env.CP0_Config6, MIPSCPU),
        VMSTATE_INT32(env.CP0_Config7, MIPSCPU),
        VMSTATE_UINT64_ARRAY(env.CP0_MAAR, MIPSCPU, MIPS_MAAR_MAX),
        VMSTATE_INT32(env.CP0_MAARI, MIPSCPU),
        VMSTATE_UINT64(env.lladdr, MIPSCPU),
        VMSTATE_UINTTL_ARRAY(env.CP0_WatchLo, MIPSCPU, 8),
        VMSTATE_INT32_ARRAY(env.CP0_WatchHi, MIPSCPU, 8),
        VMSTATE_UINTTL(env.CP0_XContext, MIPSCPU),
        VMSTATE_INT32(env.CP0_Framemask, MIPSCPU),
        VMSTATE_INT32(env.CP0_Debug, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_DEPC, MIPSCPU),
        VMSTATE_INT32(env.CP0_Performance0, MIPSCPU),
        VMSTATE_UINT64(env.CP0_TagLo, MIPSCPU),
        VMSTATE_INT32(env.CP0_DataLo, MIPSCPU),
        VMSTATE_INT32(env.CP0_TagHi, MIPSCPU),
        VMSTATE_INT32(env.CP0_DataHi, MIPSCPU),
        VMSTATE_UINTTL(env.CP0_ErrorEPC, MIPSCPU),
        VMSTATE_INT32(env.CP0_DESAVE, MIPSCPU),
        VMSTATE_UINTTL_ARRAY(env.CP0_KScratch, MIPSCPU, MIPS_KSCRATCH_NUM),

        /* Inactive TC */
        VMSTATE_STRUCT_ARRAY(env.tcs, MIPSCPU, MIPS_SHADOW_SET_MAX, 1,
                             vmstate_inactive_tc, TCState),
        VMSTATE_STRUCT_ARRAY(env.fpus, MIPSCPU, MIPS_FPU_MAX, 1,
                             vmstate_inactive_fpu, CPUMIPSFPUContext),

        VMSTATE_END_OF_LIST()
    },
};
