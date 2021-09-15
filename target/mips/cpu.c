/*
 * QEMU MIPS CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "cpu.h"
#include "internal.h"
#include "kvm_mips.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"
#include "exec/exec-all.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "semihosting/semihost.h"
#include "qapi/qapi-commands-machine-target.h"
#include "fpu_helper.h"

const char regnames[32][3] = {
    "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra",
};

static void fpu_dump_fpr(fpr_t *fpr, FILE *f, bool is_fpu64)
{
    if (is_fpu64) {
        qemu_fprintf(f, "w:%08x d:%016" PRIx64 " fd:%13g fs:%13g psu: %13g\n",
                     fpr->w[FP_ENDIAN_IDX], fpr->d,
                     (double)fpr->fd,
                     (double)fpr->fs[FP_ENDIAN_IDX],
                     (double)fpr->fs[!FP_ENDIAN_IDX]);
    } else {
        fpr_t tmp;

        tmp.w[FP_ENDIAN_IDX] = fpr->w[FP_ENDIAN_IDX];
        tmp.w[!FP_ENDIAN_IDX] = (fpr + 1)->w[FP_ENDIAN_IDX];
        qemu_fprintf(f, "w:%08x d:%016" PRIx64 " fd:%13g fs:%13g psu:%13g\n",
                     tmp.w[FP_ENDIAN_IDX], tmp.d,
                     (double)tmp.fd,
                     (double)tmp.fs[FP_ENDIAN_IDX],
                     (double)tmp.fs[!FP_ENDIAN_IDX]);
    }
}

static void fpu_dump_state(CPUMIPSState *env, FILE *f, int flags)
{
    int i;
    bool is_fpu64 = !!(env->hflags & MIPS_HFLAG_F64);

    qemu_fprintf(f,
                 "CP1 FCR0 0x%08x  FCR31 0x%08x  SR.FR %d  fp_status 0x%02x\n",
                 env->active_fpu.fcr0, env->active_fpu.fcr31, is_fpu64,
                 get_float_exception_flags(&env->active_fpu.fp_status));
    for (i = 0; i < 32; (is_fpu64) ? i++ : (i += 2)) {
        qemu_fprintf(f, "%3s: ", fregnames[i]);
        fpu_dump_fpr(&env->active_fpu.fpr[i], f, is_fpu64);
    }
}

static void mips_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int i;

    qemu_fprintf(f, "pc=0x" TARGET_FMT_lx " HI=0x" TARGET_FMT_lx
                 " LO=0x" TARGET_FMT_lx " ds %04x "
                 TARGET_FMT_lx " " TARGET_FMT_ld "\n",
                 env->active_tc.PC, env->active_tc.HI[0], env->active_tc.LO[0],
                 env->hflags, env->btarget, env->bcond);
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0) {
            qemu_fprintf(f, "GPR%02d:", i);
        }
        qemu_fprintf(f, " %s " TARGET_FMT_lx,
                     regnames[i], env->active_tc.gpr[i]);
        if ((i & 3) == 3) {
            qemu_fprintf(f, "\n");
        }
    }

    qemu_fprintf(f, "CP0 Status  0x%08x Cause   0x%08x EPC    0x"
                 TARGET_FMT_lx "\n",
                 env->CP0_Status, env->CP0_Cause, env->CP0_EPC);
    qemu_fprintf(f, "    Config0 0x%08x Config1 0x%08x LLAddr 0x%016"
                 PRIx64 "\n",
                 env->CP0_Config0, env->CP0_Config1, env->CP0_LLAddr);
    qemu_fprintf(f, "    Config2 0x%08x Config3 0x%08x\n",
                 env->CP0_Config2, env->CP0_Config3);
    qemu_fprintf(f, "    Config4 0x%08x Config5 0x%08x\n",
                 env->CP0_Config4, env->CP0_Config5);
    if ((flags & CPU_DUMP_FPU) && (env->hflags & MIPS_HFLAG_FPU)) {
        fpu_dump_state(env, f, flags);
    }
}

void cpu_set_exception_base(int vp_index, target_ulong address)
{
    MIPSCPU *vp = MIPS_CPU(qemu_get_cpu(vp_index));
    vp->env.exception_base = address;
}

static void mips_cpu_set_pc(CPUState *cs, vaddr value)
{
    MIPSCPU *cpu = MIPS_CPU(cs);

    mips_env_set_pc(&cpu->env, value);
}

static bool mips_cpu_has_work(CPUState *cs)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    bool has_work = false;

    /*
     * Prior to MIPS Release 6 it is implementation dependent if non-enabled
     * interrupts wake-up the CPU, however most of the implementations only
     * check for interrupts that can be taken.
     */
    if ((cs->interrupt_request & CPU_INTERRUPT_HARD) &&
        cpu_mips_hw_interrupts_pending(env)) {
        if (cpu_mips_hw_interrupts_enabled(env) ||
            (env->insn_flags & ISA_MIPS_R6)) {
            has_work = true;
        }
    }

    /* MIPS-MT has the ability to halt the CPU.  */
    if (ase_mt_available(env)) {
        /*
         * The QEMU model will issue an _WAKE request whenever the CPUs
         * should be woken up.
         */
        if (cs->interrupt_request & CPU_INTERRUPT_WAKE) {
            has_work = true;
        }

        if (!mips_vpe_active(env)) {
            has_work = false;
        }
    }
    /* MIPS Release 6 has the ability to halt the CPU.  */
    if (env->CP0_Config5 & (1 << CP0C5_VP)) {
        if (cs->interrupt_request & CPU_INTERRUPT_WAKE) {
            has_work = true;
        }
        if (!mips_vp_active(env)) {
            has_work = false;
        }
    }
    return has_work;
}

