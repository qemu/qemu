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
#include <sys/mman.h>

#include "cpu.h"
#include "exec-all.h"

//#define DEBUG_MMU

CPUX86State *cpu_x86_init(void)
{
    CPUX86State *env;
    int i;
    static int inited;

    cpu_exec_init();

    env = malloc(sizeof(CPUX86State));
    if (!env)
        return NULL;
    memset(env, 0, sizeof(CPUX86State));
    /* basic FPU init */
    for(i = 0;i < 8; i++)
        env->fptags[i] = 1;
    env->fpuc = 0x37f;
    /* flags setup : we activate the IRQs by default as in user mode */
    env->eflags = 0x2 | IF_MASK;

    tlb_flush(env);
#ifdef CONFIG_SOFTMMU
    env->hflags |= HF_SOFTMMU_MASK;
#endif
    /* init various static tables */
    if (!inited) {
        inited = 1;
        optimize_flags_init();
    }
    return env;
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
    "MUL",
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
    int eflags;
    char cc_op_name[32];

    eflags = env->eflags;
    fprintf(f, "EAX=%08x EBX=%08x ECX=%08x EDX=%08x\n"
            "ESI=%08x EDI=%08x EBP=%08x ESP=%08x\n"
            "EIP=%08x EFL=%08x [%c%c%c%c%c%c%c]\n",
            env->regs[R_EAX], env->regs[R_EBX], env->regs[R_ECX], env->regs[R_EDX], 
            env->regs[R_ESI], env->regs[R_EDI], env->regs[R_EBP], env->regs[R_ESP], 
            env->eip, eflags,
            eflags & DF_MASK ? 'D' : '-',
            eflags & CC_O ? 'O' : '-',
            eflags & CC_S ? 'S' : '-',
            eflags & CC_Z ? 'Z' : '-',
            eflags & CC_A ? 'A' : '-',
            eflags & CC_P ? 'P' : '-',
            eflags & CC_C ? 'C' : '-');
    fprintf(f, "CS=%04x SS=%04x DS=%04x ES=%04x FS=%04x GS=%04x\n",
            env->segs[R_CS].selector,
            env->segs[R_SS].selector,
            env->segs[R_DS].selector,
            env->segs[R_ES].selector,
            env->segs[R_FS].selector,
            env->segs[R_GS].selector);
    if (flags & X86_DUMP_CCOP) {
        if ((unsigned)env->cc_op < CC_OP_NB)
            strcpy(cc_op_name, cc_op_str[env->cc_op]);
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

/* called when cr3 or PG bit are modified */
static int last_pg_state = -1;
static int last_pe_state = 0;
static uint32_t a20_mask;
int a20_enabled;

int phys_ram_size;
int phys_ram_fd;
uint8_t *phys_ram_base;

void cpu_x86_set_a20(CPUX86State *env, int a20_state)
{
    a20_state = (a20_state != 0);
    if (a20_state != a20_enabled) {
        /* when a20 is changed, all the MMU mappings are invalid, so
           we must flush everything */
        page_unmap();
        tlb_flush(env);
        a20_enabled = a20_state;
        if (a20_enabled)
            a20_mask = 0xffffffff;
        else
            a20_mask = 0xffefffff;
    }
}

void cpu_x86_update_cr0(CPUX86State *env)
{
    int pg_state, pe_state;

#ifdef DEBUG_MMU
    printf("CR0 update: CR0=0x%08x\n", env->cr[0]);
#endif
    pg_state = env->cr[0] & CR0_PG_MASK;
    if (pg_state != last_pg_state) {
        page_unmap();
        tlb_flush(env);
        last_pg_state = pg_state;
    }
    pe_state = env->cr[0] & CR0_PE_MASK;
    if (last_pe_state != pe_state) {
        tb_flush();
        last_pe_state = pe_state;
    }
}

void cpu_x86_update_cr3(CPUX86State *env)
{
    if (env->cr[0] & CR0_PG_MASK) {
#if defined(DEBUG_MMU)
        printf("CR3 update: CR3=%08x\n", env->cr[3]);
#endif
        page_unmap();
        tlb_flush(env);
    }
}

void cpu_x86_init_mmu(CPUX86State *env)
{
    a20_enabled = 1;
    a20_mask = 0xffffffff;

    last_pg_state = -1;
    cpu_x86_update_cr0(env);
}

/* XXX: also flush 4MB pages */
void cpu_x86_flush_tlb(CPUX86State *env, uint32_t addr)
{
    int flags;
    unsigned long virt_addr;

    tlb_flush_page(env, addr);

    flags = page_get_flags(addr);
    if (flags & PAGE_VALID) {
        virt_addr = addr & ~0xfff;
#if !defined(CONFIG_SOFTMMU)
        munmap((void *)virt_addr, 4096);
#endif
        page_set_flags(virt_addr, virt_addr + 4096, 0);
    }
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
    uint32_t pde, pte, virt_addr;
    int error_code, is_dirty, prot, page_size, ret;
    unsigned long pd;
    
#ifdef DEBUG_MMU
    printf("MMU fault: addr=0x%08x w=%d u=%d eip=%08x\n", 
           addr, is_write, is_user, env->eip);
#endif

    if (env->user_mode_only) {
        /* user mode only emulation */
        error_code = 0;
        goto do_fault;
    }

    if (!(env->cr[0] & CR0_PG_MASK)) {
        pte = addr;
        virt_addr = addr & TARGET_PAGE_MASK;
        prot = PROT_READ | PROT_WRITE;
        page_size = 4096;
        goto do_mapping;
    }

    /* page directory entry */
    pde_ptr = phys_ram_base + 
        (((env->cr[3] & ~0xfff) + ((addr >> 20) & ~3)) & a20_mask);
    pde = ldl_raw(pde_ptr);
    if (!(pde & PG_PRESENT_MASK)) {
        error_code = 0;
        goto do_fault;
    }
    if (is_user) {
        if (!(pde & PG_USER_MASK))
            goto do_fault_protect;
        if (is_write && !(pde & PG_RW_MASK))
            goto do_fault_protect;
    } else {
        if ((env->cr[0] & CR0_WP_MASK) && (pde & PG_USER_MASK) &&
            is_write && !(pde & PG_RW_MASK)) 
            goto do_fault_protect;
    }
    /* if PSE bit is set, then we use a 4MB page */
    if ((pde & PG_PSE_MASK) && (env->cr[4] & CR4_PSE_MASK)) {
        is_dirty = is_write && !(pde & PG_DIRTY_MASK);
        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            if (is_dirty)
                pde |= PG_DIRTY_MASK;
            stl_raw(pde_ptr, pde);
        }
        
        pte = pde & ~0x003ff000; /* align to 4MB */
        page_size = 4096 * 1024;
        virt_addr = addr & ~0x003fffff;
    } else {
        if (!(pde & PG_ACCESSED_MASK)) {
            pde |= PG_ACCESSED_MASK;
            stl_raw(pde_ptr, pde);
        }

        /* page directory entry */
        pte_ptr = phys_ram_base + 
            (((pde & ~0xfff) + ((addr >> 10) & 0xffc)) & a20_mask);
        pte = ldl_raw(pte_ptr);
        if (!(pte & PG_PRESENT_MASK)) {
            error_code = 0;
            goto do_fault;
        }
        if (is_user) {
            if (!(pte & PG_USER_MASK))
                goto do_fault_protect;
            if (is_write && !(pte & PG_RW_MASK))
                goto do_fault_protect;
        } else {
            if ((env->cr[0] & CR0_WP_MASK) && (pte & PG_USER_MASK) &&
                is_write && !(pte & PG_RW_MASK)) 
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
    prot = PROT_READ;
    if (is_user) {
        if (pte & PG_RW_MASK)
            prot |= PROT_WRITE;
    } else {
        if (!(env->cr[0] & CR0_WP_MASK) || !(pte & PG_USER_MASK) ||
            (pte & PG_RW_MASK))
            prot |= PROT_WRITE;
    }
    
 do_mapping:
    pte = pte & a20_mask;
#if !defined(CONFIG_SOFTMMU)
    if (is_softmmu) 
#endif
    {
        unsigned long paddr, vaddr, address, addend, page_offset;
        int index;

        /* software MMU case. Even if 4MB pages, we map only one 4KB
           page in the cache to avoid filling it too fast */
        page_offset = (addr & TARGET_PAGE_MASK) & (page_size - 1);
        paddr = (pte & TARGET_PAGE_MASK) + page_offset;
        vaddr = virt_addr + page_offset;
        index = (addr >> 12) & (CPU_TLB_SIZE - 1);
        pd = physpage_find(paddr);
        if (pd & 0xfff) {
            /* IO memory case */
            address = vaddr | pd;
            addend = paddr;
        } else {
            /* standard memory */
            address = vaddr;
            addend = (unsigned long)phys_ram_base + pd;
        }
        addend -= vaddr;
        env->tlb_read[is_user][index].address = address;
        env->tlb_read[is_user][index].addend = addend;
        if (prot & PROT_WRITE) {
            env->tlb_write[is_user][index].address = address;
            env->tlb_write[is_user][index].addend = addend;
        }
        page_set_flags(vaddr, vaddr + TARGET_PAGE_SIZE, 
                       PAGE_VALID | PAGE_EXEC | prot);
        ret = 0;
    }
#if !defined(CONFIG_SOFTMMU)
    else {
        ret = 0;
        /* XXX: incorrect for 4MB pages */
        pd = physpage_find(pte & ~0xfff);
        if ((pd & 0xfff) != 0) {
            /* IO access: no mapping is done as it will be handled by the
               soft MMU */
            if (!(env->hflags & HF_SOFTMMU_MASK))
                ret = 2;
        } else {
            void *map_addr;
            map_addr = mmap((void *)virt_addr, page_size, prot, 
                            MAP_SHARED | MAP_FIXED, phys_ram_fd, pd);
            if (map_addr == MAP_FAILED) {
                fprintf(stderr, 
                        "mmap failed when mapped physical address 0x%08x to virtual address 0x%08x\n",
                        pte & ~0xfff, virt_addr);
                exit(1);
            }
#ifdef DEBUG_MMU
            printf("mmaping 0x%08x to virt 0x%08x pse=%d\n", 
                   pte & ~0xfff, virt_addr, (page_size != 4096));
#endif
            page_set_flags(virt_addr, virt_addr + page_size, 
                           PAGE_VALID | PAGE_EXEC | prot);
        }
    }
#endif
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
