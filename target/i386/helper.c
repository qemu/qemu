/*
 *  i386 helpers (without register variable usage)
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "qemu/qemu-print.h"
#include "sysemu/kvm.h"
#include "sysemu/runstate.h"
#include "kvm_i386.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/tcg.h"
#include "sysemu/hw_accel.h"
#include "monitor/monitor.h"
#include "hw/i386/apic_internal.h"
#endif

void cpu_sync_bndcs_hflags(CPUX86State *env)
{
    uint32_t hflags = env->hflags;
    uint32_t hflags2 = env->hflags2;
    uint32_t bndcsr;

    if ((hflags & HF_CPL_MASK) == 3) {
        bndcsr = env->bndcs_regs.cfgu;
    } else {
        bndcsr = env->msr_bndcfgs;
    }

    if ((env->cr[4] & CR4_OSXSAVE_MASK)
        && (env->xcr0 & XSTATE_BNDCSR_MASK)
        && (bndcsr & BNDCFG_ENABLE)) {
        hflags |= HF_MPX_EN_MASK;
    } else {
        hflags &= ~HF_MPX_EN_MASK;
    }

    if (bndcsr & BNDCFG_BNDPRESERVE) {
        hflags2 |= HF2_MPX_PR_MASK;
    } else {
        hflags2 &= ~HF2_MPX_PR_MASK;
    }

    env->hflags = hflags;
    env->hflags2 = hflags2;
}

static void cpu_x86_version(CPUX86State *env, int *family, int *model)
{
    int cpuver = env->cpuid_version;

    if (family == NULL || model == NULL) {
        return;
    }

    *family = (cpuver >> 8) & 0x0f;
    *model = ((cpuver >> 12) & 0xf0) + ((cpuver >> 4) & 0x0f);
}

/* Broadcast MCA signal for processor version 06H_EH and above */
int cpu_x86_support_mca_broadcast(CPUX86State *env)
{
    int family = 0;
    int model = 0;

    cpu_x86_version(env, &family, &model);
    if ((family == 6 && model >= 14) || family > 6) {
        return 1;
    }

    return 0;
}

/***********************************************************/
/* x86 debug */

static const char *cc_op_str[CC_OP_NB] = {
    "DYNAMIC",
    "EFLAGS",

    "MULB",
    "MULW",
    "MULL",
    "MULQ",

    "ADDB",
    "ADDW",
    "ADDL",
    "ADDQ",

    "ADCB",
    "ADCW",
    "ADCL",
    "ADCQ",

    "SUBB",
    "SUBW",
    "SUBL",
    "SUBQ",

    "SBBB",
    "SBBW",
    "SBBL",
    "SBBQ",

    "LOGICB",
    "LOGICW",
    "LOGICL",
    "LOGICQ",

    "INCB",
    "INCW",
    "INCL",
    "INCQ",

    "DECB",
    "DECW",
    "DECL",
    "DECQ",

    "SHLB",
    "SHLW",
    "SHLL",
    "SHLQ",

    "SARB",
    "SARW",
    "SARL",
    "SARQ",

    "BMILGB",
    "BMILGW",
    "BMILGL",
    "BMILGQ",

    "ADCX",
    "ADOX",
    "ADCOX",

    "CLR",
};

static void
cpu_x86_dump_seg_cache(CPUX86State *env, FILE *f,
                       const char *name, struct SegmentCache *sc)
{
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        qemu_fprintf(f, "%-3s=%04x %016" PRIx64 " %08x %08x", name,
                     sc->selector, sc->base, sc->limit,
                     sc->flags & 0x00ffff00);
    } else
#endif
    {
        qemu_fprintf(f, "%-3s=%04x %08x %08x %08x", name, sc->selector,
                     (uint32_t)sc->base, sc->limit,
                     sc->flags & 0x00ffff00);
    }

    if (!(env->hflags & HF_PE_MASK) || !(sc->flags & DESC_P_MASK))
        goto done;

    qemu_fprintf(f, " DPL=%d ",
                 (sc->flags & DESC_DPL_MASK) >> DESC_DPL_SHIFT);
    if (sc->flags & DESC_S_MASK) {
        if (sc->flags & DESC_CS_MASK) {
            qemu_fprintf(f, (sc->flags & DESC_L_MASK) ? "CS64" :
                         ((sc->flags & DESC_B_MASK) ? "CS32" : "CS16"));
            qemu_fprintf(f, " [%c%c", (sc->flags & DESC_C_MASK) ? 'C' : '-',
                         (sc->flags & DESC_R_MASK) ? 'R' : '-');
        } else {
            qemu_fprintf(f, (sc->flags & DESC_B_MASK
                             || env->hflags & HF_LMA_MASK)
                         ? "DS  " : "DS16");
            qemu_fprintf(f, " [%c%c", (sc->flags & DESC_E_MASK) ? 'E' : '-',
                         (sc->flags & DESC_W_MASK) ? 'W' : '-');
        }
        qemu_fprintf(f, "%c]", (sc->flags & DESC_A_MASK) ? 'A' : '-');
    } else {
        static const char *sys_type_name[2][16] = {
            { /* 32 bit mode */
                "Reserved", "TSS16-avl", "LDT", "TSS16-busy",
                "CallGate16", "TaskGate", "IntGate16", "TrapGate16",
                "Reserved", "TSS32-avl", "Reserved", "TSS32-busy",
                "CallGate32", "Reserved", "IntGate32", "TrapGate32"
            },
            { /* 64 bit mode */
                "<hiword>", "Reserved", "LDT", "Reserved", "Reserved",
                "Reserved", "Reserved", "Reserved", "Reserved",
                "TSS64-avl", "Reserved", "TSS64-busy", "CallGate64",
                "Reserved", "IntGate64", "TrapGate64"
            }
        };
        qemu_fprintf(f, "%s",
                     sys_type_name[(env->hflags & HF_LMA_MASK) ? 1 : 0]
                     [(sc->flags & DESC_TYPE_MASK) >> DESC_TYPE_SHIFT]);
    }
