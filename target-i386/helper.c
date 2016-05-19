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
#include "sysemu/kvm.h"
#include "kvm_i386.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/sysemu.h"
#include "monitor/monitor.h"
#include "hw/i386/apic_internal.h"
#endif

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
cpu_x86_dump_seg_cache(CPUX86State *env, FILE *f, fprintf_function cpu_fprintf,
                       const char *name, struct SegmentCache *sc)
{
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        cpu_fprintf(f, "%-3s=%04x %016" PRIx64 " %08x %08x", name,
                    sc->selector, sc->base, sc->limit, sc->flags & 0x00ffff00);
    } else
#endif
    {
        cpu_fprintf(f, "%-3s=%04x %08x %08x %08x", name, sc->selector,
                    (uint32_t)sc->base, sc->limit, sc->flags & 0x00ffff00);
    }

    if (!(env->hflags & HF_PE_MASK) || !(sc->flags & DESC_P_MASK))
        goto done;

    cpu_fprintf(f, " DPL=%d ", (sc->flags & DESC_DPL_MASK) >> DESC_DPL_SHIFT);
    if (sc->flags & DESC_S_MASK) {
        if (sc->flags & DESC_CS_MASK) {
            cpu_fprintf(f, (sc->flags & DESC_L_MASK) ? "CS64" :
                           ((sc->flags & DESC_B_MASK) ? "CS32" : "CS16"));
            cpu_fprintf(f, " [%c%c", (sc->flags & DESC_C_MASK) ? 'C' : '-',
                        (sc->flags & DESC_R_MASK) ? 'R' : '-');
        } else {
            cpu_fprintf(f,
                        (sc->flags & DESC_B_MASK || env->hflags & HF_LMA_MASK)
                        ? "DS  " : "DS16");
            cpu_fprintf(f, " [%c%c", (sc->flags & DESC_E_MASK) ? 'E' : '-',
                        (sc->flags & DESC_W_MASK) ? 'W' : '-');
        }
        cpu_fprintf(f, "%c]", (sc->flags & DESC_A_MASK) ? 'A' : '-');
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
        cpu_fprintf(f, "%s",
                    sys_type_name[(env->hflags & HF_LMA_MASK) ? 1 : 0]
                                 [(sc->flags & DESC_TYPE_MASK)
                                  >> DESC_TYPE_SHIFT]);
    }
done:
    cpu_fprintf(f, "\n");
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

