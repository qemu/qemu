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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <assert.h>

#include "cpu.h"
#include "exec-all.h"

//#define DEBUG_MMU

#ifdef USE_CODE_COPY
#include <asm/ldt.h>
#include <linux/unistd.h>
#include <linux/version.h>

_syscall3(int, modify_ldt, int, func, void *, ptr, unsigned long, bytecount)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 66)
#define modify_ldt_ldt_s user_desc
#endif
#endif /* USE_CODE_COPY */

CPUX86State *cpu_x86_init(void)
{
    CPUX86State *env;
    static int inited;

    cpu_exec_init();

    env = malloc(sizeof(CPUX86State));
    if (!env)
        return NULL;
    memset(env, 0, sizeof(CPUX86State));
    /* init various static tables */
    if (!inited) {
        inited = 1;
        optimize_flags_init();
    }
#ifdef USE_CODE_COPY
    /* testing code for code copy case */
    {
        struct modify_ldt_ldt_s ldt;

        ldt.entry_number = 1;
        ldt.base_addr = (unsigned long)env;
        ldt.limit = (sizeof(CPUState) + 0xfff) >> 12;
        ldt.seg_32bit = 1;
        ldt.contents = MODIFY_LDT_CONTENTS_DATA;
        ldt.read_exec_only = 0;
        ldt.limit_in_pages = 1;
        ldt.seg_not_present = 0;
        ldt.useable = 1;
        modify_ldt(1, &ldt, sizeof(ldt)); /* write ldt entry */
        
        asm volatile ("movl %0, %%fs" : : "r" ((1 << 3) | 7));
    }
#endif
    cpu_single_env = env;
    cpu_reset(env);
    return env;
}

/* NOTE: must be called outside the CPU execute loop */
void cpu_reset(CPUX86State *env)
{
    int i;

    memset(env, 0, offsetof(CPUX86State, breakpoints));

    tlb_flush(env, 1);

    /* init to reset state */

#ifdef CONFIG_SOFTMMU
    env->hflags |= HF_SOFTMMU_MASK;
#endif

    cpu_x86_update_cr0(env, 0x60000010);
    env->a20_mask = 0xffffffff;
    
    env->idt.limit = 0xffff;
    env->gdt.limit = 0xffff;
    env->ldt.limit = 0xffff;
    env->ldt.flags = DESC_P_MASK;
    env->tr.limit = 0xffff;
    env->tr.flags = DESC_P_MASK;
    
    cpu_x86_load_seg_cache(env, R_CS, 0xf000, (uint8_t *)0xffff0000, 0xffff, 0); 
    cpu_x86_load_seg_cache(env, R_DS, 0, NULL, 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_ES, 0, NULL, 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_SS, 0, NULL, 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_FS, 0, NULL, 0xffff, 0);
    cpu_x86_load_seg_cache(env, R_GS, 0, NULL, 0xffff, 0);
    
    env->eip = 0xfff0;
    env->regs[R_EDX] = 0x600; /* indicate P6 processor */
    
    env->eflags = 0x2;
    
    /* FPU init */
    for(i = 0;i < 8; i++)
        env->fptags[i] = 1;
    env->fpuc = 0x37f;
}

void cpu_x86_close(CPUX86State *env)
{
    free(env);
}

/***********************************************************/
/* x86 debug */

static const char *cc_op_str[] = {
    "DYNAMIC",
    "EFLAGS",
    "MULB",
    "MULW",
    "MULL",
    "ADDB",
    "ADDW",
    "ADDL",
    "ADCB",
    "ADCW",
    "ADCL",
    "SUBB",
    "SUBW",
    "SUBL",
    "SBBB",
    "SBBW",
    "SBBL",
    "LOGICB",
    "LOGICW",
    "LOGICL",
    "INCB",
    "INCW",
    "INCL",
    "DECB",
    "DECW",
    "DECL",
    "SHLB",
    "SHLW",
    "SHLL",
    "SARB",
    "SARW",
    "SARL",
};

