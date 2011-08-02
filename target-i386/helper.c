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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "qemu-common.h"
#include "kvm.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu.h"
#include "monitor.h"
#endif

//#define DEBUG_MMU

/* NOTE: must be called outside the CPU execute loop */
void cpu_reset(CPUX86State *env)
{
    int i;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", env->cpu_index);
        log_cpu_state(env, X86_DUMP_FPU | X86_DUMP_CCOP);
    }

    memset(env, 0, offsetof(CPUX86State, breakpoints));

    tlb_flush(env, 1);

    env->old_exception = -1;

    /* init to reset state */

#ifdef CONFIG_SOFTMMU
    env->hflags |= HF_SOFTMMU_MASK;
#endif
    env->hflags2 |= HF2_GIF_MASK;

    cpu_x86_update_cr0(env, 0x60000010);
    env->a20_mask = ~0x0;
    env->smbase = 0x30000;

    env->idt.limit = 0xffff;
    env->gdt.limit = 0xffff;
    env->ldt.limit = 0xffff;
    env->ldt.flags = DESC_P_MASK | (2 << DESC_TYPE_SHIFT);
    env->tr.limit = 0xffff;
    env->tr.flags = DESC_P_MASK | (11 << DESC_TYPE_SHIFT);

    cpu_x86_load_seg_cache(env, R_CS, 0xf000, 0xffff0000, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_CS_MASK |
                           DESC_R_MASK | DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_DS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_ES, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_SS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_FS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);
    cpu_x86_load_seg_cache(env, R_GS, 0, 0, 0xffff,
                           DESC_P_MASK | DESC_S_MASK | DESC_W_MASK |
                           DESC_A_MASK);

    env->eip = 0xfff0;
    env->regs[R_EDX] = env->cpuid_version;

    env->eflags = 0x2;

    /* FPU init */
    for(i = 0;i < 8; i++)
        env->fptags[i] = 1;
    env->fpuc = 0x37f;

    env->mxcsr = 0x1f80;

    env->pat = 0x0007040600070406ULL;

    memset(env->dr, 0, sizeof(env->dr));
    env->dr[6] = DR6_FIXED_1;
    env->dr[7] = DR7_FIXED_1;
    cpu_breakpoint_remove_all(env, BP_CPU);
    cpu_watchpoint_remove_all(env, BP_CPU);
}

void cpu_x86_close(CPUX86State *env)
{
    qemu_free(env);
}

static void cpu_x86_version(CPUState *env, int *family, int *model)
{
    int cpuver = env->cpuid_version;

    if (family == NULL || model == NULL) {
        return;
    }

    *family = (cpuver >> 8) & 0x0f;
    *model = ((cpuver >> 12) & 0xf0) + ((cpuver >> 4) & 0x0f);
}

/* Broadcast MCA signal for processor version 06H_EH and above */
int cpu_x86_support_mca_broadcast(CPUState *env)
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

static const char *cc_op_str[] = {
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
};

