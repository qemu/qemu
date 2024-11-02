/*
 *  i386 CPU dump to FILE
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
#include "qemu/qemu-print.h"
#ifndef CONFIG_USER_ONLY
#include "hw/i386/apic_internal.h"
#endif

/***********************************************************/
/* x86 debug */

static const char * const cc_op_str[] = {
    [CC_OP_DYNAMIC] = "DYNAMIC",

    [CC_OP_EFLAGS] = "EFLAGS",
    [CC_OP_ADCX] = "ADCX",
    [CC_OP_ADOX] = "ADOX",
    [CC_OP_ADCOX] = "ADCOX",

    [CC_OP_MULB] = "MULB",
    [CC_OP_MULW] = "MULW",
    [CC_OP_MULL] = "MULL",
    [CC_OP_MULQ] = "MULQ",

    [CC_OP_ADDB] = "ADDB",
    [CC_OP_ADDW] = "ADDW",
    [CC_OP_ADDL] = "ADDL",
    [CC_OP_ADDQ] = "ADDQ",

    [CC_OP_ADCB] = "ADCB",
    [CC_OP_ADCW] = "ADCW",
    [CC_OP_ADCL] = "ADCL",
    [CC_OP_ADCQ] = "ADCQ",

    [CC_OP_SUBB] = "SUBB",
    [CC_OP_SUBW] = "SUBW",
    [CC_OP_SUBL] = "SUBL",
    [CC_OP_SUBQ] = "SUBQ",

    [CC_OP_SBBB] = "SBBB",
    [CC_OP_SBBW] = "SBBW",
    [CC_OP_SBBL] = "SBBL",
    [CC_OP_SBBQ] = "SBBQ",

    [CC_OP_LOGICB] = "LOGICB",
    [CC_OP_LOGICW] = "LOGICW",
    [CC_OP_LOGICL] = "LOGICL",
    [CC_OP_LOGICQ] = "LOGICQ",

    [CC_OP_INCB] = "INCB",
    [CC_OP_INCW] = "INCW",
    [CC_OP_INCL] = "INCL",
    [CC_OP_INCQ] = "INCQ",

    [CC_OP_DECB] = "DECB",
    [CC_OP_DECW] = "DECW",
    [CC_OP_DECL] = "DECL",
    [CC_OP_DECQ] = "DECQ",

    [CC_OP_SHLB] = "SHLB",
    [CC_OP_SHLW] = "SHLW",
    [CC_OP_SHLL] = "SHLL",
    [CC_OP_SHLQ] = "SHLQ",

    [CC_OP_SARB] = "SARB",
    [CC_OP_SARW] = "SARW",
    [CC_OP_SARL] = "SARL",
    [CC_OP_SARQ] = "SARQ",

    [CC_OP_BMILGB] = "BMILGB",
    [CC_OP_BMILGW] = "BMILGW",
    [CC_OP_BMILGL] = "BMILGL",
    [CC_OP_BMILGQ] = "BMILGQ",

    [CC_OP_POPCNT] = "POPCNT",
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

#endif /* !CONFIG_USER_ONLY */

#define DUMP_CODE_BYTES_TOTAL    50
#define DUMP_CODE_BYTES_BACKWARD 20

void x86_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    int eflags, i, nb;
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
        const char *cc_op_name = NULL;
        char cc_op_buf[32];

        if ((unsigned)env->cc_op < ARRAY_SIZE(cc_op_str)) {
            cc_op_name = cc_op_str[env->cc_op];
        }
        if (cc_op_name == NULL) {
            snprintf(cc_op_buf, sizeof(cc_op_buf), "[%d]", env->cc_op);
            cc_op_name = cc_op_buf;
        }
#ifdef TARGET_X86_64
        if (env->hflags & HF_CS64_MASK) {
            qemu_fprintf(f, "CCS=%016" PRIx64 " CCD=%016" PRIx64 " CCO=%s\n",
                         env->cc_src, env->cc_dst,
                         cc_op_name);
        } else
#endif
        {
            qemu_fprintf(f, "CCS=%08x CCD=%08x CCO=%s\n",
                         (uint32_t)env->cc_src, (uint32_t)env->cc_dst,
                         cc_op_name);
        }
    }
    qemu_fprintf(f, "EFER=%016" PRIx64 "\n", env->efer);
    if (flags & CPU_DUMP_FPU) {
        int fptag;
        const uint64_t avx512_mask = XSTATE_OPMASK_MASK | \
                                     XSTATE_ZMM_Hi256_MASK | \
                                     XSTATE_Hi16_ZMM_MASK | \
                                     XSTATE_YMM_MASK | XSTATE_SSE_MASK,
                       avx_mask = XSTATE_YMM_MASK | XSTATE_SSE_MASK;
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

        if ((env->xcr0 & avx512_mask) == avx512_mask) {
            /* XSAVE enabled AVX512 */
            for (i = 0; i < NB_OPMASK_REGS; i++) {
                qemu_fprintf(f, "Opmask%02d=%016"PRIx64"%s", i,
                             env->opmask_regs[i], ((i & 3) == 3) ? "\n" : " ");
            }

            nb = (env->hflags & HF_CS64_MASK) ? 32 : 8;
            for (i = 0; i < nb; i++) {
                qemu_fprintf(f, "ZMM%02d=%016"PRIx64" %016"PRIx64" %016"PRIx64
                             " %016"PRIx64" %016"PRIx64" %016"PRIx64
                             " %016"PRIx64" %016"PRIx64"\n",
                             i,
                             env->xmm_regs[i].ZMM_Q(7),
                             env->xmm_regs[i].ZMM_Q(6),
                             env->xmm_regs[i].ZMM_Q(5),
                             env->xmm_regs[i].ZMM_Q(4),
                             env->xmm_regs[i].ZMM_Q(3),
                             env->xmm_regs[i].ZMM_Q(2),
                             env->xmm_regs[i].ZMM_Q(1),
                             env->xmm_regs[i].ZMM_Q(0));
            }
        } else if ((env->xcr0 & avx_mask)  == avx_mask) {
            /* XSAVE enabled AVX */
            nb = env->hflags & HF_CS64_MASK ? 16 : 8;
            for (i = 0; i < nb; i++) {
                qemu_fprintf(f, "YMM%02d=%016"PRIx64" %016"PRIx64" %016"PRIx64
                             " %016"PRIx64"\n", i,
                             env->xmm_regs[i].ZMM_Q(3),
                             env->xmm_regs[i].ZMM_Q(2),
                             env->xmm_regs[i].ZMM_Q(1),
                             env->xmm_regs[i].ZMM_Q(0));
            }
        } else { /* SSE and below cases */
            nb = env->hflags & HF_CS64_MASK ? 16 : 8;
            for (i = 0; i < nb; i++) {
                qemu_fprintf(f, "XMM%02d=%016"PRIx64" %016"PRIx64"%s",
                             i,
                             env->xmm_regs[i].ZMM_Q(1),
                             env->xmm_regs[i].ZMM_Q(0),
                             (i & 1) ? "\n" : " ");
            }
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