done:
    qemu_fprintf(f, "\n");
}

#ifndef CONFIG_USER_ONLY

/* ARRAY_SIZE check is not required because
 * DeliveryMode(dm) has a size of 3 bit.
 */
static inline const char *dm2str(uint32_t dm)
{
    static const char *str[] = {
        "Fixed",
        "...",
        "SMI",
        "...",
        "NMI",
        "INIT",
        "...",
        "ExtINT"
    };
    return str[dm];
}

static void dump_apic_lvt(const char *name, uint32_t lvt, bool is_timer)
{
    uint32_t dm = (lvt & APIC_LVT_DELIV_MOD) >> APIC_LVT_DELIV_MOD_SHIFT;
    qemu_printf("%s\t 0x%08x %s %-5s %-6s %-7s %-12s %-6s",
                name, lvt,
                lvt & APIC_LVT_INT_POLARITY ? "active-lo" : "active-hi",
                lvt & APIC_LVT_LEVEL_TRIGGER ? "level" : "edge",
                lvt & APIC_LVT_MASKED ? "masked" : "",
                lvt & APIC_LVT_DELIV_STS ? "pending" : "",
                !is_timer ?
                    "" : lvt & APIC_LVT_TIMER_PERIODIC ?
                            "periodic" : lvt & APIC_LVT_TIMER_TSCDEADLINE ?
                                            "tsc-deadline" : "one-shot",
                dm2str(dm));
    if (dm != APIC_DM_NMI) {
        qemu_printf(" (vec %u)\n", lvt & APIC_VECTOR_MASK);
    } else {
        qemu_printf("\n");
    }
}

/* ARRAY_SIZE check is not required because
 * destination shorthand has a size of 2 bit.
 */
static inline const char *shorthand2str(uint32_t shorthand)
{
    const char *str[] = {
        "no-shorthand", "self", "all-self", "all"
    };
    return str[shorthand];
}

static inline uint8_t divider_conf(uint32_t divide_conf)
{
    uint8_t divide_val = ((divide_conf & 0x8) >> 1) | (divide_conf & 0x3);

    return divide_val == 7 ? 1 : 2 << divide_val;
}

static inline void mask2str(char *str, uint32_t val, uint8_t size)
{
    while (size--) {
        *str++ = (val >> size) & 1 ? '1' : '0';
    }
    *str = 0;
}

#define MAX_LOGICAL_APIC_ID_MASK_SIZE 16

static void dump_apic_icr(APICCommonState *s, CPUX86State *env)
{
    uint32_t icr = s->icr[0], icr2 = s->icr[1];
    uint8_t dest_shorthand = \
        (icr & APIC_ICR_DEST_SHORT) >> APIC_ICR_DEST_SHORT_SHIFT;
    bool logical_mod = icr & APIC_ICR_DEST_MOD;
    char apic_id_str[MAX_LOGICAL_APIC_ID_MASK_SIZE + 1];
    uint32_t dest_field;
    bool x2apic;

    qemu_printf("ICR\t 0x%08x %s %s %s %s\n",
                icr,
                logical_mod ? "logical" : "physical",
                icr & APIC_ICR_TRIGGER_MOD ? "level" : "edge",
                icr & APIC_ICR_LEVEL ? "assert" : "de-assert",
                shorthand2str(dest_shorthand));

    qemu_printf("ICR2\t 0x%08x", icr2);
    if (dest_shorthand != 0) {
        qemu_printf("\n");
        return;
    }
    x2apic = env->features[FEAT_1_ECX] & CPUID_EXT_X2APIC;
    dest_field = x2apic ? icr2 : icr2 >> APIC_ICR_DEST_SHIFT;

    if (!logical_mod) {
        if (x2apic) {
            qemu_printf(" cpu %u (X2APIC ID)\n", dest_field);
        } else {
            qemu_printf(" cpu %u (APIC ID)\n",
                        dest_field & APIC_LOGDEST_XAPIC_ID);
        }
        return;
    }

    if (s->dest_mode == 0xf) { /* flat mode */
        mask2str(apic_id_str, icr2 >> APIC_ICR_DEST_SHIFT, 8);
        qemu_printf(" mask %s (APIC ID)\n", apic_id_str);
    } else if (s->dest_mode == 0) { /* cluster mode */
        if (x2apic) {
            mask2str(apic_id_str, dest_field & APIC_LOGDEST_X2APIC_ID, 16);
            qemu_printf(" cluster %u mask %s (X2APIC ID)\n",
                        dest_field >> APIC_LOGDEST_X2APIC_SHIFT, apic_id_str);
        } else {
            mask2str(apic_id_str, dest_field & APIC_LOGDEST_XAPIC_ID, 4);
            qemu_printf(" cluster %u mask %s (APIC ID)\n",
                        dest_field >> APIC_LOGDEST_XAPIC_SHIFT, apic_id_str);
        }
    }
}