static void dump_apic_lvt(FILE *f, fprintf_function cpu_fprintf,
                          const char *name, uint32_t lvt, bool is_timer)
{
    uint32_t dm = (lvt & APIC_LVT_DELIV_MOD) >> APIC_LVT_DELIV_MOD_SHIFT;
    cpu_fprintf(f,
                "%s\t 0x%08x %s %-5s %-6s %-7s %-12s %-6s",
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
        cpu_fprintf(f, " (vec %u)\n", lvt & APIC_VECTOR_MASK);
    } else {
        cpu_fprintf(f, "\n");
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

static void dump_apic_icr(FILE *f, fprintf_function cpu_fprintf,
                          APICCommonState *s, CPUX86State *env)
{
    uint32_t icr = s->icr[0], icr2 = s->icr[1];
    uint8_t dest_shorthand = \
        (icr & APIC_ICR_DEST_SHORT) >> APIC_ICR_DEST_SHORT_SHIFT;
    bool logical_mod = icr & APIC_ICR_DEST_MOD;
    char apic_id_str[MAX_LOGICAL_APIC_ID_MASK_SIZE + 1];
    uint32_t dest_field;
    bool x2apic;

    cpu_fprintf(f, "ICR\t 0x%08x %s %s %s %s\n",
                icr,
                logical_mod ? "logical" : "physical",
                icr & APIC_ICR_TRIGGER_MOD ? "level" : "edge",
                icr & APIC_ICR_LEVEL ? "assert" : "de-assert",
                shorthand2str(dest_shorthand));

    cpu_fprintf(f, "ICR2\t 0x%08x", icr2);
    if (dest_shorthand != 0) {
        cpu_fprintf(f, "\n");
        return;
    }
    x2apic = env->features[FEAT_1_ECX] & CPUID_EXT_X2APIC;
    dest_field = x2apic ? icr2 : icr2 >> APIC_ICR_DEST_SHIFT;

    if (!logical_mod) {
        if (x2apic) {
            cpu_fprintf(f, " cpu %u (X2APIC ID)\n", dest_field);
        } else {
            cpu_fprintf(f, " cpu %u (APIC ID)\n",
                        dest_field & APIC_LOGDEST_XAPIC_ID);
        }
        return;
    }

    if (s->dest_mode == 0xf) { /* flat mode */
        mask2str(apic_id_str, icr2 >> APIC_ICR_DEST_SHIFT, 8);
        cpu_fprintf(f, " mask %s (APIC ID)\n", apic_id_str);
    } else if (s->dest_mode == 0) { /* cluster mode */
        if (x2apic) {
            mask2str(apic_id_str, dest_field & APIC_LOGDEST_X2APIC_ID, 16);
            cpu_fprintf(f, " cluster %u mask %s (X2APIC ID)\n",
                        dest_field >> APIC_LOGDEST_X2APIC_SHIFT, apic_id_str);
        } else {
            mask2str(apic_id_str, dest_field & APIC_LOGDEST_XAPIC_ID, 4);
            cpu_fprintf(f, " cluster %u mask %s (APIC ID)\n",
                        dest_field >> APIC_LOGDEST_XAPIC_SHIFT, apic_id_str);
        }
    }
}

static void dump_apic_interrupt(FILE *f, fprintf_function cpu_fprintf,
                                const char *name, uint32_t *ireg_tab,
                                uint32_t *tmr_tab)
{
    int i, empty = true;

    cpu_fprintf(f, "%s\t ", name);
    for (i = 0; i < 256; i++) {
        if (apic_get_bit(ireg_tab, i)) {
            cpu_fprintf(f, "%u%s ", i,
                        apic_get_bit(tmr_tab, i) ? "(level)" : "");
            empty = false;
        }
    }
    cpu_fprintf(f, "%s\n", empty ? "(none)" : "");
}

void x86_cpu_dump_local_apic_state(CPUState *cs, FILE *f,
                                   fprintf_function cpu_fprintf, int flags)
{
    X86CPU *cpu = X86_CPU(cs);
    APICCommonState *s = APIC_COMMON(cpu->apic_state);
    uint32_t *lvt = s->lvt;

    cpu_fprintf(f, "dumping local APIC state for CPU %-2u\n\n",
                CPU(cpu)->cpu_index);
    dump_apic_lvt(f, cpu_fprintf, "LVT0", lvt[APIC_LVT_LINT0], false);
    dump_apic_lvt(f, cpu_fprintf, "LVT1", lvt[APIC_LVT_LINT1], false);
    dump_apic_lvt(f, cpu_fprintf, "LVTPC", lvt[APIC_LVT_PERFORM], false);
    dump_apic_lvt(f, cpu_fprintf, "LVTERR", lvt[APIC_LVT_ERROR], false);
    dump_apic_lvt(f, cpu_fprintf, "LVTTHMR", lvt[APIC_LVT_THERMAL], false);
    dump_apic_lvt(f, cpu_fprintf, "LVTT", lvt[APIC_LVT_TIMER], true);

    cpu_fprintf(f, "Timer\t DCR=0x%x (divide by %u) initial_count = %u\n",
                s->divide_conf & APIC_DCR_MASK,
                divider_conf(s->divide_conf),
                s->initial_count);

    cpu_fprintf(f, "SPIV\t 0x%08x APIC %s, focus=%s, spurious vec %u\n",
                s->spurious_vec,
                s->spurious_vec & APIC_SPURIO_ENABLED ? "enabled" : "disabled",
                s->spurious_vec & APIC_SPURIO_FOCUS ? "on" : "off",
                s->spurious_vec & APIC_VECTOR_MASK);

    dump_apic_icr(f, cpu_fprintf, s, &cpu->env);

    cpu_fprintf(f, "ESR\t 0x%08x\n", s->esr);

    dump_apic_interrupt(f, cpu_fprintf, "ISR", s->isr, s->tmr);
    dump_apic_interrupt(f, cpu_fprintf, "IRR", s->irr, s->tmr);

    cpu_fprintf(f, "\nAPR 0x%02x TPR 0x%02x DFR 0x%02x LDR 0x%02x",
                s->arb_id, s->tpr, s->dest_mode, s->log_dest);
    if (s->dest_mode == 0) {
        cpu_fprintf(f, "(cluster %u: id %u)",
                    s->log_dest >> APIC_LOGDEST_XAPIC_SHIFT,
                    s->log_dest & APIC_LOGDEST_XAPIC_ID);
    }
    cpu_fprintf(f, " PPR 0x%02x\n", apic_get_ppr(s));
}
#else
void x86_cpu_dump_local_apic_state(CPUState *cs, FILE *f,
                                   fprintf_function cpu_fprintf, int flags)
{
}
#endif /* !CONFIG_USER_ONLY */

#define DUMP_CODE_BYTES_TOTAL    50
#define DUMP_CODE_BYTES_BACKWARD 20

void x86_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                        int flags)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int eflags, i, nb;
    char cc_op_name[32];
    static const char *seg_name[6] = { "ES", "CS", "SS", "DS", "FS", "GS" };

    eflags = cpu_compute_eflags(env);
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        cpu_fprintf(f,
                    "RAX=%016" PRIx64 " RBX=%016" PRIx64 " RCX=%016" PRIx64 " RDX=%016" PRIx64 "\n"
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
        cpu_fprintf(f, "EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
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
        cpu_x86_dump_seg_cache(env, f, cpu_fprintf, seg_name[i],
                               &env->segs[i]);
    }
    cpu_x86_dump_seg_cache(env, f, cpu_fprintf, "LDT", &env->ldt);
    cpu_x86_dump_seg_cache(env, f, cpu_fprintf, "TR", &env->tr);

#ifdef TARGET_X86_64
    if (env->hflags & HF_LMA_MASK) {
        cpu_fprintf(f, "GDT=     %016" PRIx64 " %08x\n",
                    env->gdt.base, env->gdt.limit);
        cpu_fprintf(f, "IDT=     %016" PRIx64 " %08x\n",
                    env->idt.base, env->idt.limit);
        cpu_fprintf(f, "CR0=%08x CR2=%016" PRIx64 " CR3=%016" PRIx64 " CR4=%08x\n",
                    (uint32_t)env->cr[0],
                    env->cr[2],
                    env->cr[3],
                    (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++)
            cpu_fprintf(f, "DR%d=%016" PRIx64 " ", i, env->dr[i]);
        cpu_fprintf(f, "\nDR6=%016" PRIx64 " DR7=%016" PRIx64 "\n",
                    env->dr[6], env->dr[7]);
    } else
#endif
    {
        cpu_fprintf(f, "GDT=     %08x %08x\n",
                    (uint32_t)env->gdt.base, env->gdt.limit);
        cpu_fprintf(f, "IDT=     %08x %08x\n",
                    (uint32_t)env->idt.base, env->idt.limit);
        cpu_fprintf(f, "CR0=%08x CR2=%08x CR3=%08x CR4=%08x\n",
                    (uint32_t)env->cr[0],
                    (uint32_t)env->cr[2],
                    (uint32_t)env->cr[3],
                    (uint32_t)env->cr[4]);
        for(i = 0; i < 4; i++) {
            cpu_fprintf(f, "DR%d=" TARGET_FMT_lx " ", i, env->dr[i]);
        }
        cpu_fprintf(f, "\nDR6=" TARGET_FMT_lx " DR7=" TARGET_FMT_lx "\n",
                    env->dr[6], env->dr[7]);
    }
    if (flags & CPU_DUMP_CCOP) {
        if ((unsigned)env->cc_op < CC_OP_NB)
            snprintf(cc_op_name, sizeof(cc_op_name), "%s", cc_op_str[env->cc_op]);
        else
            snprintf(cc_op_name, sizeof(cc_op_name), "[%d]", env->cc_op);
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            cpu_fprintf(f, "CCS=%016" PRIx64 " CCD=%016" PRIx64 " CCO=%-8s\n",
                        env->cc_src, env->cc_dst,
                        cc_op_name);
        } else
#endif
        {
            cpu_fprintf(f, "CCS=%08x CCD=%08x CCO=%-8s\n",
                        (uint32_t)env->cc_src, (uint32_t)env->cc_dst,
                        cc_op_name);
        }
    }
    cpu_fprintf(f, "EFER=%016" PRIx64 "\n", env->efer);
    if (flags & CPU_DUMP_FPU) {
        int fptag;
        fptag = 0;
        for(i = 0; i < 8; i++) {
            fptag |= ((!env->fptags[i]) << i);
        }
        cpu_fprintf(f, "FCW=%04x FSW=%04x [ST=%d] FTW=%02x MXCSR=%08x\n",
                    env->fpuc,
                    (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11,
                    env->fpstt,
                    fptag,
                    env->mxcsr);
        for(i=0;i<8;i++) {
            CPU_LDoubleU u;
            u.d = env->fpregs[i].d;
            cpu_fprintf(f, "FPR%d=%016" PRIx64 " %04x",
                        i, u.l.lower, u.l.upper);
            if ((i & 1) == 1)
                cpu_fprintf(f, "\n");
            else
                cpu_fprintf(f, " ");
        }
        if (env->hflags & HF_CS64_MASK)
            nb = 16;
        else
            nb = 8;
        for(i=0;i<nb;i++) {
            cpu_fprintf(f, "XMM%02d=%08x%08x%08x%08x",
                        i,
                        env->xmm_regs[i].ZMM_L(3),
                        env->xmm_regs[i].ZMM_L(2),
                        env->xmm_regs[i].ZMM_L(1),
                        env->xmm_regs[i].ZMM_L(0));
            if ((i & 1) == 1)
                cpu_fprintf(f, "\n");
            else
                cpu_fprintf(f, " ");
        }
    }
    if (flags & CPU_DUMP_CODE) {
        target_ulong base = env->segs[R_CS].base + env->eip;
        target_ulong offs = MIN(env->eip, DUMP_CODE_BYTES_BACKWARD);
        uint8_t code;
        char codestr[3];

        cpu_fprintf(f, "Code=");
        for (i = 0; i < DUMP_CODE_BYTES_TOTAL; i++) {
            if (cpu_memory_rw_debug(cs, base - offs + i, &code, 1, 0) == 0) {
                snprintf(codestr, sizeof(codestr), "%02x", code);
            } else {
                snprintf(codestr, sizeof(codestr), "??");
            }
            cpu_fprintf(f, "%s%s%s%s", i > 0 ? " " : "",
                        i == offs ? "<" : "", codestr, i == offs ? ">" : "");
        }
        cpu_fprintf(f, "\n");
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
        tlb_flush(cs, 1);
        env->a20_mask = ~(1 << 20) | (a20_state << 20);
    }
}

void cpu_x86_update_cr0(CPUX86State *env, uint32_t new_cr0)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    int pe_state;

    qemu_log_mask(CPU_LOG_MMU, "CR0 update: CR0=0x%08x\n", new_cr0);
    if ((new_cr0 & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK)) !=
        (env->cr[0] & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK))) {
        tlb_flush(CPU(cpu), 1);
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
    X86CPU *cpu = x86_env_get_cpu(env);

    env->cr[3] = new_cr3;
    if (env->cr[0] & CR0_PG_MASK) {
        qemu_log_mask(CPU_LOG_MMU,
                        "CR3 update: CR3=" TARGET_FMT_lx "\n", new_cr3);
        tlb_flush(CPU(cpu), 0);
    }
}

