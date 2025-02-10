/*
 *  PowerPC exception emulation helpers for QEMU.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "system/system.h"
#include "system/runstate.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "internal.h"
#include "helper_regs.h"
#include "hw/ppc/ppc.h"

#include "trace.h"

#ifdef CONFIG_TCG
#include "system/tcg.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#endif

/*****************************************************************************/
/* Exception processing */
#ifndef CONFIG_USER_ONLY

static const char *powerpc_excp_name(int excp)
{
    switch (excp) {
    case POWERPC_EXCP_CRITICAL: return "CRITICAL";
    case POWERPC_EXCP_MCHECK:   return "MCHECK";
    case POWERPC_EXCP_DSI:      return "DSI";
    case POWERPC_EXCP_ISI:      return "ISI";
    case POWERPC_EXCP_EXTERNAL: return "EXTERNAL";
    case POWERPC_EXCP_ALIGN:    return "ALIGN";
    case POWERPC_EXCP_PROGRAM:  return "PROGRAM";
    case POWERPC_EXCP_FPU:      return "FPU";
    case POWERPC_EXCP_SYSCALL:  return "SYSCALL";
    case POWERPC_EXCP_APU:      return "APU";
    case POWERPC_EXCP_DECR:     return "DECR";
    case POWERPC_EXCP_FIT:      return "FIT";
    case POWERPC_EXCP_WDT:      return "WDT";
    case POWERPC_EXCP_DTLB:     return "DTLB";
    case POWERPC_EXCP_ITLB:     return "ITLB";
    case POWERPC_EXCP_DEBUG:    return "DEBUG";
    case POWERPC_EXCP_SPEU:     return "SPEU";
    case POWERPC_EXCP_EFPDI:    return "EFPDI";
    case POWERPC_EXCP_EFPRI:    return "EFPRI";
    case POWERPC_EXCP_EPERFM:   return "EPERFM";
    case POWERPC_EXCP_DOORI:    return "DOORI";
    case POWERPC_EXCP_DOORCI:   return "DOORCI";
    case POWERPC_EXCP_GDOORI:   return "GDOORI";
    case POWERPC_EXCP_GDOORCI:  return "GDOORCI";
    case POWERPC_EXCP_HYPPRIV:  return "HYPPRIV";
    case POWERPC_EXCP_RESET:    return "RESET";
    case POWERPC_EXCP_DSEG:     return "DSEG";
    case POWERPC_EXCP_ISEG:     return "ISEG";
    case POWERPC_EXCP_HDECR:    return "HDECR";
    case POWERPC_EXCP_TRACE:    return "TRACE";
    case POWERPC_EXCP_HDSI:     return "HDSI";
    case POWERPC_EXCP_HISI:     return "HISI";
    case POWERPC_EXCP_HDSEG:    return "HDSEG";
    case POWERPC_EXCP_HISEG:    return "HISEG";
    case POWERPC_EXCP_VPU:      return "VPU";
    case POWERPC_EXCP_PIT:      return "PIT";
    case POWERPC_EXCP_EMUL:     return "EMUL";
    case POWERPC_EXCP_IFTLB:    return "IFTLB";
    case POWERPC_EXCP_DLTLB:    return "DLTLB";
    case POWERPC_EXCP_DSTLB:    return "DSTLB";
    case POWERPC_EXCP_FPA:      return "FPA";
    case POWERPC_EXCP_DABR:     return "DABR";
    case POWERPC_EXCP_IABR:     return "IABR";
    case POWERPC_EXCP_SMI:      return "SMI";
    case POWERPC_EXCP_PERFM:    return "PERFM";
    case POWERPC_EXCP_THERM:    return "THERM";
    case POWERPC_EXCP_VPUA:     return "VPUA";
    case POWERPC_EXCP_SOFTP:    return "SOFTP";
    case POWERPC_EXCP_MAINT:    return "MAINT";
    case POWERPC_EXCP_MEXTBR:   return "MEXTBR";
    case POWERPC_EXCP_NMEXTBR:  return "NMEXTBR";
    case POWERPC_EXCP_ITLBE:    return "ITLBE";
    case POWERPC_EXCP_DTLBE:    return "DTLBE";
    case POWERPC_EXCP_VSXU:     return "VSXU";
    case POWERPC_EXCP_FU:       return "FU";
    case POWERPC_EXCP_HV_EMU:   return "HV_EMU";
    case POWERPC_EXCP_HV_MAINT: return "HV_MAINT";
    case POWERPC_EXCP_HV_FU:    return "HV_FU";
    case POWERPC_EXCP_SDOOR:    return "SDOOR";
    case POWERPC_EXCP_SDOOR_HV: return "SDOOR_HV";
    case POWERPC_EXCP_HVIRT:    return "HVIRT";
    case POWERPC_EXCP_SYSCALL_VECTORED: return "SYSCALL_VECTORED";
    default:
        g_assert_not_reached();
    }
}

static void dump_syscall(CPUPPCState *env)
{
    qemu_log_mask(CPU_LOG_INT, "syscall r0=%016" PRIx64
                  " r3=%016" PRIx64 " r4=%016" PRIx64 " r5=%016" PRIx64
                  " r6=%016" PRIx64 " r7=%016" PRIx64 " r8=%016" PRIx64
                  " nip=" TARGET_FMT_lx "\n",
                  ppc_dump_gpr(env, 0), ppc_dump_gpr(env, 3),
                  ppc_dump_gpr(env, 4), ppc_dump_gpr(env, 5),
                  ppc_dump_gpr(env, 6), ppc_dump_gpr(env, 7),
                  ppc_dump_gpr(env, 8), env->nip);
}

static void dump_hcall(CPUPPCState *env)
{
    qemu_log_mask(CPU_LOG_INT, "hypercall r3=%016" PRIx64
                  " r4=%016" PRIx64 " r5=%016" PRIx64 " r6=%016" PRIx64
                  " r7=%016" PRIx64 " r8=%016" PRIx64 " r9=%016" PRIx64
                  " r10=%016" PRIx64 " r11=%016" PRIx64 " r12=%016" PRIx64
                  " nip=" TARGET_FMT_lx "\n",
                  ppc_dump_gpr(env, 3), ppc_dump_gpr(env, 4),
                  ppc_dump_gpr(env, 5), ppc_dump_gpr(env, 6),
                  ppc_dump_gpr(env, 7), ppc_dump_gpr(env, 8),
                  ppc_dump_gpr(env, 9), ppc_dump_gpr(env, 10),
                  ppc_dump_gpr(env, 11), ppc_dump_gpr(env, 12),
                  env->nip);
}

#ifdef CONFIG_TCG
/* Return true iff byteswap is needed to load instruction */
static inline bool insn_need_byteswap(CPUArchState *env)
{
    /* SYSTEM builds TARGET_BIG_ENDIAN. Need to swap when MSR[LE] is set */
    return !!(env->msr & ((target_ulong)1 << MSR_LE));
}

static uint32_t ppc_ldl_code(CPUArchState *env, target_ulong addr)
{
    uint32_t insn = cpu_ldl_code(env, addr);

    if (insn_need_byteswap(env)) {
        insn = bswap32(insn);
    }

    return insn;
}

#endif

static void ppc_excp_debug_sw_tlb(CPUPPCState *env, int excp)
{
    const char *es;
    target_ulong *miss, *cmp;
    int en;

    if (!qemu_loglevel_mask(CPU_LOG_MMU)) {
        return;
    }

    if (excp == POWERPC_EXCP_IFTLB) {
        es = "I";
        en = 'I';
        miss = &env->spr[SPR_IMISS];
        cmp = &env->spr[SPR_ICMP];
    } else {
        if (excp == POWERPC_EXCP_DLTLB) {
            es = "DL";
        } else {
            es = "DS";
        }
        en = 'D';
        miss = &env->spr[SPR_DMISS];
        cmp = &env->spr[SPR_DCMP];
    }
    qemu_log("6xx %sTLB miss: %cM " TARGET_FMT_lx " %cC "
             TARGET_FMT_lx " H1 " TARGET_FMT_lx " H2 "
             TARGET_FMT_lx " %08x\n", es, en, *miss, en, *cmp,
             env->spr[SPR_HASH1], env->spr[SPR_HASH2],
             env->error_code);
}

#ifdef TARGET_PPC64
static int powerpc_reset_wakeup(CPUPPCState *env, int excp, target_ulong *msr)
{
    /* We no longer are in a PM state */
    env->resume_as_sreset = false;

    /* Pretend to be returning from doze always as we don't lose state */
    *msr |= SRR1_WS_NOLOSS;

    /* Machine checks are sent normally */
    if (excp == POWERPC_EXCP_MCHECK) {
        return excp;
    }
    switch (excp) {
    case POWERPC_EXCP_RESET:
        *msr |= SRR1_WAKERESET;
        break;
    case POWERPC_EXCP_EXTERNAL:
        *msr |= SRR1_WAKEEE;
        break;
    case POWERPC_EXCP_DECR:
        *msr |= SRR1_WAKEDEC;
        break;
    case POWERPC_EXCP_SDOOR:
        *msr |= SRR1_WAKEDBELL;
        break;
    case POWERPC_EXCP_SDOOR_HV:
        *msr |= SRR1_WAKEHDBELL;
        break;
    case POWERPC_EXCP_HV_MAINT:
        *msr |= SRR1_WAKEHMI;
        break;
    case POWERPC_EXCP_HVIRT:
        *msr |= SRR1_WAKEHVI;
        break;
    default:
        cpu_abort(env_cpu(env),
                  "Unsupported exception %d in Power Save mode\n", excp);
    }
    return POWERPC_EXCP_RESET;
}

/*
 * AIL - Alternate Interrupt Location, a mode that allows interrupts to be
 * taken with the MMU on, and which uses an alternate location (e.g., so the
 * kernel/hv can map the vectors there with an effective address).
 *
 * An interrupt is considered to be taken "with AIL" or "AIL applies" if they
 * are delivered in this way. AIL requires the LPCR to be set to enable this
 * mode, and then a number of conditions have to be true for AIL to apply.
 *
 * First of all, SRESET, MCE, and HMI are always delivered without AIL, because
 * they specifically want to be in real mode (e.g., the MCE might be signaling
 * a SLB multi-hit which requires SLB flush before the MMU can be enabled).
 *
 * After that, behaviour depends on the current MSR[IR], MSR[DR], MSR[HV],
 * whether or not the interrupt changes MSR[HV] from 0 to 1, and the current
 * radix mode (LPCR[HR]).
 *
 * POWER8, POWER9 with LPCR[HR]=0
 * | LPCR[AIL] | MSR[IR||DR] | MSR[HV] | new MSR[HV] | AIL |
 * +-----------+-------------+---------+-------------+-----+
 * | a         | 00/01/10    | x       | x           | 0   |
 * | a         | 11          | 0       | 1           | 0   |
 * | a         | 11          | 1       | 1           | a   |
 * | a         | 11          | 0       | 0           | a   |
 * +-------------------------------------------------------+
 *
 * POWER9 with LPCR[HR]=1
 * | LPCR[AIL] | MSR[IR||DR] | MSR[HV] | new MSR[HV] | AIL |
 * +-----------+-------------+---------+-------------+-----+
 * | a         | 00/01/10    | x       | x           | 0   |
 * | a         | 11          | x       | x           | a   |
 * +-------------------------------------------------------+
 *
 * The difference with POWER9 being that MSR[HV] 0->1 interrupts can be sent to
 * the hypervisor in AIL mode if the guest is radix. This is good for
 * performance but allows the guest to influence the AIL of hypervisor
 * interrupts using its MSR, and also the hypervisor must disallow guest
 * interrupts (MSR[HV] 0->0) from using AIL if the hypervisor does not want to
 * use AIL for its MSR[HV] 0->1 interrupts.
 *
 * POWER10 addresses those issues with a new LPCR[HAIL] bit that is applied to
 * interrupts that begin execution with MSR[HV]=1 (so both MSR[HV] 0->1 and
 * MSR[HV] 1->1).
 *
 * HAIL=1 is equivalent to AIL=3, for interrupts delivered with MSR[HV]=1.
 *
 * POWER10 behaviour is
 * | LPCR[AIL] | LPCR[HAIL] | MSR[IR||DR] | MSR[HV] | new MSR[HV] | AIL |
 * +-----------+------------+-------------+---------+-------------+-----+
 * | a         | h          | 00/01/10    | 0       | 0           | 0   |
 * | a         | h          | 11          | 0       | 0           | a   |
 * | a         | h          | x           | 0       | 1           | h   |
 * | a         | h          | 00/01/10    | 1       | 1           | 0   |
 * | a         | h          | 11          | 1       | 1           | h   |
 * +--------------------------------------------------------------------+
 */