#include "cpu-defs.c.inc"

static void mips_cpu_reset(DeviceState *dev)
{
    CPUState *cs = CPU(dev);
    MIPSCPU *cpu = MIPS_CPU(cs);
    MIPSCPUClass *mcc = MIPS_CPU_GET_CLASS(cpu);
    CPUMIPSState *env = &cpu->env;

    mcc->parent_reset(dev);

    memset(env, 0, offsetof(CPUMIPSState, end_reset_fields));

    /* Reset registers to their default values */
    env->CP0_PRid = env->cpu_model->CP0_PRid;
    env->CP0_Config0 = env->cpu_model->CP0_Config0;
#ifdef TARGET_WORDS_BIGENDIAN
    env->CP0_Config0 |= (1 << CP0C0_BE);
#endif
    env->CP0_Config1 = env->cpu_model->CP0_Config1;
    env->CP0_Config2 = env->cpu_model->CP0_Config2;
    env->CP0_Config3 = env->cpu_model->CP0_Config3;
    env->CP0_Config4 = env->cpu_model->CP0_Config4;
    env->CP0_Config4_rw_bitmask = env->cpu_model->CP0_Config4_rw_bitmask;
    env->CP0_Config5 = env->cpu_model->CP0_Config5;
    env->CP0_Config5_rw_bitmask = env->cpu_model->CP0_Config5_rw_bitmask;
    env->CP0_Config6 = env->cpu_model->CP0_Config6;
    env->CP0_Config6_rw_bitmask = env->cpu_model->CP0_Config6_rw_bitmask;
    env->CP0_Config7 = env->cpu_model->CP0_Config7;
    env->CP0_Config7_rw_bitmask = env->cpu_model->CP0_Config7_rw_bitmask;
    env->CP0_LLAddr_rw_bitmask = env->cpu_model->CP0_LLAddr_rw_bitmask
                                 << env->cpu_model->CP0_LLAddr_shift;
    env->CP0_LLAddr_shift = env->cpu_model->CP0_LLAddr_shift;
    env->SYNCI_Step = env->cpu_model->SYNCI_Step;
    env->CCRes = env->cpu_model->CCRes;
    env->CP0_Status_rw_bitmask = env->cpu_model->CP0_Status_rw_bitmask;
    env->CP0_TCStatus_rw_bitmask = env->cpu_model->CP0_TCStatus_rw_bitmask;
    env->CP0_SRSCtl = env->cpu_model->CP0_SRSCtl;
    env->current_tc = 0;
    env->SEGBITS = env->cpu_model->SEGBITS;
    env->SEGMask = (target_ulong)((1ULL << env->cpu_model->SEGBITS) - 1);
#if defined(TARGET_MIPS64)
    if (env->cpu_model->insn_flags & ISA_MIPS3) {
        env->SEGMask |= 3ULL << 62;
    }
#endif
    env->PABITS = env->cpu_model->PABITS;
    env->CP0_SRSConf0_rw_bitmask = env->cpu_model->CP0_SRSConf0_rw_bitmask;
    env->CP0_SRSConf0 = env->cpu_model->CP0_SRSConf0;
    env->CP0_SRSConf1_rw_bitmask = env->cpu_model->CP0_SRSConf1_rw_bitmask;
    env->CP0_SRSConf1 = env->cpu_model->CP0_SRSConf1;
    env->CP0_SRSConf2_rw_bitmask = env->cpu_model->CP0_SRSConf2_rw_bitmask;
    env->CP0_SRSConf2 = env->cpu_model->CP0_SRSConf2;
    env->CP0_SRSConf3_rw_bitmask = env->cpu_model->CP0_SRSConf3_rw_bitmask;
    env->CP0_SRSConf3 = env->cpu_model->CP0_SRSConf3;
    env->CP0_SRSConf4_rw_bitmask = env->cpu_model->CP0_SRSConf4_rw_bitmask;
    env->CP0_SRSConf4 = env->cpu_model->CP0_SRSConf4;
    env->CP0_PageGrain_rw_bitmask = env->cpu_model->CP0_PageGrain_rw_bitmask;
    env->CP0_PageGrain = env->cpu_model->CP0_PageGrain;
    env->CP0_EBaseWG_rw_bitmask = env->cpu_model->CP0_EBaseWG_rw_bitmask;
    env->active_fpu.fcr0 = env->cpu_model->CP1_fcr0;
    env->active_fpu.fcr31_rw_bitmask = env->cpu_model->CP1_fcr31_rw_bitmask;
    env->active_fpu.fcr31 = env->cpu_model->CP1_fcr31;
    env->msair = env->cpu_model->MSAIR;
    env->insn_flags = env->cpu_model->insn_flags;

#if defined(CONFIG_USER_ONLY)
    env->CP0_Status = (MIPS_HFLAG_UM << CP0St_KSU);
# ifdef TARGET_MIPS64
    /* Enable 64-bit register mode.  */
    env->CP0_Status |= (1 << CP0St_PX);
# endif
# ifdef TARGET_ABI_MIPSN64
    /* Enable 64-bit address mode.  */
    env->CP0_Status |= (1 << CP0St_UX);
# endif
    /*
     * Enable access to the CPUNum, SYNCI_Step, CC, and CCRes RDHWR
     * hardware registers.
     */
    env->CP0_HWREna |= 0x0000000F;
    if (env->CP0_Config1 & (1 << CP0C1_FP)) {
        env->CP0_Status |= (1 << CP0St_CU1);
    }
    if (env->CP0_Config3 & (1 << CP0C3_DSPP)) {
        env->CP0_Status |= (1 << CP0St_MX);
    }
# if defined(TARGET_MIPS64)
    /* For MIPS64, init FR bit to 1 if FPU unit is there and bit is writable. */
    if ((env->CP0_Config1 & (1 << CP0C1_FP)) &&
        (env->CP0_Status_rw_bitmask & (1 << CP0St_FR))) {
        env->CP0_Status |= (1 << CP0St_FR);
    }
# endif
#else /* !CONFIG_USER_ONLY */
    if (env->hflags & MIPS_HFLAG_BMASK) {
        /*
         * If the exception was raised from a delay slot,
         * come back to the jump.
         */
        env->CP0_ErrorEPC = (env->active_tc.PC
                             - (env->hflags & MIPS_HFLAG_B16 ? 2 : 4));
    } else {
        env->CP0_ErrorEPC = env->active_tc.PC;
    }
    env->active_tc.PC = env->exception_base;
    env->CP0_Random = env->tlb->nb_tlb - 1;
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
    env->CP0_Wired = 0;
    env->CP0_GlobalNumber = (cs->cpu_index & 0xFF) << CP0GN_VPId;
    env->CP0_EBase = (cs->cpu_index & 0x3FF);
    if (mips_um_ksegs_enabled()) {
        env->CP0_EBase |= 0x40000000;
    } else {
        env->CP0_EBase |= (int32_t)0x80000000;
    }
    if (env->CP0_Config3 & (1 << CP0C3_CMGCR)) {
        env->CP0_CMGCRBase = 0x1fbf8000 >> 4;
    }
    env->CP0_EntryHi_ASID_mask = (env->CP0_Config5 & (1 << CP0C5_MI)) ?
            0x0 : (env->CP0_Config4 & (1 << CP0C4_AE)) ? 0x3ff : 0xff;
    env->CP0_Status = (1 << CP0St_BEV) | (1 << CP0St_ERL);
    /*
     * Vectored interrupts not implemented, timer on int 7,
     * no performance counters.
     */
    env->CP0_IntCtl = 0xe0000000;
    {
        int i;

        for (i = 0; i < 7; i++) {
            env->CP0_WatchLo[i] = 0;
            env->CP0_WatchHi[i] = 0x80000000;
        }
        env->CP0_WatchLo[7] = 0;
        env->CP0_WatchHi[7] = 0;
    }
    /* Count register increments in debug mode, EJTAG version 1 */
    env->CP0_Debug = (1 << CP0DB_CNT) | (0x1 << CP0DB_VER);

    cpu_mips_store_count(env, 1);

    if (ase_mt_available(env)) {
        int i;

        /* Only TC0 on VPE 0 starts as active.  */
        for (i = 0; i < ARRAY_SIZE(env->tcs); i++) {
            env->tcs[i].CP0_TCBind = cs->cpu_index << CP0TCBd_CurVPE;
            env->tcs[i].CP0_TCHalt = 1;
        }
        env->active_tc.CP0_TCHalt = 1;
        cs->halted = 1;

        if (cs->cpu_index == 0) {
            /* VPE0 starts up enabled.  */
            env->mvp->CP0_MVPControl |= (1 << CP0MVPCo_EVP);
            env->CP0_VPEConf0 |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);

            /* TC0 starts up unhalted.  */
            cs->halted = 0;
            env->active_tc.CP0_TCHalt = 0;
            env->tcs[0].CP0_TCHalt = 0;
            /* With thread 0 active.  */
            env->active_tc.CP0_TCStatus = (1 << CP0TCSt_A);
            env->tcs[0].CP0_TCStatus = (1 << CP0TCSt_A);
        }
    }

    /*
     * Configure default legacy segmentation control. We use this regardless of
     * whether segmentation control is presented to the guest.
     */
    /* KSeg3 (seg0 0xE0000000..0xFFFFFFFF) */
    env->CP0_SegCtl0 =   (CP0SC_AM_MK << CP0SC_AM);
    /* KSeg2 (seg1 0xC0000000..0xDFFFFFFF) */
    env->CP0_SegCtl0 |= ((CP0SC_AM_MSK << CP0SC_AM)) << 16;
    /* KSeg1 (seg2 0xA0000000..0x9FFFFFFF) */
    env->CP0_SegCtl1 =   (0 << CP0SC_PA) | (CP0SC_AM_UK << CP0SC_AM) |
                         (2 << CP0SC_C);
    /* KSeg0 (seg3 0x80000000..0x9FFFFFFF) */
    env->CP0_SegCtl1 |= ((0 << CP0SC_PA) | (CP0SC_AM_UK << CP0SC_AM) |
                         (3 << CP0SC_C)) << 16;
    /* USeg (seg4 0x40000000..0x7FFFFFFF) */
    env->CP0_SegCtl2 =   (2 << CP0SC_PA) | (CP0SC_AM_MUSK << CP0SC_AM) |
                         (1 << CP0SC_EU) | (2 << CP0SC_C);
    /* USeg (seg5 0x00000000..0x3FFFFFFF) */
    env->CP0_SegCtl2 |= ((0 << CP0SC_PA) | (CP0SC_AM_MUSK << CP0SC_AM) |
                         (1 << CP0SC_EU) | (2 << CP0SC_C)) << 16;
    /* XKPhys (note, SegCtl2.XR = 0, so XAM won't be used) */
    env->CP0_SegCtl1 |= (CP0SC_AM_UK << CP0SC1_XAM);