static void dump_apic_interrupt(const char *name, uint32_t *ireg_tab,
                                uint32_t *tmr_tab)
{
    int i, empty = true;

    qemu_printf("%s\t ", name);
    for (i = 0; i < 256; i++) {
        if (apic_get_bit(ireg_tab, i)) {
            qemu_printf("%u%s ", i,
                        apic_get_bit(tmr_tab, i) ? "(level)" : "");
            empty = false;
        }
    }
    qemu_printf("%s\n", empty ? "(none)" : "");
}

void x86_cpu_dump_local_apic_state(CPUState *cs, int flags)
{
    X86CPU *cpu = X86_CPU(cs);
    APICCommonState *s = APIC_COMMON(cpu->apic_state);
    if (!s) {
        qemu_printf("local apic state not available\n");
        return;
    }
    uint32_t *lvt = s->lvt;

    qemu_printf("dumping local APIC state for CPU %-2u\n\n",
                CPU(cpu)->cpu_index);
    dump_apic_lvt("LVT0", lvt[APIC_LVT_LINT0], false);
    dump_apic_lvt("LVT1", lvt[APIC_LVT_LINT1], false);
    dump_apic_lvt("LVTPC", lvt[APIC_LVT_PERFORM], false);
    dump_apic_lvt("LVTERR", lvt[APIC_LVT_ERROR], false);
    dump_apic_lvt("LVTTHMR", lvt[APIC_LVT_THERMAL], false);
    dump_apic_lvt("LVTT", lvt[APIC_LVT_TIMER], true);

    qemu_printf("Timer\t DCR=0x%x (divide by %u) initial_count = %u"
                " current_count = %u\n",
                s->divide_conf & APIC_DCR_MASK,
                divider_conf(s->divide_conf),
                s->initial_count, apic_get_current_count(s));

    qemu_printf("SPIV\t 0x%08x APIC %s, focus=%s, spurious vec %u\n",
                s->spurious_vec,
                s->spurious_vec & APIC_SPURIO_ENABLED ? "enabled" : "disabled",
                s->spurious_vec & APIC_SPURIO_FOCUS ? "on" : "off",
                s->spurious_vec & APIC_VECTOR_MASK);

    dump_apic_icr(s, &cpu->env);

    qemu_printf("ESR\t 0x%08x\n", s->esr);

    dump_apic_interrupt("ISR", s->isr, s->tmr);
    dump_apic_interrupt("IRR", s->irr, s->tmr);

    qemu_printf("\nAPR 0x%02x TPR 0x%02x DFR 0x%02x LDR 0x%02x",
                s->arb_id, s->tpr, s->dest_mode, s->log_dest);
    if (s->dest_mode == 0) {
        qemu_printf("(cluster %u: id %u)",
                    s->log_dest >> APIC_LOGDEST_XAPIC_SHIFT,
                    s->log_dest & APIC_LOGDEST_XAPIC_ID);
    }
    qemu_printf(" PPR 0x%02x\n", apic_get_ppr(s));
}
#else
void x86_cpu_dump_local_apic_state(CPUState *cs, int flags)
{
}
#endif /* !CONFIG_USER_ONLY */

#define DUMP_CODE_BYTES_TOTAL    50
#define DUMP_CODE_BYTES_BACKWARD 20