static void ppc_excp_apply_ail(PowerPCCPU *cpu, int excp, target_ulong msr,
                               target_ulong *new_msr, target_ulong *vector)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;
    bool mmu_all_on = ((msr >> MSR_IR) & 1) && ((msr >> MSR_DR) & 1);
    bool hv_escalation = !(msr & MSR_HVB) && (*new_msr & MSR_HVB);
    int ail = 0;

    if (excp == POWERPC_EXCP_MCHECK ||
        excp == POWERPC_EXCP_RESET ||
        excp == POWERPC_EXCP_HV_MAINT) {
        /* SRESET, MCE, HMI never apply AIL */
        return;
    }

    if (!(pcc->lpcr_mask & LPCR_AIL)) {
        /* This CPU does not have AIL */
        return;
    }

    /* P8 & P9 */
    if (!(pcc->lpcr_mask & LPCR_HAIL)) {
        if (!mmu_all_on) {
            /* AIL only works if MSR[IR] and MSR[DR] are both enabled. */
            return;
        }
        if (hv_escalation && !(env->spr[SPR_LPCR] & LPCR_HR)) {
            /*
             * AIL does not work if there is a MSR[HV] 0->1 transition and the
             * partition is in HPT mode. For radix guests, such interrupts are
             * allowed to be delivered to the hypervisor in ail mode.
             */
            return;
        }

        ail = (env->spr[SPR_LPCR] & LPCR_AIL) >> LPCR_AIL_SHIFT;
        if (ail == 0 || ail == 1) {
            /* AIL=1 is reserved, treat it like AIL=0 */
            return;
        }

    /* P10 and up */
    } else {
        if (!mmu_all_on && !hv_escalation) {
            /*
             * AIL works for HV interrupts even with guest MSR[IR/DR] disabled.
             * Guest->guest and HV->HV interrupts do require MMU on.
             */
            return;
        }

        if (*new_msr & MSR_HVB) {
            if (!(env->spr[SPR_LPCR] & LPCR_HAIL)) {
                /* HV interrupts depend on LPCR[HAIL] */
                return;
            }
            ail = 3; /* HAIL=1 gives AIL=3 behaviour for HV interrupts */
        } else {
            ail = (env->spr[SPR_LPCR] & LPCR_AIL) >> LPCR_AIL_SHIFT;
        }
        if (ail == 0 || ail == 1 || ail == 2) {
            /* AIL=1 and AIL=2 are reserved, treat them like AIL=0 */
            return;
        }
    }

    /*
     * AIL applies, so the new MSR gets IR and DR set, and an offset applied
     * to the new IP.
     */
    *new_msr |= (1 << MSR_IR) | (1 << MSR_DR);

    if (excp != POWERPC_EXCP_SYSCALL_VECTORED) {
        if (ail == 2) {
            *vector |= 0x0000000000018000ull;
        } else if (ail == 3) {
            *vector |= 0xc000000000004000ull;
        }
    } else {
        /*
         * scv AIL is a little different. AIL=2 does not change the address,
         * only the MSR. AIL=3 replaces the 0x17000 base with 0xc...3000.
         */
        if (ail == 3) {
            *vector &= ~0x0000000000017000ull; /* Un-apply the base offset */
            *vector |= 0xc000000000003000ull; /* Apply scv's AIL=3 offset */
        }
    }
}
#endif /* TARGET_PPC64 */

static void powerpc_reset_excp_state(PowerPCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUPPCState *env = &cpu->env;

    /* Reset exception state */
    cs->exception_index = POWERPC_EXCP_NONE;
    env->error_code = 0;
}

static void powerpc_set_excp_state(PowerPCCPU *cpu, target_ulong vector,
                                   target_ulong msr)
{
    CPUPPCState *env = &cpu->env;

    assert((msr & env->msr_mask) == msr);

    /*
     * We don't use hreg_store_msr here as already have treated any
     * special case that could occur. Just store MSR and update hflags
     *
     * Note: We *MUST* not use hreg_store_msr() as-is anyway because it will
     * prevent setting of the HV bit which some exceptions might need to do.
     */
    env->nip = vector;
    env->msr = msr;
    hreg_compute_hflags(env);
    ppc_maybe_interrupt(env);

    powerpc_reset_excp_state(cpu);

    /*
     * Any interrupt is context synchronizing, check if TCG TLB needs
     * a delayed flush on ppc64
     */
    check_tlb_flush(env, false);

    /* Reset the reservation */
    env->reserve_addr = -1;
}

#ifdef CONFIG_TCG
/*
 * This stops the machine and logs CPU state without killing QEMU (like
 * cpu_abort()) because it is often a guest error as opposed to a QEMU error,
 * so the machine can still be debugged.
 */
static G_NORETURN void powerpc_checkstop(CPUPPCState *env, const char *reason)
{
    CPUState *cs = env_cpu(env);
    FILE *f;

    f = qemu_log_trylock();
    if (f) {
        fprintf(f, "Entering checkstop state: %s\n", reason);
        cpu_dump_state(cs, f, CPU_DUMP_FPU | CPU_DUMP_CCOP);
        qemu_log_unlock(f);
    }

    /*
     * This stops the machine and logs CPU state without killing QEMU
     * (like cpu_abort()) so the machine can still be debugged (because
     * it is often a guest error).
     */
    qemu_system_guest_panicked(NULL);
    cpu_loop_exit_noexc(cs);
}

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)
void helper_attn(CPUPPCState *env)
{
    /* POWER attn is unprivileged when enabled by HID, otherwise illegal */
    if ((*env->check_attn)(env)) {
        powerpc_checkstop(env, "host executed attn");
    } else {
        raise_exception_err(env, POWERPC_EXCP_HV_EMU,
                            POWERPC_EXCP_INVAL | POWERPC_EXCP_INVAL_INVAL);
    }
}
#endif
#endif /* CONFIG_TCG */

static void powerpc_mcheck_checkstop(CPUPPCState *env)
{
    /* KVM guests always have MSR[ME] enabled */
#ifdef CONFIG_TCG
    if (FIELD_EX64(env->msr, MSR, ME)) {
        return;
    }

    powerpc_checkstop(env, "machine check with MSR[ME]=0");
#endif
}

static void powerpc_excp_40x(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    int srr0 = SPR_SRR0, srr1 = SPR_SRR1;

    /* new srr1 value excluding must-be-zero bits */
    msr = env->msr & ~0x783f0000ULL;

    /* new interrupt handler msr preserves ME unless explicitly overridden */
    new_msr = env->msr & (((target_ulong)1 << MSR_ME));

    /* HV emu assistance interrupt only exists on server arch 2.05 or later */
    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_CRITICAL:    /* Critical input                         */
        srr0 = SPR_40x_SRR2;
        srr1 = SPR_40x_SRR3;
        break;
    case POWERPC_EXCP_MCHECK:    /* Machine check exception                  */
        powerpc_mcheck_checkstop(env);
        /* machine check exceptions don't have ME set */
        new_msr &= ~((target_ulong)1 << MSR_ME);
        srr0 = SPR_40x_SRR2;
        srr1 = SPR_40x_SRR3;
        break;
    case POWERPC_EXCP_DSI:       /* Data storage exception                   */
        trace_ppc_excp_dsi(env->spr[SPR_40x_ESR], env->spr[SPR_40x_DEAR]);
        break;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        trace_ppc_excp_isi(msr, env->nip);
        break;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        break;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        break;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                return;
            }
            env->spr[SPR_40x_ESR] = ESR_FP;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            env->spr[SPR_40x_ESR] = ESR_PIL;
            break;
        case POWERPC_EXCP_PRIV:
            env->spr[SPR_40x_ESR] = ESR_PPR;
            break;
        case POWERPC_EXCP_TRAP:
            env->spr[SPR_40x_ESR] = ESR_PTR;
            break;
        default:
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
        dump_syscall(env);

        /*
         * We need to correct the NIP which in this case is supposed
         * to point to the next instruction
         */
        env->nip += 4;
        break;
    case POWERPC_EXCP_FIT:       /* Fixed-interval timer interrupt           */
        trace_ppc_excp_print("FIT");
        break;
    case POWERPC_EXCP_WDT:       /* Watchdog timer interrupt                 */
        trace_ppc_excp_print("WDT");
        break;
    case POWERPC_EXCP_DTLB:      /* Data TLB error                           */
    case POWERPC_EXCP_ITLB:      /* Instruction TLB error                    */
        break;
    case POWERPC_EXCP_PIT:       /* Programmable interval timer interrupt    */
        trace_ppc_excp_print("PIT");
        break;
    case POWERPC_EXCP_DEBUG:     /* Debug interrupt                          */
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    env->spr[srr0] = env->nip;
    env->spr[srr1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
}

static void powerpc_excp_6xx(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;

    /* new srr1 value excluding must-be-zero bits */
    msr = env->msr & ~0x783f0000ULL;

    /* new interrupt handler msr preserves ME unless explicitly overridden */
    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    /* HV emu assistance interrupt only exists on server arch 2.05 or later */
    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_CRITICAL:    /* Critical input                         */
        break;
    case POWERPC_EXCP_MCHECK:    /* Machine check exception                  */
        powerpc_mcheck_checkstop(env);
        /* machine check exceptions don't have ME set */
        new_msr &= ~((target_ulong)1 << MSR_ME);
        break;
    case POWERPC_EXCP_DSI:       /* Data storage exception                   */
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        break;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        /* Get rS/rD and rA from faulting opcode */
        /*
         * Note: the opcode fields will not be set properly for a
         * direct store load/store, but nobody cares as nobody
         * actually uses direct store segments.
         */
        env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        break;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                return;
            }
            /*
             * NIP always points to the faulting instruction for FP exceptions,
             * so always use store_next and claim we are precise in the MSR.
             */
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
        dump_syscall(env);

        /*
         * We need to correct the NIP which in this case is supposed
         * to point to the next instruction
         */
        env->nip += 4;
        break;
    case POWERPC_EXCP_FPU:       /* Floating-point unavailable exception     */
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        break;
    case POWERPC_EXCP_DTLB:      /* Data TLB error                           */
    case POWERPC_EXCP_ITLB:      /* Instruction TLB error                    */
        break;
    case POWERPC_EXCP_RESET:     /* System reset exception                   */
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset exception "
                      "%d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_TRACE:     /* Trace exception                          */
        break;
    case POWERPC_EXCP_IFTLB:     /* Instruction fetch TLB error              */
    case POWERPC_EXCP_DLTLB:     /* Data load TLB miss                       */
    case POWERPC_EXCP_DSTLB:     /* Data store TLB miss                      */
        /* Swap temporary saved registers with GPRs */
        if (!(new_msr & ((target_ulong)1 << MSR_TGPR))) {
            new_msr |= (target_ulong)1 << MSR_TGPR;
            hreg_swap_gpr_tgpr(env);
        }

        ppc_excp_debug_sw_tlb(env, excp);

        msr |= env->crf[0] << 28;
        msr |= env->error_code; /* key, D/I, S/L bits */
        /* Set way using a LRU mechanism */
        msr |= ((env->last_way + 1) & (env->nb_ways - 1)) << 17;
        break;
    case POWERPC_EXCP_FPA:       /* Floating-point assist exception          */
    case POWERPC_EXCP_DABR:      /* Data address breakpoint                  */
    case POWERPC_EXCP_IABR:      /* Instruction address breakpoint           */
    case POWERPC_EXCP_SMI:       /* System management interrupt              */
    case POWERPC_EXCP_MEXTBR:    /* Maskable external breakpoint             */
    case POWERPC_EXCP_NMEXTBR:   /* Non maskable external breakpoint         */
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    env->spr[SPR_SRR0] = env->nip;
    env->spr[SPR_SRR1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
}

static void powerpc_excp_7xx(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;

    /* new srr1 value excluding must-be-zero bits */
    msr = env->msr & ~0x783f0000ULL;

    /* new interrupt handler msr preserves ME unless explicitly overridden */
    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    /* HV emu assistance interrupt only exists on server arch 2.05 or later */
    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_MCHECK:    /* Machine check exception                  */
        powerpc_mcheck_checkstop(env);
        /* machine check exceptions don't have ME set */
        new_msr &= ~((target_ulong)1 << MSR_ME);
        break;
    case POWERPC_EXCP_DSI:       /* Data storage exception                   */
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        break;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        /* Get rS/rD and rA from faulting opcode */
        /*
         * Note: the opcode fields will not be set properly for a
         * direct store load/store, but nobody cares as nobody
         * actually uses direct store segments.
         */
        env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        break;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                return;
            }
            /*
             * NIP always points to the faulting instruction for FP exceptions,
             * so always use store_next and claim we are precise in the MSR.
             */
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
    {
        int lev = env->error_code;

        if (lev == 1 && cpu->vhyp) {
            dump_hcall(env);
        } else {
            dump_syscall(env);
        }

        /*
         * We need to correct the NIP which in this case is supposed
         * to point to the next instruction
         */
        env->nip += 4;

        /*
         * The Virtual Open Firmware (VOF) relies on the 'sc 1'
         * instruction to communicate with QEMU. The pegasos2 machine
         * uses VOF and the 7xx CPUs, so although the 7xx don't have
         * HV mode, we need to keep hypercall support.
         */
        if (lev == 1 && cpu->vhyp) {
            cpu->vhyp_class->hypercall(cpu->vhyp, cpu);
            powerpc_reset_excp_state(cpu);
            return;
        }

        break;
    }
    case POWERPC_EXCP_FPU:       /* Floating-point unavailable exception     */
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        break;
    case POWERPC_EXCP_RESET:     /* System reset exception                   */
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset exception "
                      "%d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_TRACE:     /* Trace exception                          */
        break;
    case POWERPC_EXCP_IFTLB:     /* Instruction fetch TLB error              */
    case POWERPC_EXCP_DLTLB:     /* Data load TLB miss                       */
    case POWERPC_EXCP_DSTLB:     /* Data store TLB miss                      */
        ppc_excp_debug_sw_tlb(env, excp);
        msr |= env->crf[0] << 28;
        msr |= env->error_code; /* key, D/I, S/L bits */
        /* Set way using a LRU mechanism */
        msr |= ((env->last_way + 1) & (env->nb_ways - 1)) << 17;
        break;
    case POWERPC_EXCP_IABR:      /* Instruction address breakpoint           */
    case POWERPC_EXCP_SMI:       /* System management interrupt              */
    case POWERPC_EXCP_THERM:     /* Thermal interrupt                        */
    case POWERPC_EXCP_PERFM:     /* Embedded performance monitor interrupt   */
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    env->spr[SPR_SRR0] = env->nip;
    env->spr[SPR_SRR1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
}

