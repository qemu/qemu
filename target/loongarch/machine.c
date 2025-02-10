/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch Machine State
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/cpu.h"
#include "system/tcg.h"
#include "vec.h"

static const VMStateDescription vmstate_fpu_reg = {
    .name = "fpu_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(UD(0), VReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_FPU_REGS(_field, _state, _start)            \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, 32, 0, \
                             vmstate_fpu_reg, fpr_t)

static bool fpu_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, FP);
}

static const VMStateDescription vmstate_fpu = {
    .name = "cpu/fpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = fpu_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_FPU_REGS(env.fpr, LoongArchCPU, 0),
        VMSTATE_UINT32(env.fcsr0, LoongArchCPU),
        VMSTATE_BOOL_ARRAY(env.cf, LoongArchCPU, 8),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_lsxh_reg = {
    .name = "lsxh_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(UD(1), VReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_LSXH_REGS(_field, _state, _start)           \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, 32, 0, \
                             vmstate_lsxh_reg, fpr_t)

static bool lsx_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LSX);
}

static const VMStateDescription vmstate_lsx = {
    .name = "cpu/lsx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = lsx_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_LSXH_REGS(env.fpr, LoongArchCPU, 0),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_lasxh_reg = {
    .name = "lasxh_reg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(UD(2), VReg),
        VMSTATE_UINT64(UD(3), VReg),
        VMSTATE_END_OF_LIST()
    }
};

#define VMSTATE_LASXH_REGS(_field, _state, _start)          \
    VMSTATE_STRUCT_SUB_ARRAY(_field, _state, _start, 32, 0, \
                             vmstate_lasxh_reg, fpr_t)

static bool lasx_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LASX);
}

static const VMStateDescription vmstate_lasx = {
    .name = "cpu/lasx",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = lasx_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_LASXH_REGS(env.fpr, LoongArchCPU, 0),
        VMSTATE_END_OF_LIST()
    },
};

static bool lbt_needed(void *opaque)
{
    LoongArchCPU *cpu = opaque;

    return !!FIELD_EX64(cpu->env.cpucfg[2], CPUCFG2, LBT_ALL);
}

static const VMStateDescription vmstate_lbt = {
    .name = "cpu/lbt",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = lbt_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(env.lbt.scr0,   LoongArchCPU),
        VMSTATE_UINT64(env.lbt.scr1,   LoongArchCPU),
        VMSTATE_UINT64(env.lbt.scr2,   LoongArchCPU),
        VMSTATE_UINT64(env.lbt.scr3,   LoongArchCPU),
        VMSTATE_UINT32(env.lbt.eflags, LoongArchCPU),
        VMSTATE_UINT32(env.lbt.ftop,   LoongArchCPU),
        VMSTATE_END_OF_LIST()
    },
};

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
static bool tlb_needed(void *opaque)
{
    return tcg_enabled();
}

/* TLB state */
static const VMStateDescription vmstate_tlb_entry = {
    .name = "cpu/tlb_entry",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(tlb_misc, LoongArchTLB),
        VMSTATE_UINT64(tlb_entry0, LoongArchTLB),
        VMSTATE_UINT64(tlb_entry1, LoongArchTLB),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_tlb = {
    .name = "cpu/tlb",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = tlb_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.tlb, LoongArchCPU, LOONGARCH_TLB_MAX,
                             0, vmstate_tlb_entry, LoongArchTLB),
        VMSTATE_END_OF_LIST()
    }
};
#endif

/* LoongArch CPU state */
const VMStateDescription vmstate_loongarch_cpu = {
    .name = "cpu",
    .version_id = 3,
    .minimum_version_id = 3,
    .fields = (const VMStateField[]) {
        VMSTATE_UINTTL_ARRAY(env.gpr, LoongArchCPU, 32),
        VMSTATE_UINTTL(env.pc, LoongArchCPU),

        /* Remaining CSRs */
        VMSTATE_UINT64(env.CSR_CRMD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRMD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_EUEN, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MISC, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ECFG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ESTAT, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_BADV, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_BADI, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_EENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBIDX, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBEHI, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBELO0, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBELO1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_ASID, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PGDL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PGDH, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PGD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PWCL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PWCH, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_STLBPS, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_RVACFG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRCFG1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRCFG2, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_PRCFG3, LoongArchCPU),
        VMSTATE_UINT64_ARRAY(env.CSR_SAVE, LoongArchCPU, 16),
        VMSTATE_UINT64(env.CSR_TID, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TCFG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TVAL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_CNTC, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TICLR, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_LLBCTL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_IMPCTL1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_IMPCTL2, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRBADV, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRSAVE, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRELO0, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRELO1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBREHI, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_TLBRPRMD, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRCTL, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRINFO1, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRINFO2, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRENTRY, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_MERRSAVE, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_CTAG, LoongArchCPU),
        VMSTATE_UINT64_ARRAY(env.CSR_DMW, LoongArchCPU, 4),

        /* Debug CSRs */
        VMSTATE_UINT64(env.CSR_DBG, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_DERA, LoongArchCPU),
        VMSTATE_UINT64(env.CSR_DSAVE, LoongArchCPU),

        VMSTATE_UINT64(kvm_state_counter, LoongArchCPU),
        /* PV steal time */
        VMSTATE_UINT64(env.stealtime.guest_addr, LoongArchCPU),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_fpu,
        &vmstate_lsx,
        &vmstate_lasx,
#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
        &vmstate_tlb,
#endif
        &vmstate_lbt,
        NULL
    }
};