void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    uint32_t hflags;

#if defined(DEBUG_MMU)
    printf("CR4 update: CR4=%08x\n", (uint32_t)env->cr[4]);
#endif
    if ((new_cr4 ^ env->cr[4]) &
        (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK |
         CR4_SMEP_MASK | CR4_SMAP_MASK)) {
        tlb_flush(CPU(cpu), 1);
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

#if defined(CONFIG_USER_ONLY)

int x86_cpu_handle_mmu_fault(CPUState *cs, vaddr addr,
                             int is_write, int mmu_idx)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    /* user mode only emulation */
    is_write &= 1;
    env->cr[2] = addr;
    env->error_code = (is_write << PG_ERROR_W_BIT);
    env->error_code |= PG_ERROR_U_MASK;
    cs->exception_index = EXCP0E_PAGE;
    return 1;
}

#else

/* return value:
 * -1 = cannot handle fault
 * 0  = nothing more to do
 * 1  = generate PF fault
 */
int x86_cpu_handle_mmu_fault(CPUState *cs, vaddr addr,
                             int is_write1, int mmu_idx)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    uint64_t ptep, pte;
    target_ulong pde_addr, pte_addr;
    int error_code = 0;
    int is_dirty, prot, page_size, is_write, is_user;
    hwaddr paddr;
    uint64_t rsvd_mask = PG_HI_RSVD_MASK;
    uint32_t page_offset;
    target_ulong vaddr;

    is_user = mmu_idx == MMU_USER_IDX;