void x86_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int eflags, i, nb;
    char cc_op_name[32];
    static const char *seg_name[6] = { "ES", "CS", "SS", "DS", "FS", "GS" };

    eflags = cpu_compute_eflags(env);
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        qemu_fprintf(f, "RAX=%016" PRIx64 " RBX=%016" PRIx64 " RCX=%016" PRIx64 " RDX=%016" PRIx64 "\n"
                     "RSI=%016" PRIx64 " RDI=%016" PRIx64 " RBP=%016" PRIx64 " RSP=%016" PRIx64 "\n"
                     "R8 =%016" PRIx64 " R9 =%016" PRIx64 " R10=%016" PRIx64 " R11=%016" PRIx64 "\n"
                     "R12=%016" PRIx64 " R13=%016" PRIx64 " R14=%016" PRIx64 " R15=%016" PRIx64 "\n"
                     "RIP=%016" PRIx64 " RFL=%08x [%c%c%c%c%c%c%c] CPL=%d II=%d A20=%d SMM=%d HLT=%d\n",
                     env->regs[R_EAX],
                     env->regs[R_EBX],
                     env->regs[R_ECX],
                     env->regs[R_EDX],
                     env->regs[R_ESI],
                     env->regs[R_EDI],
                     env->regs[R_EBP],
                     env->regs[R_ESP],
                     env->regs[8],
                     env->regs[9],
                     env->regs[10],
                     env->regs[11],
                     env->regs[12],
                     env->regs[13],
                     env->regs[14],
                     env->regs[15],
                     env->eip, eflags,
                     eflags & DF_MASK ? 'D' : '-',
                     eflags & CC_O ? 'O' : '-',
                     eflags & CC_S ? 'S' : '-',
                     eflags & CC_Z ? 'Z' : '-',
                     eflags & CC_A ? 'A' : '-',
                     eflags & CC_P ? 'P' : '-',
                     eflags & CC_C ? 'C' : '-',
                     env->hflags & HF_CPL_MASK,
                     (env->hflags >> HF_INHIBIT_IRQ_SHIFT) & 1,
                     (env->a20_mask >> 20) & 1,
                     (env->hflags >> HF_SMM_SHIFT) & 1,
                     cs->halted);
    } else
#endif
    {
        qemu_fprintf(f, "EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
                     "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
                     "EIP=%08x EFL=%08x [%c%c%c%c%c%c%c] CPL=%d II=%d A20=%d SMM=%d HLT=%d\n",
                     (uint32_t)env->regs[R_EAX],
                     (uint32_t)env->regs[R_EBX],
                     (uint32_t)env->regs[R_ECX],
                     (uint32_t)env->regs[R_EDX],
                     (uint32_t)env->regs[R_ESI],
                     (uint32_t)env->regs[R_EDI],
                     (uint32_t)env->regs[R_EBP],
                     (uint32_t)env->regs[R_ESP],
                     (uint32_t)env->eip, eflags,
                     eflags & DF_MASK ? 'D' : '-',
                     eflags & CC_O ? 'O' : '-',
                     eflags & CC_S ? 'S' : '-',
                     eflags & CC_Z ? 'Z' : '-',
                     eflags & CC_A ? 'A' : '-',
                     eflags & CC_P ? 'P' : '-',
                     eflags & CC_C ? 'C' : '-',
                     env->hflags & HF_CPL_MASK,
                     (env->hflags >> HF_INHIBIT_IRQ_SHIFT) & 1,
                     (env->a20_mask >> 20) & 1,
                     (env->hflags >> HF_SMM_SHIFT) & 1,
                     cs->halted);
    }

    for(i = 0; i < 6; i++) {
        cpu_x86_dump_seg_cache(env, f, seg_name[i], &env->segs[i]);
    }
    cpu_x86_dump_seg_cache(env, f, "LDT", &env->ldt);
    cpu_x86_dump_seg_cache(env, f, "TR", &env->tr);

#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        qemu_fprintf(f, "GDT=     %016" PRIx64 " %08x\n",
                     env->gdt.base, env->gdt.limit);
        qemu_fprintf(f, "IDT=     %016" PRIx64 " %08x\n",
                     env->idt.base, env->idt.limit);
        qemu_fprintf(f, "CR0=%08x CR2=%016" PRIx64 " CR3=%016" PRIx64 " CR4=%08x\n",
                     (uint32_t)env->cr[0],
                     env->cr[2],
                     env->cr[3],
                     (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++)
            qemu_fprintf(f, "DR%d=%016" PRIx64 " ", i, env->dr[i]);
        qemu_fprintf(f, "\nDR6=%016" PRIx64 " DR7=%016" PRIx64 "\n",
                     env->dr[6], env->dr[7]);
    } else