static void
cpu_x86_dump_seg_cache(CPUState *env, FILE *f, fprintf_function cpu_fprintf,
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
            cpu_fprintf(f, (sc->flags & DESC_B_MASK) ? "DS  " : "DS16");
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

#define DUMP_CODE_BYTES_TOTAL    50
#define DUMP_CODE_BYTES_BACKWARD 20

void cpu_dump_state(CPUState *env, FILE *f, fprintf_function cpu_fprintf,
                    int flags)
{
    int eflags, i, nb;
    char cc_op_name[32];
    static const char *seg_name[6] = { "ES", "CS", "SS", "DS", "FS", "GS" };

    cpu_synchronize_state(env);

    eflags = env->eflags;
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
                    env->halted);
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
                    env->halted);
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
    if (flags & X86_DUMP_CCOP) {
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
    if (flags & X86_DUMP_FPU) {
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
                        env->xmm_regs[i].XMM_L(3),
                        env->xmm_regs[i].XMM_L(2),
                        env->xmm_regs[i].XMM_L(1),
                        env->xmm_regs[i].XMM_L(0));
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
            if (cpu_memory_rw_debug(env, base - offs + i, &code, 1, 0) == 0) {
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

void cpu_x86_set_a20(CPUX86State *env, int a20_state)
{
    a20_state = (a20_state != 0);
    if (a20_state != ((env->a20_mask >> 20) & 1)) {
#if defined(DEBUG_MMU)
        printf("A20 update: a20=%d\n", a20_state);
#endif
        /* if the cpu is currently executing code, we must unlink it and
           all the potentially executing TB */
        cpu_interrupt(env, CPU_INTERRUPT_EXITTB);

        /* when a20 is changed, all the MMU mappings are invalid, so
           we must flush everything */
        tlb_flush(env, 1);
        env->a20_mask = ~(1 << 20) | (a20_state << 20);
    }
}

void cpu_x86_update_cr0(CPUX86State *env, uint32_t new_cr0)
{
    int pe_state;

#if defined(DEBUG_MMU)
    printf("CR0 update: CR0=0x%08x\n", new_cr0);
#endif
    if ((new_cr0 & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK)) !=
        (env->cr[0] & (CR0_PG_MASK | CR0_WP_MASK | CR0_PE_MASK))) {
        tlb_flush(env, 1);
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
#if defined(DEBUG_MMU)
        printf("CR3 update: CR3=" TARGET_FMT_lx "\n", new_cr3);
#endif
        tlb_flush(env, 0);
    }
}

void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4)
{
#if defined(DEBUG_MMU)
    printf("CR4 update: CR4=%08x\n", (uint32_t)env->cr[4]);
#endif
    if ((new_cr4 & (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK)) !=
        (env->cr[4] & (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK))) {
        tlb_flush(env, 1);
    }
    /* SSE handling */
    if (!(env->cpuid_features & CPUID_SSE))
        new_cr4 &= ~CR4_OSFXSR_MASK;
    if (new_cr4 & CR4_OSFXSR_MASK)
        env->hflags |= HF_OSFXSR_MASK;
    else
        env->hflags &= ~HF_OSFXSR_MASK;

    env->cr[4] = new_cr4;
}

#if defined(CONFIG_USER_ONLY)

int cpu_x86_handle_mmu_fault(CPUX86State *env, target_ulong addr,
                             int is_write, int mmu_idx, int is_softmmu)
{
    /* user mode only emulation */
    is_write &= 1;
    env->cr[2] = addr;
    env->error_code = (is_write << PG_ERROR_W_BIT);
    env->error_code |= PG_ERROR_U_MASK;
    env->exception_index = EXCP0E_PAGE;
    return 1;
}

#else

/* XXX: This value should match the one returned by CPUID
 * and in exec.c */
# if defined(TARGET_X86_64)
# define PHYS_ADDR_MASK 0xfffffff000LL
# else
# define PHYS_ADDR_MASK 0xffffff000LL
# endif

/* return value:
   -1 = cannot handle fault
   0  = nothing more to do
   1  = generate PF fault
*/
int cpu_x86_handle_mmu_fault(CPUX86State *env, target_ulong addr,
                             int is_write1, int mmu_idx, int is_softmmu)
{
    uint64_t ptep, pte;
    target_ulong pde_addr, pte_addr;
    int error_code, is_dirty, prot, page_size, is_write, is_user;
    target_phys_addr_t paddr;
    uint32_t page_offset;
    target_ulong vaddr, virt_addr;

    is_user = mmu_idx == MMU_USER_IDX;
#if defined(DEBUG_MMU)
    printf("MMU fault: addr=" TARGET_FMT_lx " w=%d u=%d eip=" TARGET_FMT_lx "\n",
           addr, is_write1, is_user, env->eip);
#endif
    is_write = is_write1 & 1;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
        virt_addr = addr & TARGET_PAGE_MASK;
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        page_size = 4096;
        goto do_mapping;
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
                env->exception_index = EXCP0D_GPF;
                return 1;
            }

            pml4e_addr = ((env->cr[3] & ~0xfff) + (((addr >> 39) & 0x1ff) << 3)) &
                env->a20_mask;
            pml4e = ldq_phys(pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            if (!(env->efer & MSR_EFER_NXE) && (pml4e & PG_NX_MASK)) {
                error_code = PG_ERROR_RSVD_MASK;
                goto do_fault;
            }
            if (!(pml4e & PG_ACCESSED_MASK)) {
                pml4e |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pml4e_addr, pml4e);
            }
            ptep = pml4e ^ PG_NX_MASK;
            pdpe_addr = ((pml4e & PHYS_ADDR_MASK) + (((addr >> 30) & 0x1ff) << 3)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            if (!(env->efer & MSR_EFER_NXE) && (pdpe & PG_NX_MASK)) {
                error_code = PG_ERROR_RSVD_MASK;
                goto do_fault;
            }
            ptep &= pdpe ^ PG_NX_MASK;
            if (!(pdpe & PG_ACCESSED_MASK)) {
                pdpe |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pdpe_addr, pdpe);
            }
        } else