#if defined(DEBUG_MMU)
    printf("MMU fault: addr=%" VADDR_PRIx " w=%d u=%d eip=" TARGET_FMT_lx "\n",
           addr, is_write1, is_user, env->eip);
#endif
    is_write = is_write1 & 1;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
#ifdef TARGET_X86_64
        if (!(env->hflags & HF_LMA_MASK)) {
            /* Without long mode we can only address 32bits in real mode */
            pte = (uint32_t)pte;
        }
#endif
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        page_size = 4096;
        goto do_mapping;
    }

    if (!(env->efer & MSR_EFER_NXE)) {
        rsvd_mask |= PG_NX_MASK;
    }

    if (env->cr[4] & CR4_PAE_MASK) {
        uint64_t pde, pdpe;
        target_ulong pdpe_addr;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                env->error_code = 0;
                cs->exception_index = EXCP0D_GPF;
                return 1;
            }

            pml4e_addr = ((env->cr[3] & ~0xfff) + (((addr >> 39) & 0x1ff) << 3)) &
                env->a20_mask;
            pml4e = x86_ldq_phys(cs, pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pml4e & (rsvd_mask | PG_PSE_MASK)) {
                goto do_fault_rsvd;
            }
            if (!(pml4e & PG_ACCESSED_MASK)) {
                pml4e |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pml4e_addr, pml4e);
            }
            ptep = pml4e ^ PG_NX_MASK;
            pdpe_addr = ((pml4e & PG_ADDRESS_MASK) + (((addr >> 30) & 0x1ff) << 3)) &
                env->a20_mask;
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            if (pdpe & rsvd_mask) {
                goto do_fault_rsvd;
            }
            ptep &= pdpe ^ PG_NX_MASK;
            if (!(pdpe & PG_ACCESSED_MASK)) {
                pdpe |= PG_ACCESSED_MASK;
                x86_stl_phys_notdirty(cs, pdpe_addr, pdpe);
            }
            if (pdpe & PG_PSE_MASK) {
                /* 1 GB page */
                page_size = 1024 * 1024 * 1024;
                pte_addr = pdpe_addr;
                pte = pdpe;
                goto do_check_protect;
            }
        } else