static void powerpc_excp_74xx(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;

    /* new srr1 value excluding must-be-zero bits */
    msr = env->msr & ~0x783f0000ULL;

    /* new interrupt handler msr preserves ME unless explicitly overridden */
    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    /* HV emu assistance interrupt only exists on server arch 2.05 or later */
    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_MCHECK:    /* Machine check exception                  */
        powerpc_mcheck_checkstop(env);
        /* machine check exceptions don't have ME set */
        new_msr &= ~((target_ulong)1 << MSR_ME);
        break;
    case POWERPC_EXCP_DSI:       /* Data storage exception                   */
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        break;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        /* Get rS/rD and rA from faulting opcode */
        /*
         * Note: the opcode fields will not be set properly for a
         * direct store load/store, but nobody cares as nobody
         * actually uses direct store segments.
         */
        env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        break;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                return;
            }
            /*
             * NIP always points to the faulting instruction for FP exceptions,
             * so always use store_next and claim we are precise in the MSR.
             */
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
    {
        int lev = env->error_code;

        if (lev == 1 && cpu->vhyp) {
            dump_hcall(env);
        } else {
            dump_syscall(env);
        }

        /*
         * We need to correct the NIP which in this case is supposed
         * to point to the next instruction
         */
        env->nip += 4;

        /*
         * The Virtual Open Firmware (VOF) relies on the 'sc 1'
         * instruction to communicate with QEMU. The pegasos2 machine
         * uses VOF and the 74xx CPUs, so although the 74xx don't have
         * HV mode, we need to keep hypercall support.
         */
        if (lev == 1 && cpu->vhyp) {
            cpu->vhyp_class->hypercall(cpu->vhyp, cpu);
            powerpc_reset_excp_state(cpu);
            return;
        }

        break;
    }
    case POWERPC_EXCP_FPU:       /* Floating-point unavailable exception     */
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        break;
    case POWERPC_EXCP_RESET:     /* System reset exception                   */
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset "
                      "exception %d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_TRACE:     /* Trace exception                          */
        break;
    case POWERPC_EXCP_VPU:       /* Vector unavailable exception             */
        break;
    case POWERPC_EXCP_IABR:      /* Instruction address breakpoint           */
    case POWERPC_EXCP_SMI:       /* System management interrupt              */
    case POWERPC_EXCP_THERM:     /* Thermal interrupt                        */
    case POWERPC_EXCP_PERFM:     /* Embedded performance monitor interrupt   */
    case POWERPC_EXCP_VPUA:      /* Vector assist exception                  */
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    env->spr[SPR_SRR0] = env->nip;
    env->spr[SPR_SRR1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
}

static void powerpc_excp_booke(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    int srr0 = SPR_SRR0, srr1 = SPR_SRR1;

    /*
     * Book E does not play games with certain bits of xSRR1 being MSR save
     * bits and others being error status. xSRR1 is the old MSR, period.
     */
    msr = env->msr;

    /* new interrupt handler msr preserves ME unless explicitly overridden */
    new_msr = env->msr & ((target_ulong)1 << MSR_ME);

    /* HV emu assistance interrupt only exists on server arch 2.05 or later */
    if (excp == POWERPC_EXCP_HV_EMU) {
        excp = POWERPC_EXCP_PROGRAM;
    }

#ifdef TARGET_PPC64
    /*
     * SPEU and VPU share the same IVOR but they exist in different
     * processors. SPEU is e500v1/2 only and VPU is e6500 only.
     */
    if (excp == POWERPC_EXCP_VPU) {
        excp = POWERPC_EXCP_SPEU;
    }
#endif

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    switch (excp) {
    case POWERPC_EXCP_CRITICAL:    /* Critical input                         */
        srr0 = SPR_BOOKE_CSRR0;
        srr1 = SPR_BOOKE_CSRR1;
        break;
    case POWERPC_EXCP_MCHECK:    /* Machine check exception                  */
        powerpc_mcheck_checkstop(env);
        /* machine check exceptions don't have ME set */
        new_msr &= ~((target_ulong)1 << MSR_ME);

        /* FIXME: choose one or the other based on CPU type */
        srr0 = SPR_BOOKE_MCSRR0;
        srr1 = SPR_BOOKE_MCSRR1;

        env->spr[SPR_BOOKE_CSRR0] = env->nip;
        env->spr[SPR_BOOKE_CSRR1] = msr;

        break;
    case POWERPC_EXCP_DSI:       /* Data storage exception                   */
        trace_ppc_excp_dsi(env->spr[SPR_BOOKE_ESR], env->spr[SPR_BOOKE_DEAR]);
        break;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        trace_ppc_excp_isi(msr, env->nip);
        break;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
        if (env->mpic_proxy) {
            CPUState *cs = env_cpu(env);
            /* IACK the IRQ on delivery */
            env->spr[SPR_BOOKE_EPR] = ldl_phys(cs->as, env->mpic_iack);
        }
        break;
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        break;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                return;
            }
            /*
             * NIP always points to the faulting instruction for FP exceptions,
             * so always use store_next and claim we are precise in the MSR.
             */
            msr |= 0x00100000;
            env->spr[SPR_BOOKE_ESR] = ESR_FP;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            env->spr[SPR_BOOKE_ESR] = ESR_PIL;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            env->spr[SPR_BOOKE_ESR] = ESR_PPR;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            env->spr[SPR_BOOKE_ESR] = ESR_PTR;
            break;
        default:
            /* Should never occur */
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
        dump_syscall(env);

        /*
         * We need to correct the NIP which in this case is supposed
         * to point to the next instruction
         */
        env->nip += 4;
        break;
    case POWERPC_EXCP_FPU:       /* Floating-point unavailable exception     */
    case POWERPC_EXCP_APU:       /* Auxiliary processor unavailable          */
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        break;
    case POWERPC_EXCP_FIT:       /* Fixed-interval timer interrupt           */
        /* FIT on 4xx */
        trace_ppc_excp_print("FIT");
        break;
    case POWERPC_EXCP_WDT:       /* Watchdog timer interrupt                 */
        trace_ppc_excp_print("WDT");
        srr0 = SPR_BOOKE_CSRR0;
        srr1 = SPR_BOOKE_CSRR1;
        break;
    case POWERPC_EXCP_DTLB:      /* Data TLB error                           */
    case POWERPC_EXCP_ITLB:      /* Instruction TLB error                    */
        break;
    case POWERPC_EXCP_DEBUG:     /* Debug interrupt                          */
        if (env->flags & POWERPC_FLAG_DE) {
            /* FIXME: choose one or the other based on CPU type */
            srr0 = SPR_BOOKE_DSRR0;
            srr1 = SPR_BOOKE_DSRR1;

            env->spr[SPR_BOOKE_CSRR0] = env->nip;
            env->spr[SPR_BOOKE_CSRR1] = msr;

            /* DBSR already modified by caller */
        } else {
            cpu_abort(env_cpu(env),
                      "Debug exception triggered on unsupported model\n");
        }
        break;
    case POWERPC_EXCP_SPEU:   /* SPE/embedded floating-point unavailable/VPU  */
        env->spr[SPR_BOOKE_ESR] = ESR_SPV;
        break;
    case POWERPC_EXCP_DOORI:     /* Embedded doorbell interrupt              */
        break;
    case POWERPC_EXCP_DOORCI:    /* Embedded doorbell critical interrupt     */
        srr0 = SPR_BOOKE_CSRR0;
        srr1 = SPR_BOOKE_CSRR1;
        break;
    case POWERPC_EXCP_RESET:     /* System reset exception                   */
        if (FIELD_EX64(env->msr, MSR, POW)) {
            cpu_abort(env_cpu(env),
                      "Trying to deliver power-saving system reset "
                      "exception %d with no HV support\n", excp);
        }
        break;
    case POWERPC_EXCP_EFPDI:     /* Embedded floating-point data interrupt   */
    case POWERPC_EXCP_EFPRI:     /* Embedded floating-point round interrupt  */
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

#ifdef TARGET_PPC64
    if (env->spr[SPR_BOOKE_EPCR] & EPCR_ICM) {
        /* Cat.64-bit: EPCR.ICM is copied to MSR.CM */
        new_msr |= (target_ulong)1 << MSR_CM;
    } else {
        vector = (uint32_t)vector;
    }
#endif

    env->spr[srr0] = env->nip;
    env->spr[srr1] = msr;
    powerpc_set_excp_state(cpu, vector, new_msr);
}

/*
 * When running a nested HV guest under vhyp, external interrupts are
 * delivered as HVIRT.
 */
static bool books_vhyp_promotes_external_to_hvirt(PowerPCCPU *cpu)
{
    if (cpu->vhyp) {
        return vhyp_cpu_in_nested(cpu);
    }
    return false;
}

#ifdef TARGET_PPC64
/*
 * When running under vhyp, hcalls are always intercepted and sent to the
 * vhc->hypercall handler.
 */
static bool books_vhyp_handles_hcall(PowerPCCPU *cpu)
{
    if (cpu->vhyp) {
        return !vhyp_cpu_in_nested(cpu);
    }
    return false;
}

/*
 * When running a nested KVM HV guest under vhyp, HV exceptions are not
 * delivered to the guest (because there is no concept of HV support), but
 * rather they are sent to the vhyp to exit from the L2 back to the L1 and
 * return from the H_ENTER_NESTED hypercall.
 */
static bool books_vhyp_handles_hv_excp(PowerPCCPU *cpu)
{
    if (cpu->vhyp) {
        return vhyp_cpu_in_nested(cpu);
    }
    return false;
}

#ifdef CONFIG_TCG
static bool is_prefix_insn(CPUPPCState *env, uint32_t insn)
{
    if (!(env->insns_flags2 & PPC2_ISA310)) {
        return false;
    }
    return ((insn & 0xfc000000) == 0x04000000);
}

static bool is_prefix_insn_excp(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;

    if (!(env->insns_flags2 & PPC2_ISA310)) {
        return false;
    }

    if (!tcg_enabled()) {
        /*
         * This does not load instructions and set the prefix bit correctly
         * for injected interrupts with KVM. That may have to be discovered
         * and set by the KVM layer before injecting.
         */
        return false;
    }

    switch (excp) {
    case POWERPC_EXCP_MCHECK:
        if (!(env->error_code & PPC_BIT(42))) {
            /*
             * Fetch attempt caused a machine check, so attempting to fetch
             * again would cause a recursive machine check.
             */
            return false;
        }
        break;
    case POWERPC_EXCP_HDSI:
        /* HDSI PRTABLE_FAULT has the originating access type in error_code */
        if ((env->spr[SPR_HDSISR] & DSISR_PRTABLE_FAULT) &&
            (env->error_code == MMU_INST_FETCH)) {
            /*
             * Fetch failed due to partition scope translation, so prefix
             * indication is not relevant (and attempting to load the
             * instruction at NIP would cause recursive faults with the same
             * translation).
             */
            return false;
        }
        break;

    case POWERPC_EXCP_DSI:
    case POWERPC_EXCP_DSEG:
    case POWERPC_EXCP_ALIGN:
    case POWERPC_EXCP_PROGRAM:
    case POWERPC_EXCP_FPU:
    case POWERPC_EXCP_TRACE:
    case POWERPC_EXCP_HV_EMU:
    case POWERPC_EXCP_VPU:
    case POWERPC_EXCP_VSXU:
    case POWERPC_EXCP_FU:
    case POWERPC_EXCP_HV_FU:
        break;
    default:
        return false;
    }

    return is_prefix_insn(env, ppc_ldl_code(env, env->nip));
}
#else
static bool is_prefix_insn_excp(PowerPCCPU *cpu, int excp)
{
    return false;
}
#endif