#endif
    {
        qemu_fprintf(f, "GDT=     %08x %08x\n",
                     (uint32_t)env->gdt.base, env->gdt.limit);
        qemu_fprintf(f, "IDT=     %08x %08x\n",
                     (uint32_t)env->idt.base, env->idt.limit);
        qemu_fprintf(f, "CR0=%08x CR2=%08x CR3=%08x CR4=%08x\n",
                     (uint32_t)env->cr[0],
                     (uint32_t)env->cr[2],
                     (uint32_t)env->cr[3],
                     (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++) {
            qemu_fprintf(f, "DR%d=" TARGET_FMT_lx " ", i, env->dr[i]);
        }
        qemu_fprintf(f, "\nDR6=" TARGET_FMT_lx " DR7=" TARGET_FMT_lx "\n",
                     env->dr[6], env->dr[7]);
    }
    if (flags & CPU_DUMP_CCOP) {
        if ((unsigned)env->cc_op < CC_OP_NB)
            snprintf(cc_op_name, sizeof(cc_op_name), "%s", cc_op_str[env->cc_op]);
        else
            snprintf(cc_op_name, sizeof(cc_op_name), "[%d]", env->cc_op);
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            qemu_fprintf(f, "CCS=%016" PRIx64 " CCD=%016" PRIx64 " CCO=%-8s\n",
                         env->cc_src, env->cc_dst,
                         cc_op_name);
        } else
#endif
        {
            qemu_fprintf(f, "CCS=%08x CCD=%08x CCO=%-8s\n",
                         (uint32_t)env->cc_src, (uint32_t)env->cc_dst,
                         cc_op_name);
        }
    }
    qemu_fprintf(f, "EFER=%016" PRIx64 "\n", env->efer);
    if (flags & CPU_DUMP_FPU) {
        int fptag;
        fptag = 0;
        for(i = 0; i < 8; i++) {
            fptag |= ((!env->fptags[i]) << i);
        }
        update_mxcsr_from_sse_status(env);
        qemu_fprintf(f, "FCW=%04x FSW=%04x [ST=%d] FTW=%02x MXCSR=%08x\n",
                     env->fpuc,
                     (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11,
                     env->fpstt,
                     fptag,
                     env->mxcsr);
        for(i=0;i<8;i++) {
            CPU_LDoubleU u;
            u.d = env->fpregs[i].d;
            qemu_fprintf(f, "FPR%d=%016" PRIx64 " %04x",
                         i, u.l.lower, u.l.upper);
            if ((i & 1) == 1)
                qemu_fprintf(f, "\n");
            else
                qemu_fprintf(f, " ");
        }
        if (env->hflags & HF_CS64_MASK)
            nb = 16;
        else
            nb = 8;
        for(i=0;i<nb;i++) {
            qemu_fprintf(f, "XMM%02d=%08x%08x%08x%08x",
                         i,
                         env->xmm_regs[i].ZMM_L(3),
                         env->xmm_regs[i].ZMM_L(2),
                         env->xmm_regs[i].ZMM_L(1),
                         env->xmm_regs[i].ZMM_L(0));
            if ((i & 1) == 1)
                qemu_fprintf(f, "\n");
            else
                qemu_fprintf(f, " ");
        }
    }
    if (flags & CPU_DUMP_CODE) {
        target_ulong base = env->segs[R_CS].base + env->eip;
        target_ulong offs = MIN(env->eip, DUMP_CODE_BYTES_BACKWARD);
        uint8_t code;
        char codestr[3];

        qemu_fprintf(f, "Code=");
        for (i = 0; i < DUMP_CODE_BYTES_TOTAL; i++) {
            if (cpu_memory_rw_debug(cs, base - offs + i, &code, 1, 0) == 0) {
                snprintf(codestr, sizeof(codestr), "%02x", code);
            } else {
                snprintf(codestr, sizeof(codestr), "??");
            }
            qemu_fprintf(f, "%s%s%s%s", i > 0 ? " " : "",
                         i == offs ? "<" : "", codestr, i == offs ? ">" : "");
        }
        qemu_fprintf(f, "\n");
    }
}

/***********************************************************/
/* x86 mmu */
/* XXX: add PGE support */

void x86_cpu_set_a20(X86CPU *cpu, int a20_state)
{
    CPUX86State *env = &cpu->env;

    a20_state = (a20_state != 0);
    if (a20_state != ((env->a20_mask >> 20) & 1)) {
        CPUState *cs = CPU(cpu);

        qemu_log_mask(CPU_LOG_MMU, "A20 update: a20=%d\n", a20_state);
        /* if the cpu is currently executing code, we must unlink it and
           all the potentially executing TB */
        cpu_interrupt(cs, CPU_INTERRUPT_EXITTB);

        /* when a20 is changed, all the MMU mappings are invalid, so
           we must flush everything */
        tlb_flush(cs);
        env->a20_mask = ~(1 << 20) | (a20_state << 20);
    }
}

void cpu_x86_update_cr0(CPUX86State *env, uint32_t new_cr0)
{
    X86CPU *cpu = env_archcpu(env);
    int pe_state;

    qemu_log_mask(CPU_LOG_MMU, "CR0 update: CR0=0x%08x\n", new_cr0);
    if ((new_cr0 & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK)) !=
        (env->cr[0] & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK))) {
        tlb_flush(CPU(cpu));
    }