#endif
        {
            /* XXX: load them when cr3 is loaded ? */
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                env->a20_mask;
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                goto do_fault;
            }
            rsvd_mask |= PG_HI_USER_MASK;
            if (pdpe & (rsvd_mask | PG_NX_MASK)) {
                goto do_fault_rsvd;
            }
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
        }

        pde_addr = ((pdpe & PG_ADDRESS_MASK) + (((addr >> 21) & 0x1ff) << 3)) &
            env->a20_mask;
        pde = x86_ldq_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pde & rsvd_mask) {
            goto do_fault_rsvd;
        }
        ptep &= pde ^ PG_NX_MASK;
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte_addr = pde_addr;
            pte = pde;
            goto do_check_protect;
        }
        /* 4 KB page */
        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }
        pte_addr = ((pde & PG_ADDRESS_MASK) + (((addr >> 12) & 0x1ff) << 3)) &
            env->a20_mask;
        pte = x86_ldq_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        if (pte & rsvd_mask) {
            goto do_fault_rsvd;
        }
        /* combine pde and pte nx, user and rw protections */
        ptep &= pte ^ PG_NX_MASK;
        page_size = 4096;
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) &
            env->a20_mask;
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        ptep = pde | PG_NX_MASK;

        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            page_size = 4096 * 1024;
            pte_addr = pde_addr;

            /* Bits 20-13 provide bits 39-32 of the address, bit 21 is reserved.
             * Leave bits 20-13 in place for setting accessed/dirty bits below.
             */
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            rsvd_mask = 0x200000;
            goto do_check_protect_pse36;
        }

        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            x86_stl_phys_notdirty(cs, pde_addr, pde);
        }

        /* page directory entry */
        pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) &
            env->a20_mask;
        pte = x86_ldl_phys(cs, pte_addr);
        if (!(pte & PG_PRESENT_MASK)) {
            goto do_fault;
        }
        /* combine pde and pte user and rw protections */
        ptep &= pte | PG_NX_MASK;
        page_size = 4096;
        rsvd_mask = 0;
    }