#endif /* !CONFIG_USER_ONLY */
    if ((env->insn_flags & ISA_MIPS_R6) &&
        (env->active_fpu.fcr0 & (1 << FCR0_F64))) {
        /* Status.FR = 0 mode in 64-bit FPU not allowed in R6 */
        env->CP0_Status |= (1 << CP0St_FR);
    }

    if (env->insn_flags & ISA_MIPS_R6) {
        /* PTW  =  1 */
        env->CP0_PWSize = 0x40;
        /* GDI  = 12 */
        /* UDI  = 12 */
        /* MDI  = 12 */
        /* PRI  = 12 */
        /* PTEI =  2 */
        env->CP0_PWField = 0x0C30C302;
    } else {
        /* GDI  =  0 */
        /* UDI  =  0 */
        /* MDI  =  0 */
        /* PRI  =  0 */
        /* PTEI =  2 */
        env->CP0_PWField = 0x02;
    }

    if (env->CP0_Config3 & (1 << CP0C3_ISA) & (1 << (CP0C3_ISA + 1))) {
        /*  microMIPS on reset when Config3.ISA is 3 */
        env->hflags |= MIPS_HFLAG_M16;
    }

    msa_reset(env);

    compute_hflags(env);
    restore_fp_status(env);
    restore_pamask(env);
    cs->exception_index = EXCP_NONE;

    if (semihosting_get_argc()) {
        /* UHI interface can be used to obtain argc and argv */
        env->active_tc.gpr[4] = -1;
    }