#ifdef TARGET_X86_64
    if (!(env->cr[0] & CR0_PG_MASK) && (new_cr0 & CR0_PG_MASK) &&
        (env->efer & MSR_EFER_LME)) {
        /* enter in long mode */
        /* XXX: generate an exception */
        if (!(env->cr[4] & CR4_PAE_MASK))
            return;
        env->efer |= MSR_EFER_LMA;
        env->hflags |= HF_LMA_MASK;
    } else if ((env->cr[0] & CR0_PG_MASK) && !(new_cr0 & CR0_PG_MASK) &&
               (env->efer & MSR_EFER_LMA)) {
        /* exit long mode */
        env->efer &= ~MSR_EFER_LMA;
        env->hflags &= ~(HF_LMA_MASK | HF_CS64_MASK);
        env->eip &= 0xffffffff;
    }
#endif
    env->cr[0] = new_cr0 | CR0_ET_MASK;

    /* update PE flag in hidden flags */
    pe_state = (env->cr[0] & CR0_PE_MASK);
    env->hflags = (env->hflags & ~HF_PE_MASK) | (pe_state << HF_PE_SHIFT);
    /* ensure that ADDSEG is always set in real mode */
    env->hflags |= ((pe_state ^ 1) << HF_ADDSEG_SHIFT);
    /* update FPU flags */
    env->hflags = (env->hflags & ~(HF_MP_MASK | HF_EM_MASK | HF_TS_MASK)) |
        ((new_cr0 << (HF_MP_SHIFT - 1)) & (HF_MP_MASK | HF_EM_MASK | HF_TS_MASK));
}

/* XXX: in legacy PAE mode, generate a GPF if reserved bits are set in
   the PDPT */
void cpu_x86_update_cr3(CPUX86State *env, target_ulong new_cr3)
{
    env->cr[3] = new_cr3;
    if (env->cr[0] & CR0_PG_MASK) {
        qemu_log_mask(CPU_LOG_MMU,
                        "CR3 update: CR3=" TARGET_FMT_lx "\n", new_cr3);
        tlb_flush(env_cpu(env));
    }
}

void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4)
{
    uint32_t hflags;

#if defined(DEBUG_MMU)
    printf("CR4 update: %08x -> %08x\n", (uint32_t)env->cr[4], new_cr4);
#endif
    if ((new_cr4 ^ env->cr[4]) &
        (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK |
         CR4_SMEP_MASK | CR4_SMAP_MASK | CR4_LA57_MASK)) {
        tlb_flush(env_cpu(env));
    }

    /* Clear bits we're going to recompute.  */
    hflags = env->hflags & ~(HF_OSFXSR_MASK | HF_SMAP_MASK);

    /* SSE handling */
    if (!(env->features[FEAT_1_EDX] & CPUID_SSE)) {
        new_cr4 &= ~CR4_OSFXSR_MASK;
    }
    if (new_cr4 & CR4_OSFXSR_MASK) {
        hflags |= HF_OSFXSR_MASK;
    }

    if (!(env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_SMAP)) {
        new_cr4 &= ~CR4_SMAP_MASK;
    }
    if (new_cr4 & CR4_SMAP_MASK) {
        hflags |= HF_SMAP_MASK;
    }

    if (!(env->features[FEAT_7_0_ECX] & CPUID_7_0_ECX_PKU)) {
        new_cr4 &= ~CR4_PKE_MASK;
    }

    env->cr[4] = new_cr4;
    env->hflags = hflags;

    cpu_sync_bndcs_hflags(env);
}

#if !defined(CONFIG_USER_ONLY)
hwaddr x86_cpu_get_phys_page_attrs_debug(CPUState *cs, vaddr addr,
                                         MemTxAttrs *attrs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    target_ulong pde_addr, pte_addr;
    uint64_t pte;
    int32_t a20_mask;
    uint32_t page_offset;
    int page_size;

    *attrs = cpu_get_mem_attrs(env);

    a20_mask = x86_get_a20_mask(env);
    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr & a20_mask;
        page_size = 4096;
    } else if (env->cr[4] & CR4_PAE_MASK) {
        target_ulong pdpe_addr;
        uint64_t pde, pdpe;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            bool la57 = env->cr[4] & CR4_LA57_MASK;
            uint64_t pml5e_addr, pml5e;
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = la57 ? (int64_t)addr >> 56 : (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                return -1;
            }

            if (la57) {
                pml5e_addr = ((env->cr[3] & ~0xfff) +
                        (((addr >> 48) & 0x1ff) << 3)) & a20_mask;
                pml5e = x86_ldq_phys(cs, pml5e_addr);
                if (!(pml5e & PG_PRESENT_MASK)) {
                    return -1;
                }
            } else {
                pml5e = env->cr[3];
            }

            pml4e_addr = ((pml5e & PG_ADDRESS_MASK) +
                    (((addr >> 39) & 0x1ff) << 3)) & a20_mask;
            pml4e = x86_ldq_phys(cs, pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                return -1;
            }
            pdpe_addr = ((pml4e & PG_ADDRESS_MASK) +
                         (((addr >> 30) & 0x1ff) << 3)) & a20_mask;
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                return -1;
            }
            if (pdpe & PG_PSE_MASK) {
                page_size = 1024 * 1024 * 1024;
                pte = pdpe;
                goto out;
            }

        } else