do_check_protect:
    rsvd_mask |= (page_size - 1) & PG_ADDRESS_MASK & ~PG_PSE_PAT_MASK;
do_check_protect_pse36:
    if (pte & rsvd_mask) {
        goto do_fault_rsvd;
    }
    ptep ^= PG_NX_MASK;

    /* can the page can be put in the TLB?  prot will tell us */
    if (is_user && !(ptep & PG_USER_MASK)) {
        goto do_fault_protect;
    }

    prot = 0;
    if (mmu_idx != MMU_KSMAP_IDX || !(ptep & PG_USER_MASK)) {
        prot |= PAGE_READ;
        if ((ptep & PG_RW_MASK) || (!is_user && !(env->cr[0] & CR0_WP_MASK))) {
            prot |= PAGE_WRITE;
        }
    }
    if (!(ptep & PG_NX_MASK) &&
        (mmu_idx == MMU_USER_IDX ||
         !((env->cr[4] & CR4_SMEP_MASK) && (ptep & PG_USER_MASK)))) {
        prot |= PAGE_EXEC;
    }
    if ((env->cr[4] & CR4_PKE_MASK) && (env->hflags & HF_LMA_MASK) &&
        (ptep & PG_USER_MASK) && env->pkru) {
        uint32_t pk = (pte & PG_PKRU_MASK) >> PG_PKRU_BIT;
        uint32_t pkru_ad = (env->pkru >> pk * 2) & 1;
        uint32_t pkru_wd = (env->pkru >> pk * 2) & 2;
        uint32_t pkru_prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;

        if (pkru_ad) {
            pkru_prot &= ~(PAGE_READ | PAGE_WRITE);
        } else if (pkru_wd && (is_user || env->cr[0] & CR0_WP_MASK)) {
            pkru_prot &= ~PAGE_WRITE;
        }

        prot &= pkru_prot;
        if ((pkru_prot & (1 << is_write1)) == 0) {
            assert(is_write1 != 2);
            error_code |= PG_ERROR_PK_MASK;
            goto do_fault_protect;
        }
    }

    if ((prot & (1 << is_write1)) == 0) {
        goto do_fault_protect;
    }

    /* yes, it can! */
    is_dirty = is_write && !(pte & PG_DIRTY_MASK);
    if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
        pte |= PG_ACCESSED_MASK;
        if (is_dirty) {
            pte |= PG_DIRTY_MASK;
        }
        x86_stl_phys_notdirty(cs, pte_addr, pte);
    }

    if (!(pte & PG_DIRTY_MASK)) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        assert(!is_write);
        prot &= ~PAGE_WRITE;
    }

 do_mapping:
    pte = pte & env->a20_mask;

    /* align to page_size */
    pte &= PG_ADDRESS_MASK & ~(page_size - 1);

    /* Even if 4MB pages, we map only one 4KB page in the cache to
       avoid filling it too fast */
    vaddr = addr & TARGET_PAGE_MASK;
    page_offset = vaddr & (page_size - 1);
    paddr = pte + page_offset;

    assert(prot & (1 << is_write1));
    tlb_set_page_with_attrs(cs, vaddr, paddr, cpu_get_mem_attrs(env),
                            prot, mmu_idx, page_size);
    return 0;
 do_fault_rsvd:
    error_code |= PG_ERROR_RSVD_MASK;
 do_fault_protect:
    error_code |= PG_ERROR_P_MASK;
 do_fault:
    error_code |= (is_write << PG_ERROR_W_BIT);
    if (is_user)
        error_code |= PG_ERROR_U_MASK;
    if (is_write1 == 2 &&
        (((env->efer & MSR_EFER_NXE) &&
          (env->cr[4] & CR4_PAE_MASK)) ||
         (env->cr[4] & CR4_SMEP_MASK)))
        error_code |= PG_ERROR_I_D_MASK;
    if (env->intercept_exceptions & (1 << EXCP0E_PAGE)) {
        /* cr2 is not modified in case of exceptions */
        x86_stq_phys(cs,
                 env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2),
                 addr);
    } else {
        env->cr[2] = addr;
    }
    env->error_code = error_code;
    cs->exception_index = EXCP0E_PAGE;
    return 1;
}