static void powerpc_excp_books(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;
    target_ulong msr, new_msr, vector;
    int srr0 = SPR_SRR0, srr1 = SPR_SRR1, lev = -1;

    /* new srr1 value excluding must-be-zero bits */
    msr = env->msr & ~0x783f0000ULL;

    /*
     * new interrupt handler msr preserves HV and ME unless explicitly
     * overridden
     */
    new_msr = env->msr & (((target_ulong)1 << MSR_ME) | MSR_HVB);

    /*
     * check for special resume at 0x100 from doze/nap/sleep/winkle on
     * P7/P8/P9
     */
    if (env->resume_as_sreset) {
        excp = powerpc_reset_wakeup(env, excp, &msr);
    }

    /*
     * We don't want to generate a Hypervisor Emulation Assistance
     * Interrupt if we don't have HVB in msr_mask (PAPR mode),
     * unless running a nested-hv guest, in which case the L1
     * kernel wants the interrupt.
     */
    if (excp == POWERPC_EXCP_HV_EMU && !(env->msr_mask & MSR_HVB) &&
            !books_vhyp_handles_hv_excp(cpu)) {
        excp = POWERPC_EXCP_PROGRAM;
    }

    vector = env->excp_vectors[excp];
    if (vector == (target_ulong)-1ULL) {
        cpu_abort(env_cpu(env),
                  "Raised an exception without defined vector %d\n", excp);
    }
    vector |= env->excp_prefix;

    if (is_prefix_insn_excp(cpu, excp)) {
        msr |= PPC_BIT(34);
    }

    switch (excp) {
    case POWERPC_EXCP_MCHECK:    /* Machine check exception                  */
        powerpc_mcheck_checkstop(env);
        if (env->msr_mask & MSR_HVB) {
            /*
             * ISA specifies HV, but can be delivered to guest with HV
             * clear (e.g., see FWNMI in PAPR).
             */
            new_msr |= (target_ulong)MSR_HVB;

            /* HV machine check exceptions don't have ME set */
            new_msr &= ~((target_ulong)1 << MSR_ME);
        }

        msr |= env->error_code;
        break;

    case POWERPC_EXCP_DSI:       /* Data storage exception                   */
        trace_ppc_excp_dsi(env->spr[SPR_DSISR], env->spr[SPR_DAR]);
        break;
    case POWERPC_EXCP_ISI:       /* Instruction storage exception            */
        trace_ppc_excp_isi(msr, env->nip);
        msr |= env->error_code;
        break;
    case POWERPC_EXCP_EXTERNAL:  /* External input                           */
    {
        bool lpes0;

        /* LPES0 is only taken into consideration if we support HV mode */
        if (!env->has_hv_mode) {
            break;
        }
        lpes0 = !!(env->spr[SPR_LPCR] & LPCR_LPES0);
        if (!lpes0) {
            new_msr |= (target_ulong)MSR_HVB;
            new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
            srr0 = SPR_HSRR0;
            srr1 = SPR_HSRR1;
        }
        break;
    }
    case POWERPC_EXCP_ALIGN:     /* Alignment exception                      */
        /* Optional DSISR update was removed from ISA v3.0 */
        if (!(env->insns_flags2 & PPC2_ISA300)) {
            /* Get rS/rD and rA from faulting opcode */
            /*
             * Note: the opcode fields will not be set properly for a
             * direct store load/store, but nobody cares as nobody
             * actually uses direct store segments.
             */
            env->spr[SPR_DSISR] |= (env->error_code & 0x03FF0000) >> 16;
        }
        break;
    case POWERPC_EXCP_PROGRAM:   /* Program exception                        */
        switch (env->error_code & ~0xF) {
        case POWERPC_EXCP_FP:
            if (!FIELD_EX64_FE(env->msr) || !FIELD_EX64(env->msr, MSR, FP)) {
                trace_ppc_excp_fp_ignore();
                powerpc_reset_excp_state(cpu);
                return;
            }
            /*
             * NIP always points to the faulting instruction for FP exceptions,
             * so always use store_next and claim we are precise in the MSR.
             */
            msr |= 0x00100000;
            break;
        case POWERPC_EXCP_INVAL:
            trace_ppc_excp_inval(env->nip);
            msr |= 0x00080000;
            break;
        case POWERPC_EXCP_PRIV:
            msr |= 0x00040000;
            break;
        case POWERPC_EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
            cpu_abort(env_cpu(env), "Invalid program exception %d. Aborting\n",
                      env->error_code);
            break;
        }
        break;
    case POWERPC_EXCP_SYSCALL:   /* System call exception                    */
        lev = env->error_code;

        if (lev == 1 && cpu->vhyp) {
            dump_hcall(env);
        } else {
            dump_syscall(env);
        }

        /*
         * We need to correct the NIP which in this case is supposed
         * to point to the next instruction
         */
        env->nip += 4;

        /* "PAPR mode" built-in hypercall emulation */
        if (lev == 1 && books_vhyp_handles_hcall(cpu)) {
            cpu->vhyp_class->hypercall(cpu->vhyp, cpu);
            powerpc_reset_excp_state(cpu);
            return;
        }
        if (env->insns_flags2 & PPC2_ISA310) {
            /* ISAv3.1 puts LEV into SRR1 */
            msr |= lev << 20;
        }
        if (lev == 1) {
            new_msr |= (target_ulong)MSR_HVB;
        }
        break;
    case POWERPC_EXCP_SYSCALL_VECTORED: /* scv exception                     */
        lev = env->error_code;
        dump_syscall(env);
        env->nip += 4;
        new_msr |= env->msr & ((target_ulong)1 << MSR_EE);
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);

        vector += lev * 0x20;

        env->lr = env->nip;
        env->ctr = msr;
        break;
    case POWERPC_EXCP_FPU:       /* Floating-point unavailable exception     */
    case POWERPC_EXCP_DECR:      /* Decrementer exception                    */
        break;
    case POWERPC_EXCP_RESET:     /* System reset exception                   */
        /* A power-saving exception sets ME, otherwise it is unchanged */
        if (FIELD_EX64(env->msr, MSR, POW)) {
            /* indicate that we resumed from power save mode */
            msr |= 0x10000;
            new_msr |= ((target_ulong)1 << MSR_ME);
        }
        if (env->msr_mask & MSR_HVB) {
            /*
             * ISA specifies HV, but can be delivered to guest with HV
             * clear (e.g., see FWNMI in PAPR, NMI injection in QEMU).
             */
            new_msr |= (target_ulong)MSR_HVB;
        } else {
            if (FIELD_EX64(env->msr, MSR, POW)) {
                cpu_abort(env_cpu(env),
                          "Trying to deliver power-saving system reset "
                          "exception %d with no HV support\n", excp);
            }
        }
        break;
    case POWERPC_EXCP_TRACE:     /* Trace exception                          */
        msr |= env->error_code;
        /* fall through */
    case POWERPC_EXCP_DSEG:      /* Data segment exception                   */
    case POWERPC_EXCP_ISEG:      /* Instruction segment exception            */
    case POWERPC_EXCP_SDOOR:     /* Doorbell interrupt                       */
    case POWERPC_EXCP_PERFM:     /* Performance monitor interrupt            */
        break;
    case POWERPC_EXCP_HISI:      /* Hypervisor instruction storage exception */
        msr |= env->error_code;
        /* fall through */
    case POWERPC_EXCP_HDECR:     /* Hypervisor decrementer exception         */
    case POWERPC_EXCP_HDSI:      /* Hypervisor data storage exception        */
    case POWERPC_EXCP_SDOOR_HV:  /* Hypervisor Doorbell interrupt            */
    case POWERPC_EXCP_HVIRT:     /* Hypervisor virtualization                */
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        break;
#ifdef CONFIG_TCG
    case POWERPC_EXCP_HV_EMU: {
        uint32_t insn = ppc_ldl_code(env, env->nip);
        env->spr[SPR_HEIR] = insn;
        if (is_prefix_insn(env, insn)) {
            uint32_t insn2 = ppc_ldl_code(env, env->nip + 4);
            env->spr[SPR_HEIR] <<= 32;
            env->spr[SPR_HEIR] |= insn2;
        }
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        break;
    }
#endif
    case POWERPC_EXCP_VPU:       /* Vector unavailable exception             */
    case POWERPC_EXCP_VSXU:       /* VSX unavailable exception               */
    case POWERPC_EXCP_FU:         /* Facility unavailable exception          */
        env->spr[SPR_FSCR] |= ((target_ulong)env->error_code << 56);
        break;
    case POWERPC_EXCP_HV_FU:     /* Hypervisor Facility Unavailable Exception */
        env->spr[SPR_HFSCR] |= ((target_ulong)env->error_code << FSCR_IC_POS);
        srr0 = SPR_HSRR0;
        srr1 = SPR_HSRR1;
        new_msr |= (target_ulong)MSR_HVB;
        new_msr |= env->msr & ((target_ulong)1 << MSR_RI);
        break;
    case POWERPC_EXCP_PERFM_EBB:        /* Performance Monitor EBB Exception  */
    case POWERPC_EXCP_EXTERNAL_EBB:     /* External EBB Exception             */
        env->spr[SPR_BESCR] &= ~BESCR_GE;

        /*
         * Save NIP for rfebb insn in SPR_EBBRR. Next nip is
         * stored in the EBB Handler SPR_EBBHR.
         */
        env->spr[SPR_EBBRR] = env->nip;
        powerpc_set_excp_state(cpu, env->spr[SPR_EBBHR], env->msr);

        /*
         * This exception is handled in userspace. No need to proceed.
         */
        return;
    case POWERPC_EXCP_THERM:     /* Thermal interrupt                        */
    case POWERPC_EXCP_VPUA:      /* Vector assist exception                  */
    case POWERPC_EXCP_MAINT:     /* Maintenance exception                    */
    case POWERPC_EXCP_HV_MAINT:  /* Hypervisor Maintenance exception         */
        cpu_abort(env_cpu(env), "%s exception not implemented\n",
                  powerpc_excp_name(excp));
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
        break;
    }

    if (ppc_interrupts_little_endian(cpu, !!(new_msr & MSR_HVB))) {
        new_msr |= (target_ulong)1 << MSR_LE;
    }
    new_msr |= (target_ulong)1 << MSR_SF;

    if (excp != POWERPC_EXCP_SYSCALL_VECTORED) {
        env->spr[srr0] = env->nip;
        env->spr[srr1] = msr;
    }

    if ((new_msr & MSR_HVB) && books_vhyp_handles_hv_excp(cpu)) {
        /* Deliver interrupt to L1 by returning from the H_ENTER_NESTED call */
        cpu->vhyp_class->deliver_hv_excp(cpu, excp);
        powerpc_reset_excp_state(cpu);
    } else {
        /* Sanity check */
        if (!(env->msr_mask & MSR_HVB) && srr0 == SPR_HSRR0) {
            cpu_abort(env_cpu(env), "Trying to deliver HV exception (HSRR) %d "
                      "with no HV support\n", excp);
        }
        /* This can update new_msr and vector if AIL applies */
        ppc_excp_apply_ail(cpu, excp, msr, &new_msr, &vector);
        powerpc_set_excp_state(cpu, vector, new_msr);
    }
}
#else
static inline void powerpc_excp_books(PowerPCCPU *cpu, int excp)
{
    g_assert_not_reached();
}
#endif /* TARGET_PPC64 */

static void powerpc_excp(PowerPCCPU *cpu, int excp)
{
    CPUPPCState *env = &cpu->env;

    if (excp <= POWERPC_EXCP_NONE || excp >= POWERPC_EXCP_NB) {
        cpu_abort(env_cpu(env), "Invalid PowerPC exception %d. Aborting\n",
                  excp);
    }

    qemu_log_mask(CPU_LOG_INT, "Raise exception at " TARGET_FMT_lx
                  " => %s (%d) error=%02x\n", env->nip, powerpc_excp_name(excp),
                  excp, env->error_code);
    env->excp_stats[excp]++;

    switch (env->excp_model) {
    case POWERPC_EXCP_40x:
        powerpc_excp_40x(cpu, excp);
        break;
    case POWERPC_EXCP_6xx:
        powerpc_excp_6xx(cpu, excp);
        break;
    case POWERPC_EXCP_7xx:
        powerpc_excp_7xx(cpu, excp);
        break;
    case POWERPC_EXCP_74xx:
        powerpc_excp_74xx(cpu, excp);
        break;
    case POWERPC_EXCP_BOOKE:
        powerpc_excp_booke(cpu, excp);
        break;
    case POWERPC_EXCP_970:
    case POWERPC_EXCP_POWER7:
    case POWERPC_EXCP_POWER8:
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        powerpc_excp_books(cpu, excp);
        break;
    default:
        g_assert_not_reached();
    }
}

void ppc_cpu_do_interrupt(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    powerpc_excp(cpu, cs->exception_index);
}

#ifdef TARGET_PPC64
#define P7_UNUSED_INTERRUPTS \
    (PPC_INTERRUPT_RESET | PPC_INTERRUPT_HVIRT | PPC_INTERRUPT_CEXT |       \
     PPC_INTERRUPT_WDT | PPC_INTERRUPT_CDOORBELL | PPC_INTERRUPT_FIT |      \
     PPC_INTERRUPT_PIT | PPC_INTERRUPT_DOORBELL | PPC_INTERRUPT_HDOORBELL | \
     PPC_INTERRUPT_THERM | PPC_INTERRUPT_EBB)

static int p7_interrupt_powersave(uint32_t pending_interrupts,
                                  target_ulong lpcr)
{
    if ((pending_interrupts & PPC_INTERRUPT_EXT) &&
        (lpcr & LPCR_P7_PECE0)) {
        return PPC_INTERRUPT_EXT;
    }
    if ((pending_interrupts & PPC_INTERRUPT_DECR) &&
        (lpcr & LPCR_P7_PECE1)) {
        return PPC_INTERRUPT_DECR;
    }
    if ((pending_interrupts & PPC_INTERRUPT_MCK) &&
        (lpcr & LPCR_P7_PECE2)) {
        return PPC_INTERRUPT_MCK;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HMI) &&
        (lpcr & LPCR_P7_PECE2)) {
        return PPC_INTERRUPT_HMI;
    }
    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    return 0;
}