#endif
        {
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                a20_mask;
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK))
                return -1;
        }

        pde_addr = ((pdpe & PG_ADDRESS_MASK) +
                    (((addr >> 21) & 0x1ff) << 3)) & a20_mask;
        pde = x86_ldq_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            return -1;
        }
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte = pde;
        } else {
            /* 4 KB page */
            pte_addr = ((pde & PG_ADDRESS_MASK) +
                        (((addr >> 12) & 0x1ff) << 3)) & a20_mask;
            page_size = 4096;
            pte = x86_ldq_phys(cs, pte_addr);
        }
        if (!(pte & PG_PRESENT_MASK)) {
            return -1;
        }
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) & a20_mask;
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK))
            return -1;
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            page_size = 4096 * 1024;
        } else {
            /* page directory entry */
            pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & a20_mask;
            pte = x86_ldl_phys(cs, pte_addr);
            if (!(pte & PG_PRESENT_MASK)) {
                return -1;
            }
            page_size = 4096;
        }
        pte = pte & a20_mask;
    }

#ifdef TARGET_X86_64
out:
#endif
    pte &= PG_ADDRESS_MASK & ~(page_size - 1);
    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    return pte | page_offset;
}

typedef struct MCEInjectionParams {
    Monitor *mon;
    int bank;
    uint64_t status;
    uint64_t mcg_status;
    uint64_t addr;
    uint64_t misc;
    int flags;
} MCEInjectionParams;

static void do_inject_x86_mce(CPUState *cs, run_on_cpu_data data)
{
    MCEInjectionParams *params = data.host_ptr;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *cenv = &cpu->env;
    uint64_t *banks = cenv->mce_banks + 4 * params->bank;

    cpu_synchronize_state(cs);

    /*
     * If there is an MCE exception being processed, ignore this SRAO MCE
     * unless unconditional injection was requested.
     */
    if (!(params->flags & MCE_INJECT_UNCOND_AO)
        && !(params->status & MCI_STATUS_AR)
        && (cenv->mcg_status & MCG_STATUS_MCIP)) {
        return;
    }

    if (params->status & MCI_STATUS_UC) {
        /*
         * if MSR_MCG_CTL is not all 1s, the uncorrected error
         * reporting is disabled
         */
        if ((cenv->mcg_cap & MCG_CTL_P) && cenv->mcg_ctl != ~(uint64_t)0) {
            monitor_printf(params->mon,
                           "CPU %d: Uncorrected error reporting disabled\n",
                           cs->cpu_index);
            return;
        }

        /*
         * if MSR_MCi_CTL is not all 1s, the uncorrected error
         * reporting is disabled for the bank
         */
        if (banks[0] != ~(uint64_t)0) {
            monitor_printf(params->mon,
                           "CPU %d: Uncorrected error reporting disabled for"
                           " bank %d\n",
                           cs->cpu_index, params->bank);
            return;
        }

        if ((cenv->mcg_status & MCG_STATUS_MCIP) ||
            !(cenv->cr[4] & CR4_MCE_MASK)) {
            monitor_printf(params->mon,
                           "CPU %d: Previous MCE still in progress, raising"
                           " triple fault\n",
                           cs->cpu_index);
            qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return;
        }
        if (banks[1] & MCI_STATUS_VAL) {
            params->status |= MCI_STATUS_OVER;
        }
        banks[2] = params->addr;
        banks[3] = params->misc;
        cenv->mcg_status = params->mcg_status;
        banks[1] = params->status;
        cpu_interrupt(cs, CPU_INTERRUPT_MCE);
    } else if (!(banks[1] & MCI_STATUS_VAL)
               || !(banks[1] & MCI_STATUS_UC)) {
        if (banks[1] & MCI_STATUS_VAL) {
            params->status |= MCI_STATUS_OVER;
        }
        banks[2] = params->addr;
        banks[3] = params->misc;
        banks[1] = params->status;
    } else {
        banks[1] |= MCI_STATUS_OVER;
    }
}