hwaddr x86_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    target_ulong pde_addr, pte_addr;
    uint64_t pte;
    uint32_t page_offset;
    int page_size;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr & env->a20_mask;
        page_size = 4096;
    } else if (env->cr[4] & CR4_PAE_MASK) {
        target_ulong pdpe_addr;
        uint64_t pde, pdpe;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = (int64_t)addr >> 47;
            if (sext != 0 && sext != -1) {
                return -1;
            }
            pml4e_addr = ((env->cr[3] & ~0xfff) + (((addr >> 39) & 0x1ff) << 3)) &
                env->a20_mask;
            pml4e = x86_ldq_phys(cs, pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                return -1;
            }
            pdpe_addr = ((pml4e & PG_ADDRESS_MASK) +
                         (((addr >> 30) & 0x1ff) << 3)) & env->a20_mask;
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
                env->a20_mask;
            pdpe = x86_ldq_phys(cs, pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK))
                return -1;
        }

        pde_addr = ((pdpe & PG_ADDRESS_MASK) +
                    (((addr >> 21) & 0x1ff) << 3)) & env->a20_mask;
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
                        (((addr >> 12) & 0x1ff) << 3)) & env->a20_mask;
            page_size = 4096;
            pte = x86_ldq_phys(cs, pte_addr);
        }
        if (!(pte & PG_PRESENT_MASK)) {
            return -1;
        }
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) & env->a20_mask;
        pde = x86_ldl_phys(cs, pde_addr);
        if (!(pde & PG_PRESENT_MASK))
            return -1;
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            pte = pde | ((pde & 0x1fe000LL) << (32 - 13));
            page_size = 4096 * 1024;
        } else {
            /* page directory entry */
            pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & env->a20_mask;
            pte = x86_ldl_phys(cs, pte_addr);
            if (!(pte & PG_PRESENT_MASK)) {
                return -1;
            }
            page_size = 4096;
        }
        pte = pte & env->a20_mask;
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
    X86CPU *cpu;
    int bank;
    uint64_t status;
    uint64_t mcg_status;
    uint64_t addr;
    uint64_t misc;
    int flags;
} MCEInjectionParams;