static int p7_next_unmasked_interrupt(CPUPPCState *env,
                                      uint32_t pending_interrupts,
                                      target_ulong lpcr)
{
    CPUState *cs = env_cpu(env);

    /* Ignore MSR[EE] when coming out of some power management states */
    bool msr_ee = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    assert((pending_interrupts & P7_UNUSED_INTERRUPTS) == 0);

    if (cs->halted) {
        /* LPCR[PECE] controls which interrupts can exit power-saving mode */
        return p7_interrupt_powersave(pending_interrupts, lpcr);
    }

    /* Machine check exception */
    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }

    /* Hypervisor decrementer exception */
    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        /* LPCR will be clear when not supported so this will work */
        bool hdice = !!(env->spr[SPR_LPCR] & LPCR_HDICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hdice) {
            /* HDEC clears on delivery */
            return PPC_INTERRUPT_HDECR;
        }
    }

    /* External interrupt can ignore MSR:EE under some circumstances */
    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        /* HEIC blocks delivery to the hypervisor */
        if ((msr_ee && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (msr_ee != 0) {
        /* Decrementer exception */
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
    }

    return 0;
}

#define P8_UNUSED_INTERRUPTS \
    (PPC_INTERRUPT_RESET | PPC_INTERRUPT_DEBUG | PPC_INTERRUPT_HVIRT |  \
    PPC_INTERRUPT_CEXT | PPC_INTERRUPT_WDT | PPC_INTERRUPT_CDOORBELL |  \
    PPC_INTERRUPT_FIT | PPC_INTERRUPT_PIT | PPC_INTERRUPT_THERM)

static int p8_interrupt_powersave(uint32_t pending_interrupts,
                                  target_ulong lpcr)
{
    if ((pending_interrupts & PPC_INTERRUPT_EXT) &&
        (lpcr & LPCR_P8_PECE2)) {
        return PPC_INTERRUPT_EXT;
    }
    if ((pending_interrupts & PPC_INTERRUPT_DECR) &&
        (lpcr & LPCR_P8_PECE3)) {
        return PPC_INTERRUPT_DECR;
    }
    if ((pending_interrupts & PPC_INTERRUPT_MCK) &&
        (lpcr & LPCR_P8_PECE4)) {
        return PPC_INTERRUPT_MCK;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HMI) &&
        (lpcr & LPCR_P8_PECE4)) {
        return PPC_INTERRUPT_HMI;
    }
    if ((pending_interrupts & PPC_INTERRUPT_DOORBELL) &&
        (lpcr & LPCR_P8_PECE0)) {
        return PPC_INTERRUPT_DOORBELL;
    }
    if ((pending_interrupts & PPC_INTERRUPT_HDOORBELL) &&
        (lpcr & LPCR_P8_PECE1)) {
        return PPC_INTERRUPT_HDOORBELL;
    }
    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    return 0;
}

static int p8_next_unmasked_interrupt(CPUPPCState *env,
                                      uint32_t pending_interrupts,
                                      target_ulong lpcr)
{
    CPUState *cs = env_cpu(env);

    /* Ignore MSR[EE] when coming out of some power management states */
    bool msr_ee = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    assert((env->pending_interrupts & P8_UNUSED_INTERRUPTS) == 0);

    if (cs->halted) {
        /* LPCR[PECE] controls which interrupts can exit power-saving mode */
        return p8_interrupt_powersave(pending_interrupts, lpcr);
    }

    /* Machine check exception */
    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }

    /* Hypervisor decrementer exception */
    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        /* LPCR will be clear when not supported so this will work */
        bool hdice = !!(lpcr & LPCR_HDICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hdice) {
            /* HDEC clears on delivery */
            return PPC_INTERRUPT_HDECR;
        }
    }

    /* External interrupt can ignore MSR:EE under some circumstances */
    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        /* HEIC blocks delivery to the hypervisor */
        if ((msr_ee && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (msr_ee != 0) {
        /* Decrementer exception */
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            return PPC_INTERRUPT_DOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_HDOORBELL) {
            return PPC_INTERRUPT_HDOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
        /* EBB exception */
        if (pending_interrupts & PPC_INTERRUPT_EBB) {
            /*
             * EBB exception must be taken in problem state and
             * with BESCR_GE set.
             */
            if (FIELD_EX64(env->msr, MSR, PR) &&
                (env->spr[SPR_BESCR] & BESCR_GE)) {
                return PPC_INTERRUPT_EBB;
            }
        }
    }

    return 0;
}

#define P9_UNUSED_INTERRUPTS \
    (PPC_INTERRUPT_RESET | PPC_INTERRUPT_DEBUG | PPC_INTERRUPT_CEXT |   \
     PPC_INTERRUPT_WDT | PPC_INTERRUPT_CDOORBELL | PPC_INTERRUPT_FIT |  \
     PPC_INTERRUPT_PIT | PPC_INTERRUPT_THERM)

static int p9_interrupt_powersave(CPUPPCState *env,
                                  uint32_t pending_interrupts,
                                  target_ulong lpcr)
{

    /* External Exception */
    if ((pending_interrupts & PPC_INTERRUPT_EXT) &&
        (lpcr & LPCR_EEE)) {
        bool heic = !!(lpcr & LPCR_HEIC);
        if (!heic || !FIELD_EX64_HV(env->msr) ||
            FIELD_EX64(env->msr, MSR, PR)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    /* Decrementer Exception */
    if ((pending_interrupts & PPC_INTERRUPT_DECR) &&
        (lpcr & LPCR_DEE)) {
        return PPC_INTERRUPT_DECR;
    }
    /* Machine Check or Hypervisor Maintenance Exception */
    if (lpcr & LPCR_OEE) {
        if (pending_interrupts & PPC_INTERRUPT_MCK) {
            return PPC_INTERRUPT_MCK;
        }
        if (pending_interrupts & PPC_INTERRUPT_HMI) {
            return PPC_INTERRUPT_HMI;
        }
    }
    /* Privileged Doorbell Exception */
    if ((pending_interrupts & PPC_INTERRUPT_DOORBELL) &&
        (lpcr & LPCR_PDEE)) {
        return PPC_INTERRUPT_DOORBELL;
    }
    /* Hypervisor Doorbell Exception */
    if ((pending_interrupts & PPC_INTERRUPT_HDOORBELL) &&
        (lpcr & LPCR_HDEE)) {
        return PPC_INTERRUPT_HDOORBELL;
    }
    /* Hypervisor virtualization exception */
    if ((pending_interrupts & PPC_INTERRUPT_HVIRT) &&
        (lpcr & LPCR_HVEE)) {
        return PPC_INTERRUPT_HVIRT;
    }
    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    return 0;
}

static int p9_next_unmasked_interrupt(CPUPPCState *env,
                                      uint32_t pending_interrupts,
                                      target_ulong lpcr)
{
    CPUState *cs = env_cpu(env);

    /* Ignore MSR[EE] when coming out of some power management states */
    bool msr_ee = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    assert((pending_interrupts & P9_UNUSED_INTERRUPTS) == 0);

    if (cs->halted) {
        if (env->spr[SPR_PSSCR] & PSSCR_EC) {
            /*
             * When PSSCR[EC] is set, LPCR[PECE] controls which interrupts can
             * wakeup the processor
             */
            return p9_interrupt_powersave(env, pending_interrupts, lpcr);
        } else {
            /*
             * When it's clear, any system-caused exception exits power-saving
             * mode, even the ones that gate on MSR[EE].
             */
            msr_ee = true;
        }
    }

    /* Machine check exception */
    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }

    /* Hypervisor decrementer exception */
    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        /* LPCR will be clear when not supported so this will work */
        bool hdice = !!(lpcr & LPCR_HDICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hdice) {
            /* HDEC clears on delivery */
            return PPC_INTERRUPT_HDECR;
        }
    }

    /* Hypervisor virtualization interrupt */
    if (pending_interrupts & PPC_INTERRUPT_HVIRT) {
        /* LPCR will be clear when not supported so this will work */
        bool hvice = !!(lpcr & LPCR_HVICE);
        if ((msr_ee || !FIELD_EX64_HV(env->msr)) && hvice) {
            return PPC_INTERRUPT_HVIRT;
        }
    }

    /* External interrupt can ignore MSR:EE under some circumstances */
    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        /* HEIC blocks delivery to the hypervisor */
        if ((msr_ee && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (msr_ee != 0) {
        /* Decrementer exception */
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            return PPC_INTERRUPT_DOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_HDOORBELL) {
            return PPC_INTERRUPT_HDOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
        /* EBB exception */
        if (pending_interrupts & PPC_INTERRUPT_EBB) {
            /*
             * EBB exception must be taken in problem state and
             * with BESCR_GE set.
             */
            if (FIELD_EX64(env->msr, MSR, PR) &&
                (env->spr[SPR_BESCR] & BESCR_GE)) {
                return PPC_INTERRUPT_EBB;
            }
        }
    }

    return 0;
}
#endif /* TARGET_PPC64 */

static int ppc_next_unmasked_interrupt(CPUPPCState *env)
{
    uint32_t pending_interrupts = env->pending_interrupts;
    target_ulong lpcr = env->spr[SPR_LPCR];
    bool async_deliver;

#ifdef TARGET_PPC64
    switch (env->excp_model) {
    case POWERPC_EXCP_POWER7:
        return p7_next_unmasked_interrupt(env, pending_interrupts, lpcr);
    case POWERPC_EXCP_POWER8:
        return p8_next_unmasked_interrupt(env, pending_interrupts, lpcr);
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        return p9_next_unmasked_interrupt(env, pending_interrupts, lpcr);
    default:
        break;
    }
#endif

    /* External reset */
    if (pending_interrupts & PPC_INTERRUPT_RESET) {
        return PPC_INTERRUPT_RESET;
    }
    /* Machine check exception */
    if (pending_interrupts & PPC_INTERRUPT_MCK) {
        return PPC_INTERRUPT_MCK;
    }
#if 0 /* TODO */
    /* External debug exception */
    if (env->pending_interrupts & PPC_INTERRUPT_DEBUG) {
        return PPC_INTERRUPT_DEBUG;
    }
#endif

    /*
     * For interrupts that gate on MSR:EE, we need to do something a
     * bit more subtle, as we need to let them through even when EE is
     * clear when coming out of some power management states (in order
     * for them to become a 0x100).
     */
    async_deliver = FIELD_EX64(env->msr, MSR, EE) || env->resume_as_sreset;

    /* Hypervisor decrementer exception */
    if (pending_interrupts & PPC_INTERRUPT_HDECR) {
        /* LPCR will be clear when not supported so this will work */
        bool hdice = !!(lpcr & LPCR_HDICE);
        if ((async_deliver || !FIELD_EX64_HV(env->msr)) && hdice) {
            /* HDEC clears on delivery */
            return PPC_INTERRUPT_HDECR;
        }
    }

    /* Hypervisor virtualization interrupt */
    if (pending_interrupts & PPC_INTERRUPT_HVIRT) {
        /* LPCR will be clear when not supported so this will work */
        bool hvice = !!(lpcr & LPCR_HVICE);
        if ((async_deliver || !FIELD_EX64_HV(env->msr)) && hvice) {
            return PPC_INTERRUPT_HVIRT;
        }
    }

    /* External interrupt can ignore MSR:EE under some circumstances */
    if (pending_interrupts & PPC_INTERRUPT_EXT) {
        bool lpes0 = !!(lpcr & LPCR_LPES0);
        bool heic = !!(lpcr & LPCR_HEIC);
        /* HEIC blocks delivery to the hypervisor */
        if ((async_deliver && !(heic && FIELD_EX64_HV(env->msr) &&
            !FIELD_EX64(env->msr, MSR, PR))) ||
            (env->has_hv_mode && !FIELD_EX64_HV(env->msr) && !lpes0)) {
            return PPC_INTERRUPT_EXT;
        }
    }
    if (FIELD_EX64(env->msr, MSR, CE)) {
        /* External critical interrupt */
        if (pending_interrupts & PPC_INTERRUPT_CEXT) {
            return PPC_INTERRUPT_CEXT;
        }
    }
    if (async_deliver != 0) {
        /* Watchdog timer on embedded PowerPC */
        if (pending_interrupts & PPC_INTERRUPT_WDT) {
            return PPC_INTERRUPT_WDT;
        }
        if (pending_interrupts & PPC_INTERRUPT_CDOORBELL) {
            return PPC_INTERRUPT_CDOORBELL;
        }
        /* Fixed interval timer on embedded PowerPC */
        if (pending_interrupts & PPC_INTERRUPT_FIT) {
            return PPC_INTERRUPT_FIT;
        }
        /* Programmable interval timer on embedded PowerPC */
        if (pending_interrupts & PPC_INTERRUPT_PIT) {
            return PPC_INTERRUPT_PIT;
        }
        /* Decrementer exception */
        if (pending_interrupts & PPC_INTERRUPT_DECR) {
            return PPC_INTERRUPT_DECR;
        }
        if (pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            return PPC_INTERRUPT_DOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_HDOORBELL) {
            return PPC_INTERRUPT_HDOORBELL;
        }
        if (pending_interrupts & PPC_INTERRUPT_PERFM) {
            return PPC_INTERRUPT_PERFM;
        }
        /* Thermal interrupt */
        if (pending_interrupts & PPC_INTERRUPT_THERM) {
            return PPC_INTERRUPT_THERM;
        }
        /* EBB exception */
        if (pending_interrupts & PPC_INTERRUPT_EBB) {
            /*
             * EBB exception must be taken in problem state and
             * with BESCR_GE set.
             */
            if (FIELD_EX64(env->msr, MSR, PR) &&
                (env->spr[SPR_BESCR] & BESCR_GE)) {
                return PPC_INTERRUPT_EBB;
            }
        }
    }

    return 0;
}

/*
 * Sets CPU_INTERRUPT_HARD if there is at least one unmasked interrupt to be
 * delivered and clears CPU_INTERRUPT_HARD otherwise.
 *
 * This method is called by ppc_set_interrupt when an interrupt is raised or
 * lowered, and should also be called whenever an interrupt masking condition
 * is changed, e.g.:
 *  - When relevant bits of MSR are altered, like EE, HV, PR, etc.;
 *  - When relevant bits of LPCR are altered, like PECE, HDICE, HVICE, etc.;
 *  - When PSSCR[EC] or env->resume_as_sreset are changed;
 *  - When cs->halted is changed and the CPU has a different interrupt masking
 *    logic in power-saving mode (e.g., POWER7/8/9/10);
 */
void ppc_maybe_interrupt(CPUPPCState *env)
{
    CPUState *cs = env_cpu(env);
    BQL_LOCK_GUARD();

    if (ppc_next_unmasked_interrupt(env)) {
        cpu_interrupt(cs, CPU_INTERRUPT_HARD);
    } else {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

#ifdef TARGET_PPC64
static void p7_deliver_interrupt(CPUPPCState *env, int interrupt)
{
    PowerPCCPU *cpu = env_archcpu(env);

    switch (interrupt) {
    case PPC_INTERRUPT_MCK: /* Machine check exception */
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR: /* Hypervisor decrementer exception */
        /* HDEC clears on delivery */
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;

    case PPC_INTERRUPT_DECR: /* Decrementer exception */
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case 0:
        /*
         * This is a bug ! It means that has_work took us out of halt without
         * anything to deliver while in a PM state that requires getting
         * out via a 0x100
         *
         * This means we will incorrectly execute past the power management
         * instruction instead of triggering a reset.
         *
         * It generally means a discrepancy between the wakeup conditions in the
         * processor has_work implementation and the logic in this function.
         */
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}

static void p8_deliver_interrupt(CPUPPCState *env, int interrupt)
{
    PowerPCCPU *cpu = env_archcpu(env);

    switch (interrupt) {
    case PPC_INTERRUPT_MCK: /* Machine check exception */
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR: /* Hypervisor decrementer exception */
        /* HDEC clears on delivery */
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;

    case PPC_INTERRUPT_DECR: /* Decrementer exception */
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_DOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_DOORBELL;
        }
        if (is_book3s_arch2x(env)) {
            powerpc_excp(cpu, POWERPC_EXCP_SDOOR);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_DOORI);
        }
        break;
    case PPC_INTERRUPT_HDOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_HDOORBELL;
        }
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR_HV);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case PPC_INTERRUPT_EBB: /* EBB exception */
        env->pending_interrupts &= ~PPC_INTERRUPT_EBB;
        if (env->spr[SPR_BESCR] & BESCR_PMEO) {
            powerpc_excp(cpu, POWERPC_EXCP_PERFM_EBB);
        } else if (env->spr[SPR_BESCR] & BESCR_EEO) {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL_EBB);
        }
        break;
    case 0:
        /*
         * This is a bug ! It means that has_work took us out of halt without
         * anything to deliver while in a PM state that requires getting
         * out via a 0x100
         *
         * This means we will incorrectly execute past the power management
         * instruction instead of triggering a reset.
         *
         * It generally means a discrepancy between the wakeup conditions in the
         * processor has_work implementation and the logic in this function.
         */
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}

static void p9_deliver_interrupt(CPUPPCState *env, int interrupt)
{
    PowerPCCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (cs->halted && !(env->spr[SPR_PSSCR] & PSSCR_EC) &&
        !FIELD_EX64(env->msr, MSR, EE)) {
        /*
         * A pending interrupt took us out of power-saving, but MSR[EE] says
         * that we should return to NIP+4 instead of delivering it.
         */
        return;
    }

    switch (interrupt) {
    case PPC_INTERRUPT_MCK: /* Machine check exception */
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR: /* Hypervisor decrementer exception */
        /* HDEC clears on delivery */
        /* XXX: should not see an HDEC if resume_as_sreset. assert? */
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;
    case PPC_INTERRUPT_HVIRT: /* Hypervisor virtualization interrupt */
        powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;

    case PPC_INTERRUPT_DECR: /* Decrementer exception */
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_DOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_DOORBELL;
        }
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR);
        break;
    case PPC_INTERRUPT_HDOORBELL:
        if (!env->resume_as_sreset) {
            env->pending_interrupts &= ~PPC_INTERRUPT_HDOORBELL;
        }
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR_HV);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case PPC_INTERRUPT_EBB: /* EBB exception */
        env->pending_interrupts &= ~PPC_INTERRUPT_EBB;
        if (env->spr[SPR_BESCR] & BESCR_PMEO) {
            powerpc_excp(cpu, POWERPC_EXCP_PERFM_EBB);
        } else if (env->spr[SPR_BESCR] & BESCR_EEO) {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL_EBB);
        }
        break;
    case 0:
        /*
         * This is a bug ! It means that has_work took us out of halt without
         * anything to deliver while in a PM state that requires getting
         * out via a 0x100
         *
         * This means we will incorrectly execute past the power management
         * instruction instead of triggering a reset.
         *
         * It generally means a discrepancy between the wakeup conditions in the
         * processor has_work implementation and the logic in this function.
         */
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}
#endif /* TARGET_PPC64 */