#ifndef CONFIG_USER_ONLY
    if (kvm_enabled()) {
        kvm_mips_reset_vcpu(cpu);
    }
#endif
}

static void mips_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    MIPSCPU *cpu = MIPS_CPU(s);
    CPUMIPSState *env = &cpu->env;

    if (!(env->insn_flags & ISA_NANOMIPS32)) {
#ifdef TARGET_WORDS_BIGENDIAN
        info->print_insn = print_insn_big_mips;
#else
        info->print_insn = print_insn_little_mips;
#endif
    } else {
#if defined(CONFIG_NANOMIPS_DIS)
        info->print_insn = print_insn_nanomips;
#endif
    }
}

/*
 * Since commit 6af0bf9c7c3 this model assumes a CPU clocked at 200MHz.
 */
#define CPU_FREQ_HZ_DEFAULT     200000000
#define CP0_COUNT_RATE_DEFAULT  2

static void mips_cp0_period_set(MIPSCPU *cpu)
{
    CPUMIPSState *env = &cpu->env;

    env->cp0_count_ns = clock_ticks_to_ns(MIPS_CPU(cpu)->clock,
                                          cpu->cp0_count_rate);
    assert(env->cp0_count_ns);
}

static void mips_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    MIPSCPU *cpu = MIPS_CPU(dev);
    CPUMIPSState *env = &cpu->env;
    MIPSCPUClass *mcc = MIPS_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    if (!clock_get(cpu->clock)) {
#ifndef CONFIG_USER_ONLY
        if (!qtest_enabled()) {
            g_autofree char *cpu_freq_str = freq_to_str(CPU_FREQ_HZ_DEFAULT);

            warn_report("CPU input clock is not connected to any output clock, "
                        "using default frequency of %s.", cpu_freq_str);
        }
#endif
        /* Initialize the frequency in case the clock remains unconnected. */
        clock_set_hz(cpu->clock, CPU_FREQ_HZ_DEFAULT);
    }
    mips_cp0_period_set(cpu);

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    env->exception_base = (int32_t)0xBFC00000;

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
    mmu_init(env, env->cpu_model);