void cpu_x86_inject_mce(Monitor *mon, X86CPU *cpu, int bank,
                        uint64_t status, uint64_t mcg_status, uint64_t addr,
                        uint64_t misc, int flags)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *cenv = &cpu->env;
    MCEInjectionParams params = {
        .mon = mon,
        .bank = bank,
        .status = status,
        .mcg_status = mcg_status,
        .addr = addr,
        .misc = misc,
        .flags = flags,
    };
    unsigned bank_num = cenv->mcg_cap & 0xff;

    if (!cenv->mcg_cap) {
        monitor_printf(mon, "MCE injection not supported\n");
        return;
    }
    if (bank >= bank_num) {
        monitor_printf(mon, "Invalid MCE bank number\n");
        return;
    }
    if (!(status & MCI_STATUS_VAL)) {
        monitor_printf(mon, "Invalid MCE status code\n");
        return;
    }
    if ((flags & MCE_INJECT_BROADCAST)
        && !cpu_x86_support_mca_broadcast(cenv)) {
        monitor_printf(mon, "Guest CPU does not support MCA broadcast\n");
        return;
    }

    run_on_cpu(cs, do_inject_x86_mce, RUN_ON_CPU_HOST_PTR(&params));
    if (flags & MCE_INJECT_BROADCAST) {
        CPUState *other_cs;

        params.bank = 1;
        params.status = MCI_STATUS_VAL | MCI_STATUS_UC;
        params.mcg_status = MCG_STATUS_MCIP | MCG_STATUS_RIPV;
        params.addr = 0;
        params.misc = 0;
        CPU_FOREACH(other_cs) {
            if (other_cs == cs) {
                continue;
            }
            run_on_cpu(other_cs, do_inject_x86_mce, RUN_ON_CPU_HOST_PTR(&params));
        }
    }
}

void cpu_report_tpr_access(CPUX86State *env, TPRAccess access)
{
    X86CPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);

    if (kvm_enabled() || whpx_enabled()) {
        env->tpr_access_type = access;

        cpu_interrupt(cs, CPU_INTERRUPT_TPR);
    } else if (tcg_enabled()) {
        cpu_restore_state(cs, cs->mem_io_pc, false);

        apic_handle_tpr_access_report(cpu->apic_state, env->eip, access);
    }
}
#endif /* !CONFIG_USER_ONLY */

int cpu_x86_get_descr_debug(CPUX86State *env, unsigned int selector,
                            target_ulong *base, unsigned int *limit,
                            unsigned int *flags)
{
    CPUState *cs = env_cpu(env);
    SegmentCache *dt;
    target_ulong ptr;
    uint32_t e1, e2;
    int index;

    if (selector & 0x4)
        dt = &env->ldt;
    else
        dt = &env->gdt;
    index = selector & ~7;
    ptr = dt->base + index;
    if ((index + 7) > dt->limit
        || cpu_memory_rw_debug(cs, ptr, (uint8_t *)&e1, sizeof(e1), 0) != 0
        || cpu_memory_rw_debug(cs, ptr+4, (uint8_t *)&e2, sizeof(e2), 0) != 0)
        return 0;

    *base = ((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
    *limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK)
        *limit = (*limit << 12) | 0xfff;
    *flags = e2;

    return 1;
}

#if !defined(CONFIG_USER_ONLY)
void do_cpu_init(X86CPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUX86State *env = &cpu->env;
    CPUX86State *save = g_new(CPUX86State, 1);
    int sipi = cs->interrupt_request & CPU_INTERRUPT_SIPI;

    *save = *env;

    cpu_reset(cs);
    cs->interrupt_request = sipi;
    memcpy(&env->start_init_save, &save->start_init_save,
           offsetof(CPUX86State, end_init_save) -
           offsetof(CPUX86State, start_init_save));
    g_free(save);

    if (kvm_enabled()) {
        kvm_arch_do_init_vcpu(cpu);
    }
    apic_init_reset(cpu->apic_state);
}

void do_cpu_sipi(X86CPU *cpu)
{
    apic_sipi(cpu->apic_state);
}
#else
void do_cpu_init(X86CPU *cpu)
{
}
void do_cpu_sipi(X86CPU *cpu)
{
}
#endif

/* Frob eflags into and out of the CPU temporary format.  */

void x86_cpu_exec_enter(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    CC_SRC = env->eflags & (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
    env->df = 1 - (2 * ((env->eflags >> 10) & 1));
    CC_OP = CC_OP_EFLAGS;
    env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);
}

void x86_cpu_exec_exit(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    env->eflags = cpu_compute_eflags(env);
}

#ifndef CONFIG_USER_ONLY
uint8_t x86_ldub_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_ldub(as, addr, attrs, NULL);
}

uint32_t x86_lduw_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_lduw(as, addr, attrs, NULL);
}

uint32_t x86_ldl_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_ldl(as, addr, attrs, NULL);
}

uint64_t x86_ldq_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    return address_space_ldq(as, addr, attrs, NULL);
}

void x86_stb_phys(CPUState *cs, hwaddr addr, uint8_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stb(as, addr, val, attrs, NULL);
}

void x86_stl_phys_notdirty(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stl_notdirty(as, addr, val, attrs, NULL);
}

void x86_stw_phys(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stw(as, addr, val, attrs, NULL);
}

void x86_stl_phys(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stl(as, addr, val, attrs, NULL);
}

void x86_stq_phys(CPUState *cs, hwaddr addr, uint64_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    MemTxAttrs attrs = cpu_get_mem_attrs(env);
    AddressSpace *as = cpu_addressspace(cs, attrs);

    address_space_stq(as, addr, val, attrs, NULL);
}
#endif