static void ppc_deliver_interrupt(CPUPPCState *env, int interrupt)
{
#ifdef TARGET_PPC64
    switch (env->excp_model) {
    case POWERPC_EXCP_POWER7:
        return p7_deliver_interrupt(env, interrupt);
    case POWERPC_EXCP_POWER8:
        return p8_deliver_interrupt(env, interrupt);
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        return p9_deliver_interrupt(env, interrupt);
    default:
        break;
    }
#endif
    PowerPCCPU *cpu = env_archcpu(env);

    switch (interrupt) {
    case PPC_INTERRUPT_RESET: /* External reset */
        env->pending_interrupts &= ~PPC_INTERRUPT_RESET;
        powerpc_excp(cpu, POWERPC_EXCP_RESET);
        break;
    case PPC_INTERRUPT_MCK: /* Machine check exception */
        env->pending_interrupts &= ~PPC_INTERRUPT_MCK;
        powerpc_excp(cpu, POWERPC_EXCP_MCHECK);
        break;

    case PPC_INTERRUPT_HDECR: /* Hypervisor decrementer exception */
        /* HDEC clears on delivery */
        env->pending_interrupts &= ~PPC_INTERRUPT_HDECR;
        powerpc_excp(cpu, POWERPC_EXCP_HDECR);
        break;
    case PPC_INTERRUPT_HVIRT: /* Hypervisor virtualization interrupt */
        powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        break;

    case PPC_INTERRUPT_EXT:
        if (books_vhyp_promotes_external_to_hvirt(cpu)) {
            powerpc_excp(cpu, POWERPC_EXCP_HVIRT);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL);
        }
        break;
    case PPC_INTERRUPT_CEXT: /* External critical interrupt */
        powerpc_excp(cpu, POWERPC_EXCP_CRITICAL);
        break;

    case PPC_INTERRUPT_WDT: /* Watchdog timer on embedded PowerPC */
        env->pending_interrupts &= ~PPC_INTERRUPT_WDT;
        powerpc_excp(cpu, POWERPC_EXCP_WDT);
        break;
    case PPC_INTERRUPT_CDOORBELL:
        env->pending_interrupts &= ~PPC_INTERRUPT_CDOORBELL;
        powerpc_excp(cpu, POWERPC_EXCP_DOORCI);
        break;
    case PPC_INTERRUPT_FIT: /* Fixed interval timer on embedded PowerPC */
        env->pending_interrupts &= ~PPC_INTERRUPT_FIT;
        powerpc_excp(cpu, POWERPC_EXCP_FIT);
        break;
    case PPC_INTERRUPT_PIT: /* Programmable interval timer on embedded ppc */
        env->pending_interrupts &= ~PPC_INTERRUPT_PIT;
        powerpc_excp(cpu, POWERPC_EXCP_PIT);
        break;
    case PPC_INTERRUPT_DECR: /* Decrementer exception */
        if (ppc_decr_clear_on_delivery(env)) {
            env->pending_interrupts &= ~PPC_INTERRUPT_DECR;
        }
        powerpc_excp(cpu, POWERPC_EXCP_DECR);
        break;
    case PPC_INTERRUPT_DOORBELL:
        env->pending_interrupts &= ~PPC_INTERRUPT_DOORBELL;
        if (is_book3s_arch2x(env)) {
            powerpc_excp(cpu, POWERPC_EXCP_SDOOR);
        } else {
            powerpc_excp(cpu, POWERPC_EXCP_DOORI);
        }
        break;
    case PPC_INTERRUPT_HDOORBELL:
        env->pending_interrupts &= ~PPC_INTERRUPT_HDOORBELL;
        powerpc_excp(cpu, POWERPC_EXCP_SDOOR_HV);
        break;
    case PPC_INTERRUPT_PERFM:
        powerpc_excp(cpu, POWERPC_EXCP_PERFM);
        break;
    case PPC_INTERRUPT_THERM:  /* Thermal interrupt */
        env->pending_interrupts &= ~PPC_INTERRUPT_THERM;
        powerpc_excp(cpu, POWERPC_EXCP_THERM);
        break;
    case PPC_INTERRUPT_EBB: /* EBB exception */
        env->pending_interrupts &= ~PPC_INTERRUPT_EBB;
        if (env->spr[SPR_BESCR] & BESCR_PMEO) {
            powerpc_excp(cpu, POWERPC_EXCP_PERFM_EBB);
        } else if (env->spr[SPR_BESCR] & BESCR_EEO) {
            powerpc_excp(cpu, POWERPC_EXCP_EXTERNAL_EBB);
        }
        break;
    case 0:
        /*
         * This is a bug ! It means that has_work took us out of halt without
         * anything to deliver while in a PM state that requires getting
         * out via a 0x100
         *
         * This means we will incorrectly execute past the power management
         * instruction instead of triggering a reset.
         *
         * It generally means a discrepancy between the wakeup conditions in the
         * processor has_work implementation and the logic in this function.
         */
        assert(!env->resume_as_sreset);
        break;
    default:
        cpu_abort(env_cpu(env), "Invalid PowerPC interrupt %d. Aborting\n",
                  interrupt);
    }
}

/*
 * system reset is not delivered via normal irq method, so have to set
 * halted = 0 to resume CPU running if it was halted. Possibly we should
 * move it over to using PPC_INTERRUPT_RESET rather than async_run_on_cpu.
 */
void ppc_cpu_do_system_reset(CPUState *cs)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);

    cs->halted = 0;
    powerpc_excp(cpu, POWERPC_EXCP_RESET);
}

void ppc_cpu_do_fwnmi_machine_check(CPUState *cs, target_ulong vector)
{
    PowerPCCPU *cpu = POWERPC_CPU(cs);
    CPUPPCState *env = &cpu->env;
    target_ulong msr = 0;

    /*
     * Set MSR and NIP for the handler, SRR0/1, DAR and DSISR have already
     * been set by KVM.
     */
    msr = (1ULL << MSR_ME);
    msr |= env->msr & (1ULL << MSR_SF);
    if (ppc_interrupts_little_endian(cpu, false)) {
        msr |= (1ULL << MSR_LE);
    }

    /* Anything for nested required here? MSR[HV] bit? */

    cs->halted = 0;
    powerpc_set_excp_state(cpu, vector, msr);
}

bool ppc_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUPPCState *env = cpu_env(cs);
    int interrupt;

    if ((interrupt_request & CPU_INTERRUPT_HARD) == 0) {
        return false;
    }

    interrupt = ppc_next_unmasked_interrupt(env);
    if (interrupt == 0) {
        return false;
    }

    ppc_deliver_interrupt(env, interrupt);
    if (env->pending_interrupts == 0) {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
    return true;
}

#endif /* !CONFIG_USER_ONLY */

/*****************************************************************************/
/* Exceptions processing helpers */

void raise_exception_err_ra(CPUPPCState *env, uint32_t exception,
                            uint32_t error_code, uintptr_t raddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = exception;
    env->error_code = error_code;
    cpu_loop_exit_restore(cs, raddr);
}

void raise_exception_err(CPUPPCState *env, uint32_t exception,
                         uint32_t error_code)
{
    raise_exception_err_ra(env, exception, error_code, 0);
}

void raise_exception(CPUPPCState *env, uint32_t exception)
{
    raise_exception_err_ra(env, exception, 0, 0);
}