void cpu_x86_dump_state(CPUX86State *env, FILE *f, int flags)
{
    int eflags, i;
    char cc_op_name[32];
    static const char *seg_name[6] = { "ES", "CS", "SS", "DS", "FS", "GS" };

    eflags = env->eflags;
    fprintf(f, "EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
            "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
            "EIP=%08x EFL=%08x [%c%c%c%c%c%c%c]    CPL=%d II=%d A20=%d\n",
            env->regs[R_EAX], env->regs[R_EBX], env->regs[R_ECX], env->regs[R_EDX], 
            env->regs[R_ESI], env->regs[R_EDI], env->regs[R_EBP], env->regs[R_ESP], 
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
            (env->a20_mask >> 20) & 1);
    for(i = 0; i < 6; i++) {
        SegmentCache *sc = &env->segs[i];
        fprintf(f, "%s =%04x %08x %08x %08x\n",
                seg_name[i],
                sc->selector,
                (int)sc->base,
                sc->limit,
                sc->flags);
    }
    fprintf(f, "LDT=%04x %08x %08x %08x\n",
            env->ldt.selector,
            (int)env->ldt.base,
            env->ldt.limit,
            env->ldt.flags);
    fprintf(f, "TR =%04x %08x %08x %08x\n",
            env->tr.selector,
            (int)env->tr.base,
            env->tr.limit,
            env->tr.flags);
    fprintf(f, "GDT=     %08x %08x\n",
            (int)env->gdt.base, env->gdt.limit);
    fprintf(f, "IDT=     %08x %08x\n",
            (int)env->idt.base, env->idt.limit);
    fprintf(f, "CR0=%08x CR2=%08x CR3=%08x CR4=%08x\n",
            env->cr[0], env->cr[2], env->cr[3], env->cr[4]);
    
    if (flags & X86_DUMP_CCOP) {
        if ((unsigned)env->cc_op < CC_OP_NB)
            snprintf(cc_op_name, sizeof(cc_op_name), "%s", cc_op_str[env->cc_op]);
        else
            snprintf(cc_op_name, sizeof(cc_op_name), "[%d]", env->cc_op);
        fprintf(f, "CCS=%08x CCD=%08x CCO=%-8s\n",
                env->cc_src, env->cc_dst, cc_op_name);
    }
    if (flags & X86_DUMP_FPU) {
        fprintf(f, "ST0=%f ST1=%f ST2=%f ST3=%f\n", 
                (double)env->fpregs[0], 
                (double)env->fpregs[1], 
                (double)env->fpregs[2], 
                (double)env->fpregs[3]);
        fprintf(f, "ST4=%f ST5=%f ST6=%f ST7=%f\n", 
                (double)env->fpregs[4], 
                (double)env->fpregs[5], 
                (double)env->fpregs[7], 
                (double)env->fpregs[8]);
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
        env->a20_mask = 0xffefffff | (a20_state << 20);
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

void cpu_x86_update_cr3(CPUX86State *env, uint32_t new_cr3)
{
    env->cr[3] = new_cr3;
    if (env->cr[0] & CR0_PG_MASK) {
#if defined(DEBUG_MMU)
        printf("CR3 update: CR3=%08x\n", new_cr3);
#endif
        tlb_flush(env, 0);
    }
}

void cpu_x86_update_cr4(CPUX86State *env, uint32_t new_cr4)
{
#if defined(DEBUG_MMU)
    printf("CR4 update: CR4=%08x\n", env->cr[4]);
#endif
    if ((new_cr4 & (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK)) !=
        (env->cr[4] & (CR4_PGE_MASK | CR4_PAE_MASK | CR4_PSE_MASK))) {
        tlb_flush(env, 1);
    }
    env->cr[4] = new_cr4;
}

/* XXX: also flush 4MB pages */
void cpu_x86_flush_tlb(CPUX86State *env, uint32_t addr)
{
    tlb_flush_page(env, addr);
}

/* return value:
   -1 = cannot handle fault 
   0  = nothing more to do 
   1  = generate PF fault
   2  = soft MMU activation required for this block
*/
int cpu_x86_handle_mmu_fault(CPUX86State *env, uint32_t addr, 
                             int is_write, int is_user, int is_softmmu)
{
    uint8_t *pde_ptr, *pte_ptr;
    uint32_t pde, pte, virt_addr, ptep;
    int error_code, is_dirty, prot, page_size, ret;
    unsigned long paddr, vaddr, page_offset;
    
#if defined(DEBUG_MMU)
    printf("MMU fault: addr=0x%08x w=%d u=%d eip=%08x\n", 
           addr, is_write, is_user, env->eip);
#endif
    is_write &= 1;
    
    if (env->user_mode_only) {
        /* user mode only emulation */
        error_code = 0;
        goto do_fault;
    }

    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
        virt_addr = addr & TARGET_PAGE_MASK;
        prot = PAGE_READ | PAGE_WRITE;
        page_size = 4096;
        goto do_mapping;
    }

    /* page directory entry */
    pde_ptr = phys_ram_base + 
        (((env->cr[3] & ~0xfff) + ((addr >> 20) & ~3)) & env->a20_mask);
    pde = ldl_raw(pde_ptr);
    if (!(pde & PG_PRESENT_MASK)) {
        error_code = 0;
        goto do_fault;
    }
    /* if PSE bit is set, then we use a 4MB page */
    if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
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
            stl_raw(pde_ptr, pde);
        }
        
        pte = pde & ~0x003ff000; /* align to 4MB */
        ptep = pte;
        page_size = 4096 * 1024;
        virt_addr = addr & ~0x003fffff;
    } else {
        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            stl_raw(pde_ptr, pde);
        }

        /* page directory entry */
        pte_ptr = phys_ram_base + 
            (((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & env->a20_mask);
        pte = ldl_raw(pte_ptr);
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
            stl_raw(pte_ptr, pte);
        }
        page_size = 4096;
        virt_addr = addr & ~0xfff;
    }

    /* the page can be put in the TLB */
    prot = PAGE_READ;
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
    
    ret = tlb_set_page(env, vaddr, paddr, prot, is_user, is_softmmu);
    return ret;
 do_fault_protect:
    error_code = PG_ERROR_P_MASK;
 do_fault:
    env->cr[2] = addr;
    env->error_code = (is_write << PG_ERROR_W_BIT) | error_code;
    if (is_user)
        env->error_code |= PG_ERROR_U_MASK;
    return 1;
}