#endif
        {
            /* XXX: load them when cr3 is loaded ? */
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            ptep = PG_NX_MASK | PG_USER_MASK | PG_RW_MASK;
        }

        pde_addr = ((pdpe & PHYS_ADDR_MASK) + (((addr >> 21) & 0x1ff) << 3)) &
            env->a20_mask;
        pde = ldq_phys(pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            error_code = 0;
            goto do_fault;
        }
        if (!(env->efer & MSR_EFER_NXE) && (pde & PG_NX_MASK)) {
            error_code = PG_ERROR_RSVD_MASK;
            goto do_fault;
        }
        ptep &= pde ^ PG_NX_MASK;
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            ptep ^= PG_NX_MASK;
            if ((ptep & PG_NX_MASK) && is_write1 == 2)
                goto do_fault_protect;
            if (is_user) {
                if (!(ptep & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pde & PG_DIRTY_MASK);
            if (!(pde & PG_ACCESSED_MASK) || is_dirty) {
                pde |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pde |= PG_DIRTY_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }
            /* align to page_size */
            pte = pde & ((PHYS_ADDR_MASK & ~(page_size - 1)) | 0xfff);
            virt_addr = addr & ~(page_size - 1);
        } else {
            /* 4 KB page */
            if (!(pde & PG_ACCESSED_MASK)) {
                pde |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }
            pte_addr = ((pde & PHYS_ADDR_MASK) + (((addr >> 12) & 0x1ff) << 3)) &
                env->a20_mask;
            pte = ldq_phys(pte_addr);
            if (!(pte & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            if (!(env->efer & MSR_EFER_NXE) && (pte & PG_NX_MASK)) {
                error_code = PG_ERROR_RSVD_MASK;
                goto do_fault;
            }
            /* combine pde and pte nx, user and rw protections */
            ptep &= pte ^ PG_NX_MASK;
            ptep ^= PG_NX_MASK;
            if ((ptep & PG_NX_MASK) && is_write1 == 2)
                goto do_fault_protect;
            if (is_user) {
                if (!(ptep & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pte & PG_DIRTY_MASK);
            if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
                pte |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pte |= PG_DIRTY_MASK;
                stl_phys_notdirty(pte_addr, pte);
            }
            page_size = 4096;
            virt_addr = addr & ~0xfff;
            pte = pte & (PHYS_ADDR_MASK | 0xfff);
        }
    } else {
        uint32_t pde;

        /* page directory entry */
        pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) &
            env->a20_mask;
        pde = ldl_phys(pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            error_code = 0;
            goto do_fault;
        }
        /* if PSE bit is set, then we use a 4MB page */
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            page_size = 4096 * 1024;
            if (is_user) {
                if (!(pde & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(pde & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(pde & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pde & PG_DIRTY_MASK);
            if (!(pde & PG_ACCESSED_MASK) || is_dirty) {
                pde |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pde |= PG_DIRTY_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }

            pte = pde & ~( (page_size - 1) & ~0xfff); /* align to page_size */
            ptep = pte;
            virt_addr = addr & ~(page_size - 1);
        } else {
            if (!(pde & PG_ACCESSED_MASK)) {
                pde |= PG_ACCESSED_MASK;
                stl_phys_notdirty(pde_addr, pde);
            }

            /* page directory entry */
            pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) &
                env->a20_mask;
            pte = ldl_phys(pte_addr);
            if (!(pte & PG_PRESENT_MASK)) {
                error_code = 0;
                goto do_fault;
            }
            /* combine pde and pte user and rw protections */
            ptep = pte & pde;
            if (is_user) {
                if (!(ptep & PG_USER_MASK))
                    goto do_fault_protect;
                if (is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            } else {
                if ((env->cr[0] & CR0_WP_MASK) &&
                    is_write && !(ptep & PG_RW_MASK))
                    goto do_fault_protect;
            }
            is_dirty = is_write && !(pte & PG_DIRTY_MASK);
            if (!(pte & PG_ACCESSED_MASK) || is_dirty) {
                pte |= PG_ACCESSED_MASK;
                if (is_dirty)
                    pte |= PG_DIRTY_MASK;
                stl_phys_notdirty(pte_addr, pte);
            }
            page_size = 4096;
            virt_addr = addr & ~0xfff;
        }
    }
    /* the page can be put in the TLB */
    prot = PAGE_READ;
    if (!(ptep & PG_NX_MASK))
        prot |= PAGE_EXEC;
    if (pte & PG_DIRTY_MASK) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
        if (is_user) {
            if (ptep & PG_RW_MASK)
                prot |= PAGE_WRITE;
        } else {
            if (!(env->cr[0] & CR0_WP_MASK) ||
                (ptep & PG_RW_MASK))
                prot |= PAGE_WRITE;
        }
    }
 do_mapping:
    pte = pte & env->a20_mask;

    /* Even if 4MB pages, we map only one 4KB page in the cache to
       avoid filling it too fast */
    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    paddr = (pte & TARGET_PAGE_MASK) + page_offset;
    vaddr = virt_addr + page_offset;

    tlb_set_page(env, vaddr, paddr, prot, mmu_idx, page_size);
    return 0;
 do_fault_protect:
    error_code = PG_ERROR_P_MASK;
 do_fault:
    error_code |= (is_write << PG_ERROR_W_BIT);
    if (is_user)
        error_code |= PG_ERROR_U_MASK;
    if (is_write1 == 2 &&
        (env->efer & MSR_EFER_NXE) &&
        (env->cr[4] & CR4_PAE_MASK))
        error_code |= PG_ERROR_I_D_MASK;
    if (env->intercept_exceptions & (1 << EXCP0E_PAGE)) {
        /* cr2 is not modified in case of exceptions */
        stq_phys(env->vm_vmcb + offsetof(struct vmcb, control.exit_info_2), 
                 addr);
    } else {
        env->cr[2] = addr;
    }
    env->error_code = error_code;
    env->exception_index = EXCP0E_PAGE;
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    target_ulong pde_addr, pte_addr;
    uint64_t pte;
    target_phys_addr_t paddr;
    uint32_t page_offset;
    int page_size;

    if (env->cr[4] & CR4_PAE_MASK) {
        target_ulong pdpe_addr;
        uint64_t pde, pdpe;

#ifdef TARGET_X86_64
        if (env->hflags & HF_LMA_MASK) {
            uint64_t pml4e_addr, pml4e;
            int32_t sext;

            /* test virtual address sign extension */
            sext = (int64_t)addr >> 47;
            if (sext != 0 && sext != -1)
                return -1;

            pml4e_addr = ((env->cr[3] & ~0xfff) + (((addr >> 39) & 0x1ff) << 3)) &
                env->a20_mask;
            pml4e = ldq_phys(pml4e_addr);
            if (!(pml4e & PG_PRESENT_MASK))
                return -1;

            pdpe_addr = ((pml4e & ~0xfff) + (((addr >> 30) & 0x1ff) << 3)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK))
                return -1;
        } else
#endif
        {
            pdpe_addr = ((env->cr[3] & ~0x1f) + ((addr >> 27) & 0x18)) &
                env->a20_mask;
            pdpe = ldq_phys(pdpe_addr);
            if (!(pdpe & PG_PRESENT_MASK))
                return -1;
        }

        pde_addr = ((pdpe & ~0xfff) + (((addr >> 21) & 0x1ff) << 3)) &
            env->a20_mask;
        pde = ldq_phys(pde_addr);
        if (!(pde & PG_PRESENT_MASK)) {
            return -1;
        }
        if (pde & PG_PSE_MASK) {
            /* 2 MB page */
            page_size = 2048 * 1024;
            pte = pde & ~( (page_size - 1) & ~0xfff); /* align to page_size */
        } else {
            /* 4 KB page */
            pte_addr = ((pde & ~0xfff) + (((addr >> 12) & 0x1ff) << 3)) &
                env->a20_mask;
            page_size = 4096;
            pte = ldq_phys(pte_addr);
        }
        if (!(pte & PG_PRESENT_MASK))
            return -1;
    } else {
        uint32_t pde;

        if (!(env->cr[0] & CR0_PG_MASK)) {
            pte = addr;
            page_size = 4096;
        } else {
            /* page directory entry */
            pde_addr = ((env->cr[3] & ~0xfff) + ((addr >> 20) & 0xffc)) & env->a20_mask;
            pde = ldl_phys(pde_addr);
            if (!(pde & PG_PRESENT_MASK))
                return -1;
            if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
                pte = pde & ~0x003ff000; /* align to 4MB */
                page_size = 4096 * 1024;
            } else {
                /* page directory entry */
                pte_addr = ((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & env->a20_mask;
                pte = ldl_phys(pte_addr);
                if (!(pte & PG_PRESENT_MASK))
                    return -1;
                page_size = 4096;
            }
        }
        pte = pte & env->a20_mask;
    }

    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    paddr = (pte & TARGET_PAGE_MASK) + page_offset;
    return paddr;
}

void hw_breakpoint_insert(CPUState *env, int index)
{
    int type, err = 0;

    switch (hw_breakpoint_type(env->dr[7], index)) {
    case 0:
        if (hw_breakpoint_enabled(env->dr[7], index))
            err = cpu_breakpoint_insert(env, env->dr[index], BP_CPU,
                                        &env->cpu_breakpoint[index]);
        break;
    case 1:
        type = BP_CPU | BP_MEM_WRITE;
        goto insert_wp;
    case 2:
         /* No support for I/O watchpoints yet */
        break;
    case 3:
        type = BP_CPU | BP_MEM_ACCESS;
    insert_wp:
        err = cpu_watchpoint_insert(env, env->dr[index],
                                    hw_breakpoint_len(env->dr[7], index),
                                    type, &env->cpu_watchpoint[index]);
        break;
    }
    if (err)
        env->cpu_breakpoint[index] = NULL;
}

void hw_breakpoint_remove(CPUState *env, int index)
{
    if (!env->cpu_breakpoint[index])
        return;
    switch (hw_breakpoint_type(env->dr[7], index)) {
    case 0:
        if (hw_breakpoint_enabled(env->dr[7], index))
            cpu_breakpoint_remove_by_ref(env, env->cpu_breakpoint[index]);
        break;
    case 1:
    case 3:
        cpu_watchpoint_remove_by_ref(env, env->cpu_watchpoint[index]);
        break;
    case 2:
        /* No support for I/O watchpoints yet */
        break;
    }
}

int check_hw_breakpoints(CPUState *env, int force_dr6_update)
{
    target_ulong dr6;
    int reg, type;
    int hit_enabled = 0;

    dr6 = env->dr[6] & ~0xf;
    for (reg = 0; reg < 4; reg++) {
        type = hw_breakpoint_type(env->dr[7], reg);
        if ((type == 0 && env->dr[reg] == env->eip) ||
            ((type & 1) && env->cpu_watchpoint[reg] &&
             (env->cpu_watchpoint[reg]->flags & BP_WATCHPOINT_HIT))) {
            dr6 |= 1 << reg;
            if (hw_breakpoint_enabled(env->dr[7], reg))
                hit_enabled = 1;
        }
    }
    if (hit_enabled || force_dr6_update)
        env->dr[6] = dr6;
    return hit_enabled;
}

static CPUDebugExcpHandler *prev_debug_excp_handler;

static void breakpoint_handler(CPUState *env)
{
    CPUBreakpoint *bp;

    if (env->watchpoint_hit) {
        if (env->watchpoint_hit->flags & BP_CPU) {
            env->watchpoint_hit = NULL;
            if (check_hw_breakpoints(env, 0))
                raise_exception_env(EXCP01_DB, env);
            else
                cpu_resume_from_signal(env, NULL);
        }
    } else {
        QTAILQ_FOREACH(bp, &env->breakpoints, entry)
            if (bp->pc == env->eip) {
                if (bp->flags & BP_CPU) {
                    check_hw_breakpoints(env, 1);
                    raise_exception_env(EXCP01_DB, env);
                }
                break;
            }
    }
    if (prev_debug_excp_handler)
        prev_debug_excp_handler(env);
}

typedef struct MCEInjectionParams {
    Monitor *mon;
    CPUState *env;
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
    CPUState *cenv = params->env;
    uint64_t *banks = cenv->mce_banks + 4 * params->bank;

    cpu_synchronize_state(cenv);

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
                           cenv->cpu_index);
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
                           cenv->cpu_index, params->bank);
            return;
        }

        if ((cenv->mcg_status & MCG_STATUS_MCIP) ||
            !(cenv->cr[4] & CR4_MCE_MASK)) {
            monitor_printf(params->mon,
                           "CPU %d: Previous MCE still in progress, raising"
                           " triple fault\n",
                           cenv->cpu_index);
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
        cpu_interrupt(cenv, CPU_INTERRUPT_MCE);
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

void cpu_x86_inject_mce(Monitor *mon, CPUState *cenv, int bank,
                        uint64_t status, uint64_t mcg_status, uint64_t addr,
                        uint64_t misc, int flags)
{
    MCEInjectionParams params = {
        .mon = mon,
        .env = cenv,
        .bank = bank,
        .status = status,
        .mcg_status = mcg_status,
        .addr = addr,
        .misc = misc,
        .flags = flags,
    };
    unsigned bank_num = cenv->mcg_cap & 0xff;
    CPUState *env;

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

    run_on_cpu(cenv, do_inject_x86_mce, &params);
    if (flags & MCE_INJECT_BROADCAST) {
        params.bank = 1;
        params.status = MCI_STATUS_VAL | MCI_STATUS_UC;
        params.mcg_status = MCG_STATUS_MCIP | MCG_STATUS_RIPV;
        params.addr = 0;
        params.misc = 0;
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            if (cenv == env) {
                continue;
            }
            params.env = env;
            run_on_cpu(cenv, do_inject_x86_mce, &params);
        }
    }
}
#endif /* !CONFIG_USER_ONLY */

static void mce_init(CPUX86State *cenv)
{
    unsigned int bank;

    if (((cenv->cpuid_version >> 8) & 0xf) >= 6
        && (cenv->cpuid_features & (CPUID_MCE | CPUID_MCA)) ==
            (CPUID_MCE | CPUID_MCA)) {
        cenv->mcg_cap = MCE_CAP_DEF | MCE_BANKS_DEF;
        cenv->mcg_ctl = ~(uint64_t)0;
        for (bank = 0; bank < MCE_BANKS_DEF; bank++) {
            cenv->mce_banks[bank * 4] = ~(uint64_t)0;
        }
    }
}

int cpu_x86_get_descr_debug(CPUX86State *env, unsigned int selector,
                            target_ulong *base, unsigned int *limit,
                            unsigned int *flags)
{
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
        || cpu_memory_rw_debug(env, ptr, (uint8_t *)&e1, sizeof(e1), 0) != 0
        || cpu_memory_rw_debug(env, ptr+4, (uint8_t *)&e2, sizeof(e2), 0) != 0)
        return 0;

    *base = ((e1 >> 16) | ((e2 & 0xff) << 16) | (e2 & 0xff000000));
    *limit = (e1 & 0xffff) | (e2 & 0x000f0000);
    if (e2 & DESC_G_MASK)
        *limit = (*limit << 12) | 0xfff;
    *flags = e2;

    return 1;
}