#endif
    fpu_init(env, env->cpu_model);
    mvp_init(env);

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    mcc->parent_realize(dev, errp);
}

static void mips_cpu_initfn(Object *obj)
{
    MIPSCPU *cpu = MIPS_CPU(obj);
    CPUMIPSState *env = &cpu->env;
    MIPSCPUClass *mcc = MIPS_CPU_GET_CLASS(obj);

    cpu_set_cpustate_pointers(cpu);
    cpu->clock = qdev_init_clock_in(DEVICE(obj), "clk-in", NULL, cpu, 0);
    env->cpu_model = mcc->cpu_def;
}

static char *mips_cpu_type_name(const char *cpu_model)
{
    return g_strdup_printf(MIPS_CPU_TYPE_NAME("%s"), cpu_model);
}

static ObjectClass *mips_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = mips_cpu_type_name(cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

static Property mips_cpu_properties[] = {
    /* CP0 timer running at half the clock of the CPU */
    DEFINE_PROP_UINT32("cp0-count-rate", MIPSCPU, cp0_count_rate,
                       CP0_COUNT_RATE_DEFAULT),
    DEFINE_PROP_END_OF_LIST()
};

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps mips_sysemu_ops = {
    .get_phys_page_debug = mips_cpu_get_phys_page_debug,
    .legacy_vmsd = &vmstate_mips_cpu,
};
#endif

#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
/*
 * NB: cannot be const, as some elements are changed for specific
 * mips hardware (see hw/mips/jazz.c).
 */
static const struct TCGCPUOps mips_tcg_ops = {
    .initialize = mips_tcg_init,
    .synchronize_from_tb = mips_cpu_synchronize_from_tb,

#if !defined(CONFIG_USER_ONLY)
    .tlb_fill = mips_cpu_tlb_fill,
    .cpu_exec_interrupt = mips_cpu_exec_interrupt,
    .do_interrupt = mips_cpu_do_interrupt,
    .do_transaction_failed = mips_cpu_do_transaction_failed,
    .do_unaligned_access = mips_cpu_do_unaligned_access,
    .io_recompile_replay_branch = mips_io_recompile_replay_branch,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void mips_cpu_class_init(ObjectClass *c, void *data)
{
    MIPSCPUClass *mcc = MIPS_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    device_class_set_parent_realize(dc, mips_cpu_realizefn,
                                    &mcc->parent_realize);
    device_class_set_parent_reset(dc, mips_cpu_reset, &mcc->parent_reset);
    device_class_set_props(dc, mips_cpu_properties);

    cc->class_by_name = mips_cpu_class_by_name;
    cc->has_work = mips_cpu_has_work;
    cc->dump_state = mips_cpu_dump_state;
    cc->set_pc = mips_cpu_set_pc;
    cc->gdb_read_register = mips_cpu_gdb_read_register;
    cc->gdb_write_register = mips_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &mips_sysemu_ops;
#endif
    cc->disas_set_info = mips_cpu_disas_set_info;
    cc->gdb_num_core_regs = 73;
    cc->gdb_stop_before_watchpoint = true;
#ifdef CONFIG_TCG
    cc->tcg_ops = &mips_tcg_ops;
#endif /* CONFIG_TCG */
}

static const TypeInfo mips_cpu_type_info = {
    .name = TYPE_MIPS_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(MIPSCPU),
    .instance_init = mips_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(MIPSCPUClass),
    .class_init = mips_cpu_class_init,
};

static void mips_cpu_cpudef_class_init(ObjectClass *oc, void *data)
{
    MIPSCPUClass *mcc = MIPS_CPU_CLASS(oc);
    mcc->cpu_def = data;
}

static void mips_register_cpudef_type(const struct mips_def_t *def)
{
    char *typename = mips_cpu_type_name(def->name);
    TypeInfo ti = {
        .name = typename,
        .parent = TYPE_MIPS_CPU,
        .class_init = mips_cpu_cpudef_class_init,
        .class_data = (void *)def,
    };

    type_register(&ti);
    g_free(typename);
}

static void mips_cpu_register_types(void)
{
    int i;

    type_register_static(&mips_cpu_type_info);
    for (i = 0; i < mips_defs_number; i++) {
        mips_register_cpudef_type(&mips_defs[i]);
    }
}

type_init(mips_cpu_register_types)

static void mips_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfo *info;
    const char *typename;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = g_strndup(typename,
                           strlen(typename) - strlen("-" TYPE_MIPS_CPU));
    info->q_typename = g_strdup(typename);

    QAPI_LIST_PREPEND(*cpu_list, info);
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;

    list = object_class_get_list(TYPE_MIPS_CPU, false);
    g_slist_foreach(list, mips_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}

/* Could be used by generic CPU object */
MIPSCPU *mips_cpu_create_with_clock(const char *cpu_type, Clock *cpu_refclk)
{
    DeviceState *cpu;

    cpu = DEVICE(object_new(cpu_type));
    qdev_connect_clock_in(cpu, "clk-in", cpu_refclk);
    qdev_realize(cpu, NULL, &error_abort);

    return MIPS_CPU(cpu);
}

bool cpu_supports_isa(const CPUMIPSState *env, uint64_t isa_mask)
{
    return (env->cpu_model->insn_flags & isa_mask) != 0;
}

bool cpu_type_supports_isa(const char *cpu_type, uint64_t isa)
{
    const MIPSCPUClass *mcc = MIPS_CPU_CLASS(object_class_by_name(cpu_type));
    return (mcc->cpu_def->insn_flags & isa) != 0;
}

bool cpu_type_supports_cps_smp(const char *cpu_type)
{
    const MIPSCPUClass *mcc = MIPS_CPU_CLASS(object_class_by_name(cpu_type));
    return (mcc->cpu_def->CP0_Config3 & (1 << CP0C3_CMGCR)) != 0;
}