#if defined(CONFIG_USER_ONLY) 
target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}
#else
target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    uint8_t *pde_ptr, *pte_ptr;
    uint32_t pde, pte, paddr, page_offset, page_size;

    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
        page_size = 4096;
    } else {
        /* page directory entry */
        pde_ptr = phys_ram_base + 
            (((env->cr[3] & ~0xfff) + ((addr >> 20) & ~3)) & env->a20_mask);
        pde = ldl_raw(pde_ptr);
        if (!(pde & PG_PRESENT_MASK)) 
            return -1;
        if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
            pte = pde & ~0x003ff000; /* align to 4MB */
            page_size = 4096 * 1024;
        } else {
            /* page directory entry */
            pte_ptr = phys_ram_base + 
                (((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & env->a20_mask);
            pte = ldl_raw(pte_ptr);
            if (!(pte & PG_PRESENT_MASK))
                return -1;
            page_size = 4096;
        }
    }
    pte = pte & env->a20_mask;
    page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
    paddr = (pte & TARGET_PAGE_MASK) + page_offset;
    return paddr;
}
#endif

#if defined(USE_CODE_COPY)
struct fpstate {
    uint16_t fpuc;
    uint16_t dummy1;
    uint16_t fpus;
    uint16_t dummy2;
    uint16_t fptag;
    uint16_t dummy3;

    uint32_t fpip;
    uint32_t fpcs;
    uint32_t fpoo;
    uint32_t fpos;
    uint8_t fpregs1[8 * 10];
};

void restore_native_fp_state(CPUState *env)
{
    int fptag, i, j;
    struct fpstate fp1, *fp = &fp1;
    
    fp->fpuc = env->fpuc;
    fp->fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    fptag = 0;
    for (i=7; i>=0; i--) {
	fptag <<= 2;
	if (env->fptags[i]) {
            fptag |= 3;
        } else {
            /* the FPU automatically computes it */
        }
    }
    fp->fptag = fptag;
    j = env->fpstt;
    for(i = 0;i < 8; i++) {
        memcpy(&fp->fpregs1[i * 10], &env->fpregs[j], 10);
        j = (j + 1) & 7;
    }
    asm volatile ("frstor %0" : "=m" (*fp));
    env->native_fp_regs = 1;
}
 
void save_native_fp_state(CPUState *env)
{
    int fptag, i, j;
    uint16_t fpuc;
    struct fpstate fp1, *fp = &fp1;

    asm volatile ("fsave %0" : : "m" (*fp));
    env->fpuc = fp->fpuc;
    env->fpstt = (fp->fpus >> 11) & 7;
    env->fpus = fp->fpus & ~0x3800;
    fptag = fp->fptag;
    for(i = 0;i < 8; i++) {
        env->fptags[i] = ((fptag & 3) == 3);
        fptag >>= 2;
    }
    j = env->fpstt;
    for(i = 0;i < 8; i++) {
        memcpy(&env->fpregs[j], &fp->fpregs1[i * 10], 10);
        j = (j + 1) & 7;
    }
    /* we must restore the default rounding state */
    /* XXX: we do not restore the exception state */
    fpuc = 0x037f | (env->fpuc & (3 << 10));
    asm volatile("fldcw %0" : : "m" (fpuc));
    env->native_fp_regs = 0;
}
#endif