CPUX86State *cpu_x86_init(const char *cpu_model)
{
    CPUX86State *env;
    static int inited;

    env = qemu_mallocz(sizeof(CPUX86State));
    cpu_exec_init(env);
    env->cpu_model_str = cpu_model;

    /* init various static tables used in TCG mode */
    if (tcg_enabled() && !inited) {
        inited = 1;
        optimize_flags_init();
#ifndef CONFIG_USER_ONLY
        prev_debug_excp_handler =
            cpu_set_debug_excp_handler(breakpoint_handler);
#endif
    }
    if (cpu_x86_register(env, cpu_model) < 0) {
        cpu_x86_close(env);
        return NULL;
    }
    mce_init(env);

    qemu_init_vcpu(env);

    return env;
}

#if !defined(CONFIG_USER_ONLY)
void do_cpu_init(CPUState *env)
{
    int sipi = env->interrupt_request & CPU_INTERRUPT_SIPI;
    uint64_t pat = env->pat;

    cpu_reset(env);
    env->interrupt_request = sipi;
    env->pat = pat;
    apic_init_reset(env->apic_state);
    env->halted = !cpu_is_bsp(env);
}

void do_cpu_sipi(CPUState *env)
{
    apic_sipi(env->apic_state);
}
#else
void do_cpu_init(CPUState *env)
{
}
void do_cpu_sipi(CPUState *env)
{
}
#endif