void raise_exception_ra(CPUPPCState *env, uint32_t exception,
                        uintptr_t raddr)
{
    raise_exception_err_ra(env, exception, 0, raddr);
}

#ifdef CONFIG_TCG
void helper_raise_exception_err(CPUPPCState *env, uint32_t exception,
                                uint32_t error_code)
{
    raise_exception_err_ra(env, exception, error_code, 0);
}

void helper_raise_exception(CPUPPCState *env, uint32_t exception)
{
    raise_exception_err_ra(env, exception, 0, 0);
}

#ifndef CONFIG_USER_ONLY
void helper_store_msr(CPUPPCState *env, target_ulong val)
{
    uint32_t excp = hreg_store_msr(env, val, 0);

    if (excp != 0) {
        cpu_interrupt_exittb(env_cpu(env));
        raise_exception(env, excp);
    }
}

void helper_ppc_maybe_interrupt(CPUPPCState *env)
{
    ppc_maybe_interrupt(env);
}

#ifdef TARGET_PPC64
void helper_scv(CPUPPCState *env, uint32_t lev)
{
    if (env->spr[SPR_FSCR] & (1ull << FSCR_SCV)) {
        raise_exception_err(env, POWERPC_EXCP_SYSCALL_VECTORED, lev);
    } else {
        raise_exception_err(env, POWERPC_EXCP_FU, FSCR_IC_SCV);
    }
}

void helper_pminsn(CPUPPCState *env, uint32_t insn)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;

    /* Condition for waking up at 0x100 */
    env->resume_as_sreset = (insn != PPC_PM_STOP) ||
        (env->spr[SPR_PSSCR] & PSSCR_EC);

    /* HDECR is not to wake from PM state, it may have already fired */
    if (env->resume_as_sreset) {
        PowerPCCPU *cpu = env_archcpu(env);
        ppc_set_irq(cpu, PPC_INTERRUPT_HDECR, 0);
    }

    ppc_maybe_interrupt(env);
}
#endif /* TARGET_PPC64 */

static void do_rfi(CPUPPCState *env, target_ulong nip, target_ulong msr)
{
    /* MSR:POW cannot be set by any form of rfi */
    msr &= ~(1ULL << MSR_POW);

    /* MSR:TGPR cannot be set by any form of rfi */
    if (env->flags & POWERPC_FLAG_TGPR)
        msr &= ~(1ULL << MSR_TGPR);

#ifdef TARGET_PPC64
    /* Switching to 32-bit ? Crop the nip */
    if (!msr_is_64bit(env, msr)) {
        nip = (uint32_t)nip;
    }
#else
    nip = (uint32_t)nip;
#endif
    /* XXX: beware: this is false if VLE is supported */
    env->nip = nip & ~((target_ulong)0x00000003);
    hreg_store_msr(env, msr, 1);
    trace_ppc_excp_rfi(env->nip, env->msr);
    /*
     * No need to raise an exception here, as rfi is always the last
     * insn of a TB
     */
    cpu_interrupt_exittb(env_cpu(env));
    /* Reset the reservation */
    env->reserve_addr = -1;

    /* Context synchronizing: check if TCG TLB needs flush */
    check_tlb_flush(env, false);
}

void helper_rfi(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_SRR0], env->spr[SPR_SRR1] & 0xfffffffful);
}

#ifdef TARGET_PPC64
void helper_rfid(CPUPPCState *env)
{
    /*
     * The architecture defines a number of rules for which bits can
     * change but in practice, we handle this in hreg_store_msr()
     * which will be called by do_rfi(), so there is no need to filter
     * here
     */
    do_rfi(env, env->spr[SPR_SRR0], env->spr[SPR_SRR1]);
}

void helper_rfscv(CPUPPCState *env)
{
    do_rfi(env, env->lr, env->ctr);
}

void helper_hrfid(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_HSRR0], env->spr[SPR_HSRR1]);
}

void helper_rfebb(CPUPPCState *env, target_ulong s)
{
    target_ulong msr = env->msr;

    /*
     * Handling of BESCR bits 32:33 according to PowerISA v3.1:
     *
     * "If BESCR 32:33 != 0b00 the instruction is treated as if
     *  the instruction form were invalid."
     */
    if (env->spr[SPR_BESCR] & BESCR_INVALID) {
        raise_exception_err(env, POWERPC_EXCP_PROGRAM,
                            POWERPC_EXCP_INVAL | POWERPC_EXCP_INVAL_INVAL);
    }

    env->nip = env->spr[SPR_EBBRR];

    /* Switching to 32-bit ? Crop the nip */
    if (!msr_is_64bit(env, msr)) {
        env->nip = (uint32_t)env->spr[SPR_EBBRR];
    }

    if (s) {
        env->spr[SPR_BESCR] |= BESCR_GE;
    } else {
        env->spr[SPR_BESCR] &= ~BESCR_GE;
    }
}

/*
 * Triggers or queues an 'ebb_excp' EBB exception. All checks
 * but FSCR, HFSCR and msr_pr must be done beforehand.
 *
 * PowerISA v3.1 isn't clear about whether an EBB should be
 * postponed or cancelled if the EBB facility is unavailable.
 * Our assumption here is that the EBB is cancelled if both
 * FSCR and HFSCR EBB facilities aren't available.
 */
static void do_ebb(CPUPPCState *env, int ebb_excp)
{
    PowerPCCPU *cpu = env_archcpu(env);

    /*
     * FSCR_EBB and FSCR_IC_EBB are the same bits used with
     * HFSCR.
     */
    helper_fscr_facility_check(env, FSCR_EBB, 0, FSCR_IC_EBB);
    helper_hfscr_facility_check(env, FSCR_EBB, "EBB", FSCR_IC_EBB);

    if (ebb_excp == POWERPC_EXCP_PERFM_EBB) {
        env->spr[SPR_BESCR] |= BESCR_PMEO;
    } else if (ebb_excp == POWERPC_EXCP_EXTERNAL_EBB) {
        env->spr[SPR_BESCR] |= BESCR_EEO;
    }

    if (FIELD_EX64(env->msr, MSR, PR)) {
        powerpc_excp(cpu, ebb_excp);
    } else {
        ppc_set_irq(cpu, PPC_INTERRUPT_EBB, 1);
    }
}

void raise_ebb_perfm_exception(CPUPPCState *env)
{
    bool perfm_ebb_enabled = env->spr[SPR_POWER_MMCR0] & MMCR0_EBE &&
                             env->spr[SPR_BESCR] & BESCR_PME &&
                             env->spr[SPR_BESCR] & BESCR_GE;

    if (!perfm_ebb_enabled) {
        return;
    }

    do_ebb(env, POWERPC_EXCP_PERFM_EBB);
}
#endif /* TARGET_PPC64 */

/*****************************************************************************/
/* Embedded PowerPC specific helpers */
void helper_40x_rfci(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_40x_SRR2], env->spr[SPR_40x_SRR3]);
}

void helper_rfci(CPUPPCState *env)
{
    do_rfi(env, env->spr[SPR_BOOKE_CSRR0], env->spr[SPR_BOOKE_CSRR1]);
}

void helper_rfdi(CPUPPCState *env)
{
    /* FIXME: choose CSRR1 or DSRR1 based on cpu type */
    do_rfi(env, env->spr[SPR_BOOKE_DSRR0], env->spr[SPR_BOOKE_DSRR1]);
}

void helper_rfmci(CPUPPCState *env)
{
    /* FIXME: choose CSRR1 or MCSRR1 based on cpu type */
    do_rfi(env, env->spr[SPR_BOOKE_MCSRR0], env->spr[SPR_BOOKE_MCSRR1]);
}
#endif /* !CONFIG_USER_ONLY */

void helper_TW(CPUPPCState *env, target_ulong arg1, target_ulong arg2,
               uint32_t flags)
{
    if (!likely(!(((int32_t)arg1 < (int32_t)arg2 && (flags & 0x10)) ||
                  ((int32_t)arg1 > (int32_t)arg2 && (flags & 0x08)) ||
                  ((int32_t)arg1 == (int32_t)arg2 && (flags & 0x04)) ||
                  ((uint32_t)arg1 < (uint32_t)arg2 && (flags & 0x02)) ||
                  ((uint32_t)arg1 > (uint32_t)arg2 && (flags & 0x01))))) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_TRAP, GETPC());
    }
}

#ifdef TARGET_PPC64
void helper_TD(CPUPPCState *env, target_ulong arg1, target_ulong arg2,
               uint32_t flags)
{
    if (!likely(!(((int64_t)arg1 < (int64_t)arg2 && (flags & 0x10)) ||
                  ((int64_t)arg1 > (int64_t)arg2 && (flags & 0x08)) ||
                  ((int64_t)arg1 == (int64_t)arg2 && (flags & 0x04)) ||
                  ((uint64_t)arg1 < (uint64_t)arg2 && (flags & 0x02)) ||
                  ((uint64_t)arg1 > (uint64_t)arg2 && (flags & 0x01))))) {
        raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                               POWERPC_EXCP_TRAP, GETPC());
    }
}
#endif /* TARGET_PPC64 */

static uint32_t helper_SIMON_LIKE_32_64(uint32_t x, uint64_t key, uint32_t lane)
{
    const uint16_t c = 0xfffc;
    const uint64_t z0 = 0xfa2561cdf44ac398ULL;
    uint16_t z = 0, temp;
    uint16_t k[32], eff_k[32], xleft[33], xright[33], fxleft[32];

    for (int i = 3; i >= 0; i--) {
        k[i] = key & 0xffff;
        key >>= 16;
    }
    xleft[0] = x & 0xffff;
    xright[0] = (x >> 16) & 0xffff;

    for (int i = 0; i < 28; i++) {
        z = (z0 >> (63 - i)) & 1;
        temp = ror16(k[i + 3], 3) ^ k[i + 1];
        k[i + 4] = c ^ z ^ k[i] ^ temp ^ ror16(temp, 1);
    }

    for (int i = 0; i < 8; i++) {
        eff_k[4 * i + 0] = k[4 * i + ((0 + lane) % 4)];
        eff_k[4 * i + 1] = k[4 * i + ((1 + lane) % 4)];
        eff_k[4 * i + 2] = k[4 * i + ((2 + lane) % 4)];
        eff_k[4 * i + 3] = k[4 * i + ((3 + lane) % 4)];
    }

    for (int i = 0; i < 32; i++) {
        fxleft[i] = (rol16(xleft[i], 1) &
            rol16(xleft[i], 8)) ^ rol16(xleft[i], 2);
        xleft[i + 1] = xright[i] ^ fxleft[i] ^ eff_k[i];
        xright[i + 1] = xleft[i];
    }

    return (((uint32_t)xright[32]) << 16) | xleft[32];
}

static uint64_t hash_digest(uint64_t ra, uint64_t rb, uint64_t key)
{
    uint64_t stage0_h = 0ULL, stage0_l = 0ULL;
    uint64_t stage1_h, stage1_l;

    for (int i = 0; i < 4; i++) {
        stage0_h |= ror64(rb & 0xff, 8 * (2 * i + 1));
        stage0_h |= ((ra >> 32) & 0xff) << (8 * 2 * i);
        stage0_l |= ror64((rb >> 32) & 0xff, 8 * (2 * i + 1));
        stage0_l |= (ra & 0xff) << (8 * 2 * i);
        rb >>= 8;
        ra >>= 8;
    }

    stage1_h = (uint64_t)helper_SIMON_LIKE_32_64(stage0_h >> 32, key, 0) << 32;
    stage1_h |= helper_SIMON_LIKE_32_64(stage0_h, key, 1);
    stage1_l = (uint64_t)helper_SIMON_LIKE_32_64(stage0_l >> 32, key, 2) << 32;
    stage1_l |= helper_SIMON_LIKE_32_64(stage0_l, key, 3);

    return stage1_h ^ stage1_l;
}

static void do_hash(CPUPPCState *env, target_ulong ea, target_ulong ra,
                    target_ulong rb, uint64_t key, bool store)
{
    uint64_t calculated_hash = hash_digest(ra, rb, key), loaded_hash;

    if (store) {
        cpu_stq_data_ra(env, ea, calculated_hash, GETPC());
    } else {
        loaded_hash = cpu_ldq_data_ra(env, ea, GETPC());
        if (loaded_hash != calculated_hash) {
            raise_exception_err_ra(env, POWERPC_EXCP_PROGRAM,
                POWERPC_EXCP_TRAP, GETPC());
        }
    }
}

#include "qemu/guest-random.h"