static void do_inject_x86_mce(void *data)
{
    MCEInjectionParams *params = data;
    CPUX86State *cenv = &params->cpu->env;
    CPUState *cpu = CPU(params->cpu);
    uint64_t *banks = cenv->mce_banks + 4 * params->bank;

    cpu_synchronize_state(cpu);

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
                           cpu->cpu_index);
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
                           cpu->cpu_index, params->bank);
            return;
        }

        if ((cenv->mcg_status & MCG_STATUS_MCIP) ||
            !(cenv->cr[4] & CR4_MCE_MASK)) {
            monitor_printf(params->mon,
                           "CPU %d: Previous MCE still in progress, raising"
                           " triple fault\n",
                           cpu->cpu_index);
            qemu_log_mask(CPU_LOG_RESET, "Triple fault\n");
            qemu_system_reset_request();
            return;
        }
        if (banks[1] & MCI_STATUS_VAL) {
            params->status |= MCI_STATUS_OVER;
        }
        banks[2] = params->addr;
        banks[3] = params->misc;
        cenv->mcg_status = params->mcg_status;
        banks[1] = params->status;
        cpu_interrupt(cpu, CPU_INTERRUPT_MCE);
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
        .cpu = cpu,
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

    run_on_cpu(cs, do_inject_x86_mce, &params);
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
            params.cpu = X86_CPU(other_cs);
            run_on_cpu(other_cs, do_inject_x86_mce, &params);
        }
    }
}

void cpu_report_tpr_access(CPUX86State *env, TPRAccess access)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    if (kvm_enabled()) {
        env->tpr_access_type = access;

        cpu_interrupt(cs, CPU_INTERRUPT_TPR);
    } else {
        cpu_restore_state(cs, cs->mem_io_pc);

        apic_handle_tpr_access_report(cpu->apic_state, env->eip, access);
    }
}
#endif /* !CONFIG_USER_ONLY */

int cpu_x86_get_descr_debug(CPUX86State *env, unsigned int selector,
                            target_ulong *base, unsigned int *limit,
                            unsigned int *flags)
{
    X86CPU *cpu = x86_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
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

    return address_space_ldub(cs->as, addr,
                              cpu_get_mem_attrs(env),
                              NULL);
}

uint32_t x86_lduw_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    return address_space_lduw(cs->as, addr,
                              cpu_get_mem_attrs(env),
                              NULL);
}

uint32_t x86_ldl_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    return address_space_ldl(cs->as, addr,
                             cpu_get_mem_attrs(env),
                             NULL);
}

uint64_t x86_ldq_phys(CPUState *cs, hwaddr addr)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    return address_space_ldq(cs->as, addr,
                             cpu_get_mem_attrs(env),
                             NULL);
}

void x86_stb_phys(CPUState *cs, hwaddr addr, uint8_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    address_space_stb(cs->as, addr, val,
                      cpu_get_mem_attrs(env),
                      NULL);
}

void x86_stl_phys_notdirty(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    address_space_stl_notdirty(cs->as, addr, val,
                               cpu_get_mem_attrs(env),
                               NULL);
}

void x86_stw_phys(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    address_space_stw(cs->as, addr, val,
                      cpu_get_mem_attrs(env),
                      NULL);
}

void x86_stl_phys(CPUState *cs, hwaddr addr, uint32_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    address_space_stl(cs->as, addr, val,
                      cpu_get_mem_attrs(env),
                      NULL);
}

void x86_stq_phys(CPUState *cs, hwaddr addr, uint64_t val)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;

    address_space_stq(cs->as, addr, val,
                      cpu_get_mem_attrs(env),
                      NULL);
}
#endif