#ifdef TARGET_PPC64
#define HELPER_HASH(op, key, store, dexcr_aspect)                             \
void helper_##op(CPUPPCState *env, target_ulong ea, target_ulong ra,          \
                 target_ulong rb)                                             \
{                                                                             \
    if (env->msr & R_MSR_PR_MASK) {                                           \
        if (!(env->spr[SPR_DEXCR] & R_DEXCR_PRO_##dexcr_aspect##_MASK ||      \
            env->spr[SPR_HDEXCR] & R_HDEXCR_ENF_##dexcr_aspect##_MASK))       \
            return;                                                           \
    } else if (!(env->msr & R_MSR_HV_MASK)) {                                 \
        if (!(env->spr[SPR_DEXCR] & R_DEXCR_PNH_##dexcr_aspect##_MASK ||      \
            env->spr[SPR_HDEXCR] & R_HDEXCR_ENF_##dexcr_aspect##_MASK))       \
            return;                                                           \
    } else if (!(env->msr & R_MSR_S_MASK)) {                                  \
        if (!(env->spr[SPR_HDEXCR] & R_HDEXCR_HNU_##dexcr_aspect##_MASK))     \
            return;                                                           \
    }                                                                         \
                                                                              \
    do_hash(env, ea, ra, rb, key, store);                                     \
}
#else
#define HELPER_HASH(op, key, store, dexcr_aspect)                             \
void helper_##op(CPUPPCState *env, target_ulong ea, target_ulong ra,          \
                 target_ulong rb)                                             \
{                                                                             \
    do_hash(env, ea, ra, rb, key, store);                                     \
}
#endif /* TARGET_PPC64 */

HELPER_HASH(HASHST, env->spr[SPR_HASHKEYR], true, NPHIE)
HELPER_HASH(HASHCHK, env->spr[SPR_HASHKEYR], false, NPHIE)
HELPER_HASH(HASHSTP, env->spr[SPR_HASHPKEYR], true, PHIE)
HELPER_HASH(HASHCHKP, env->spr[SPR_HASHPKEYR], false, PHIE)

#ifndef CONFIG_USER_ONLY
/* Embedded.Processor Control */
static int dbell2irq(target_ulong rb)
{
    int msg = rb & DBELL_TYPE_MASK;
    int irq = -1;

    switch (msg) {
    case DBELL_TYPE_DBELL:
        irq = PPC_INTERRUPT_DOORBELL;
        break;
    case DBELL_TYPE_DBELL_CRIT:
        irq = PPC_INTERRUPT_CDOORBELL;
        break;
    case DBELL_TYPE_G_DBELL:
    case DBELL_TYPE_G_DBELL_CRIT:
    case DBELL_TYPE_G_DBELL_MC:
        /* XXX implement */
    default:
        break;
    }

    return irq;
}

void helper_msgclr(CPUPPCState *env, target_ulong rb)
{
    int irq = dbell2irq(rb);

    if (irq < 0) {
        return;
    }

    ppc_set_irq(env_archcpu(env), irq, 0);
}

void helper_msgsnd(target_ulong rb)
{
    int irq = dbell2irq(rb);
    int pir = rb & DBELL_PIRTAG_MASK;
    CPUState *cs;

    if (irq < 0) {
        return;
    }

    bql_lock();
    CPU_FOREACH(cs) {
        PowerPCCPU *cpu = POWERPC_CPU(cs);
        CPUPPCState *cenv = &cpu->env;

        if ((rb & DBELL_BRDCAST_MASK) || (cenv->spr[SPR_BOOKE_PIR] == pir)) {
            ppc_set_irq(cpu, irq, 1);
        }
    }
    bql_unlock();
}

/* Server Processor Control */

static bool dbell_type_server(target_ulong rb)
{
    /*
     * A Directed Hypervisor Doorbell message is sent only if the
     * message type is 5. All other types are reserved and the
     * instruction is a no-op
     */
    return (rb & DBELL_TYPE_MASK) == DBELL_TYPE_DBELL_SERVER;
}

static inline bool dbell_bcast_core(target_ulong rb)
{
    return (rb & DBELL_BRDCAST_MASK) == DBELL_BRDCAST_CORE;
}

static inline bool dbell_bcast_subproc(target_ulong rb)
{
    return (rb & DBELL_BRDCAST_MASK) == DBELL_BRDCAST_SUBPROC;
}

/*
 * Send an interrupt to a thread in the same core as env).
 */
static void msgsnd_core_tir(CPUPPCState *env, uint32_t target_tir, int irq)
{
    PowerPCCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (ppc_cpu_lpar_single_threaded(cs)) {
        if (target_tir == 0) {
            ppc_set_irq(cpu, irq, 1);
        }
    } else {
        CPUState *ccs;

        /* Does iothread need to be locked for walking CPU list? */
        bql_lock();
        THREAD_SIBLING_FOREACH(cs, ccs) {
            PowerPCCPU *ccpu = POWERPC_CPU(ccs);
            if (target_tir == ppc_cpu_tir(ccpu)) {
                ppc_set_irq(ccpu, irq, 1);
                break;
            }
        }
        bql_unlock();
    }
}

void helper_book3s_msgclr(CPUPPCState *env, target_ulong rb)
{
    if (!dbell_type_server(rb)) {
        return;
    }

    ppc_set_irq(env_archcpu(env), PPC_INTERRUPT_HDOORBELL, 0);
}

void helper_book3s_msgsnd(CPUPPCState *env, target_ulong rb)
{
    int pir = rb & DBELL_PROCIDTAG_MASK;
    bool brdcast = false;
    CPUState *cs, *ccs;
    PowerPCCPU *cpu;

    if (!dbell_type_server(rb)) {
        return;
    }

    /* POWER8 msgsnd is like msgsndp (targets a thread within core) */
    if (!(env->insns_flags2 & PPC2_ISA300)) {
        msgsnd_core_tir(env, rb & PPC_BITMASK(57, 63), PPC_INTERRUPT_HDOORBELL);
        return;
    }

    /* POWER9 and later msgsnd is a global (targets any thread) */
    cpu = ppc_get_vcpu_by_pir(pir);
    if (!cpu) {
        return;
    }
    cs = CPU(cpu);

    if (dbell_bcast_core(rb) || (dbell_bcast_subproc(rb) &&
                                 (env->flags & POWERPC_FLAG_SMT_1LPAR))) {
        brdcast = true;
    }

    if (ppc_cpu_core_single_threaded(cs) || !brdcast) {
        ppc_set_irq(cpu, PPC_INTERRUPT_HDOORBELL, 1);
        return;
    }

    /*
     * Why is bql needed for walking CPU list? Answer seems to be because ppc
     * irq handling needs it, but ppc_set_irq takes the lock itself if needed,
     * so could this be removed?
     */
    bql_lock();
    THREAD_SIBLING_FOREACH(cs, ccs) {
        ppc_set_irq(POWERPC_CPU(ccs), PPC_INTERRUPT_HDOORBELL, 1);
    }
    bql_unlock();
}

#ifdef TARGET_PPC64
void helper_book3s_msgclrp(CPUPPCState *env, target_ulong rb)
{
    helper_hfscr_facility_check(env, HFSCR_MSGP, "msgclrp", HFSCR_IC_MSGP);

    if (!dbell_type_server(rb)) {
        return;
    }

    ppc_set_irq(env_archcpu(env), PPC_INTERRUPT_DOORBELL, 0);
}

/*
 * sends a message to another thread  on the same
 * multi-threaded processor
 */
void helper_book3s_msgsndp(CPUPPCState *env, target_ulong rb)
{
    helper_hfscr_facility_check(env, HFSCR_MSGP, "msgsndp", HFSCR_IC_MSGP);

    if (!dbell_type_server(rb)) {
        return;
    }

    msgsnd_core_tir(env, rb & PPC_BITMASK(57, 63), PPC_INTERRUPT_DOORBELL);
}
#endif /* TARGET_PPC64 */

/* Single-step tracing */
void helper_book3s_trace(CPUPPCState *env, target_ulong prev_ip)
{
    uint32_t error_code = 0;
    if (env->insns_flags2 & PPC2_ISA207S) {
        /* Load/store reporting, SRR1[35, 36] and SDAR, are not implemented. */
        env->spr[SPR_POWER_SIAR] = prev_ip;
        error_code = PPC_BIT(33);
    }
    raise_exception_err(env, POWERPC_EXCP_TRACE, error_code);
}

void ppc_cpu_do_unaligned_access(CPUState *cs, vaddr vaddr,
                                 MMUAccessType access_type,
                                 int mmu_idx, uintptr_t retaddr)
{
    CPUPPCState *env = cpu_env(cs);
    uint32_t insn;

    /* Restore state and reload the insn we executed, for filling in DSISR.  */
    cpu_restore_state(cs, retaddr);
    insn = ppc_ldl_code(env, env->nip);

    switch (env->mmu_model) {
    case POWERPC_MMU_SOFT_4xx:
        env->spr[SPR_40x_DEAR] = vaddr;
        break;
    case POWERPC_MMU_BOOKE:
    case POWERPC_MMU_BOOKE206:
        env->spr[SPR_BOOKE_DEAR] = vaddr;
        break;
    default:
        env->spr[SPR_DAR] = vaddr;
        break;
    }

    cs->exception_index = POWERPC_EXCP_ALIGN;
    env->error_code = insn & 0x03FF0000;
    cpu_loop_exit(cs);
}

void ppc_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                   vaddr vaddr, unsigned size,
                                   MMUAccessType access_type,
                                   int mmu_idx, MemTxAttrs attrs,
                                   MemTxResult response, uintptr_t retaddr)
{
    CPUPPCState *env = cpu_env(cs);

    switch (env->excp_model) {
#if defined(TARGET_PPC64)
    case POWERPC_EXCP_POWER8:
    case POWERPC_EXCP_POWER9:
    case POWERPC_EXCP_POWER10:
    case POWERPC_EXCP_POWER11:
        /*
         * Machine check codes can be found in processor User Manual or
         * Linux or skiboot source.
         */
        if (access_type == MMU_DATA_LOAD) {
            env->spr[SPR_DAR] = vaddr;
            env->spr[SPR_DSISR] = PPC_BIT(57);
            env->error_code = PPC_BIT(42);

        } else if (access_type == MMU_DATA_STORE) {
            /*
             * MCE for stores in POWER is asynchronous so hardware does
             * not set DAR, but QEMU can do better.
             */
            env->spr[SPR_DAR] = vaddr;
            env->error_code = PPC_BIT(36) | PPC_BIT(43) | PPC_BIT(45);
            env->error_code |= PPC_BIT(42);

        } else { /* Fetch */
            /*
             * is_prefix_insn_excp() tests !PPC_BIT(42) to avoid fetching
             * the instruction, so that must always be clear for fetches.
             */
            env->error_code = PPC_BIT(36) | PPC_BIT(44) | PPC_BIT(45);
        }
        break;
#endif
    default:
        /*
         * TODO: Check behaviour for other CPUs, for now do nothing.
         * Could add a basic MCE even if real hardware ignores.
         */
        return;
    }

    cs->exception_index = POWERPC_EXCP_MCHECK;
    cpu_loop_exit_restore(cs, retaddr);
}

void ppc_cpu_debug_excp_handler(CPUState *cs)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        if (cs->watchpoint_hit) {
            if (cs->watchpoint_hit->flags & BP_CPU) {
                env->spr[SPR_DAR] = cs->watchpoint_hit->hitaddr;
                env->spr[SPR_DSISR] = PPC_BIT(41);
                cs->watchpoint_hit = NULL;
                raise_exception(env, POWERPC_EXCP_DSI);
            }
            cs->watchpoint_hit = NULL;
        } else if (cpu_breakpoint_test(cs, env->nip, BP_CPU)) {
            raise_exception_err(env, POWERPC_EXCP_TRACE,
                                PPC_BIT(33) | PPC_BIT(43));
        }
    }
#endif
}

bool ppc_cpu_debug_check_breakpoint(CPUState *cs)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        target_ulong priv;

        priv = env->spr[SPR_CIABR] & PPC_BITMASK(62, 63);
        switch (priv) {
        case 0x1: /* problem */
            return env->msr & ((target_ulong)1 << MSR_PR);
        case 0x2: /* supervisor */
            return (!(env->msr & ((target_ulong)1 << MSR_PR)) &&
                    !(env->msr & ((target_ulong)1 << MSR_HV)));
        case 0x3: /* hypervisor */
            return (!(env->msr & ((target_ulong)1 << MSR_PR)) &&
                     (env->msr & ((target_ulong)1 << MSR_HV)));
        default:
            g_assert_not_reached();
        }
    }
#endif

    return false;
}

bool ppc_cpu_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
#if defined(TARGET_PPC64)
    CPUPPCState *env = cpu_env(cs);

    if (env->insns_flags2 & PPC2_ISA207S) {
        if (wp == env->dawr0_watchpoint) {
            uint32_t dawrx = env->spr[SPR_DAWRX0];
            bool wt = extract32(dawrx, PPC_BIT_NR(59), 1);
            bool wti = extract32(dawrx, PPC_BIT_NR(60), 1);
            bool hv = extract32(dawrx, PPC_BIT_NR(61), 1);
            bool sv = extract32(dawrx, PPC_BIT_NR(62), 1);
            bool pr = extract32(dawrx, PPC_BIT_NR(62), 1);

            if ((env->msr & ((target_ulong)1 << MSR_PR)) && !pr) {
                return false;
            } else if ((env->msr & ((target_ulong)1 << MSR_HV)) && !hv) {
                return false;
            } else if (!sv) {
                return false;
            }

            if (!wti) {
                if (env->msr & ((target_ulong)1 << MSR_DR)) {
                    if (!wt) {
                        return false;
                    }
                } else {
                    if (wt) {
                        return false;
                    }
                }
            }

            return true;
        }
    }
#endif

    return false;
}

#endif /* !CONFIG_USER_ONLY */
#endif /* CONFIG_TCG */
