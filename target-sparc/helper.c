/*
 *  sparc helpers
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
#include "exec.h"

#define DEBUG_PCALL

/* Sparc MMU emulation */
int cpu_sparc_handle_mmu_fault (CPUState *env, uint32_t address, int rw,
                              int is_user, int is_softmmu);

/* thread support */

spinlock_t global_cpu_lock = SPIN_LOCK_UNLOCKED;

void cpu_lock(void)
{
    spin_lock(&global_cpu_lock);
}

void cpu_unlock(void)
{
    spin_unlock(&global_cpu_lock);
}

#if !defined(CONFIG_USER_ONLY) 

#define MMUSUFFIX _mmu
#define GETPC() (__builtin_return_address(0))

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"


/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(unsigned long addr, int is_write, int is_user, void *retaddr)
{
    TranslationBlock *tb;
    int ret;
    unsigned long pc;
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    ret = cpu_sparc_handle_mmu_fault(env, addr, is_write, is_user, 1);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, NULL);
            }
        }
        raise_exception_err(ret, env->error_code);
    }
    env = saved_env;
}
#endif

static const int access_table[8][8] = {
    { 0, 0, 0, 0, 2, 0, 3, 3 },
    { 0, 0, 0, 0, 2, 0, 0, 0 },
    { 2, 2, 0, 0, 0, 2, 3, 3 },
    { 2, 2, 0, 0, 0, 2, 0, 0 },
    { 2, 0, 2, 0, 2, 2, 3, 3 },
    { 2, 0, 2, 0, 2, 0, 2, 0 },
    { 2, 2, 2, 0, 2, 2, 3, 3 },
    { 2, 2, 2, 0, 2, 2, 2, 0 }
};

/* 1 = write OK */
static const int rw_table[2][8] = {
    { 0, 1, 0, 1, 0, 1, 0, 1 },
    { 0, 1, 0, 1, 0, 0, 0, 0 }
};


/* Perform address translation */
int cpu_sparc_handle_mmu_fault (CPUState *env, uint32_t address, int rw,
                              int is_user, int is_softmmu)
{
    int exception = 0;
    int access_perms = 0, access_index = 0;
    uint8_t *pde_ptr;
    uint32_t pde, virt_addr;
    int error_code = 0, is_dirty, prot, ret = 0;
    unsigned long paddr, vaddr, page_offset;

    if (env->user_mode_only) {
        /* user mode only emulation */
        ret = -2;
        goto do_fault;
    }

    virt_addr = address & TARGET_PAGE_MASK;
    if ((env->mmuregs[0] & MMU_E) == 0) { /* MMU disabled */
	paddr = address;
	page_offset = address & (TARGET_PAGE_SIZE - 1);
        prot = PAGE_READ | PAGE_WRITE;
        goto do_mapping;
    }

    /* SPARC reference MMU table walk: Context table->L1->L2->PTE */
    /* Context base + context number */
    pde_ptr = phys_ram_base + (env->mmuregs[1] << 4) + (env->mmuregs[2] << 4);
    pde = ldl_raw(pde_ptr);

    /* Ctx pde */
    switch (pde & PTE_ENTRYTYPE_MASK) {
    case 0: /* Invalid */
        error_code = 1;
        goto do_fault;
    case 2: /* PTE, maybe should not happen? */
    case 3: /* Reserved */
        error_code = 4;
        goto do_fault;
    case 1: /* L1 PDE */
	pde_ptr = phys_ram_base + ((address >> 22) & ~3) + ((pde & ~3) << 4);
	pde = ldl_raw(pde_ptr);

	switch (pde & PTE_ENTRYTYPE_MASK) {
	case 0: /* Invalid */
	    error_code = 1;
	    goto do_fault;
	case 3: /* Reserved */
	    error_code = 4;
	    goto do_fault;
	case 1: /* L2 PDE */
	    pde_ptr = phys_ram_base + ((address & 0xfc0000) >> 16) + ((pde & ~3) << 4);
	    pde = ldl_raw(pde_ptr);

	    switch (pde & PTE_ENTRYTYPE_MASK) {
	    case 0: /* Invalid */
		error_code = 1;
		goto do_fault;
	    case 3: /* Reserved */
		error_code = 4;
		goto do_fault;
	    case 1: /* L3 PDE */
		pde_ptr = phys_ram_base + ((address & 0x3f000) >> 10) + ((pde & ~3) << 4);
		pde = ldl_raw(pde_ptr);

		switch (pde & PTE_ENTRYTYPE_MASK) {
		case 0: /* Invalid */
		    error_code = 1;
		    goto do_fault;
		case 1: /* PDE, should not happen */
		case 3: /* Reserved */
		    error_code = 4;
		    goto do_fault;
		case 2: /* L3 PTE */
		    virt_addr = address & TARGET_PAGE_MASK;
		    page_offset = (address & TARGET_PAGE_MASK) & (TARGET_PAGE_SIZE - 1);
		}
		break;
	    case 2: /* L2 PTE */
		virt_addr = address & ~0x3ffff;
		page_offset = address & 0x3ffff;
	    }
	    break;
	case 2: /* L1 PTE */
	    virt_addr = address & ~0xffffff;
	    page_offset = address & 0xffffff;
	}
    }

    /* update page modified and dirty bits */
    is_dirty = (rw & 1) && !(pde & PG_MODIFIED_MASK);
    if (!(pde & PG_ACCESSED_MASK) || is_dirty) {
	pde |= PG_ACCESSED_MASK;
	if (is_dirty)
	    pde |= PG_MODIFIED_MASK;
	stl_raw(pde_ptr, pde);
    }

    /* check access */
    access_index = ((rw & 1) << 2) | (rw & 2) | (is_user? 0 : 1);
    access_perms = (pde & PTE_ACCESS_MASK) >> PTE_ACCESS_SHIFT;
    error_code = access_table[access_index][access_perms];
    if (error_code)
	goto do_fault;

    /* the page can be put in the TLB */
    prot = PAGE_READ;
    if (pde & PG_MODIFIED_MASK) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
	if (rw_table[is_user][access_perms])
	        prot |= PAGE_WRITE;
    }

    /* Even if large ptes, we map only one 4KB page in the cache to
       avoid filling it too fast */
    virt_addr = address & TARGET_PAGE_MASK;
    paddr = ((pde & PTE_ADDR_MASK) << 4) + page_offset;

 do_mapping:
    vaddr = virt_addr + ((address & TARGET_PAGE_MASK) & (TARGET_PAGE_SIZE - 1));

    ret = tlb_set_page(env, vaddr, paddr, prot, is_user, is_softmmu);
    return ret;

 do_fault:
    if (env->mmuregs[3]) /* Fault status register */
	env->mmuregs[3] = 1; /* overflow (not read before another fault) */
    env->mmuregs[3] |= (access_index << 5) | (error_code << 2) | 2;
    env->mmuregs[4] = address; /* Fault address register */

    if (env->mmuregs[0] & MMU_NF || env->psret == 0) // No fault
	return 0;

    env->exception_index = exception;
    env->error_code = error_code;
    return error_code;
}

void memcpy32(uint32_t *dst, const uint32_t *src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = src[3];
    dst[4] = src[4];
    dst[5] = src[5];
    dst[6] = src[6];
    dst[7] = src[7];
}

void set_cwp(int new_cwp)
{
    /* put the modified wrap registers at their proper location */
    if (env->cwp == (NWINDOWS - 1))
        memcpy32(env->regbase, env->regbase + NWINDOWS * 16);
    env->cwp = new_cwp;
    /* put the wrap registers at their temporary location */
    if (new_cwp == (NWINDOWS - 1))
        memcpy32(env->regbase + NWINDOWS * 16, env->regbase);
    env->regwptr = env->regbase + (new_cwp * 16);
}

/*
 * Begin execution of an interruption. is_int is TRUE if coming from
 * the int instruction. next_eip is the EIP value AFTER the interrupt
 * instruction. It is only relevant if is_int is TRUE.  
 */
void do_interrupt(int intno, int is_int, int error_code, 
                  unsigned int next_eip, int is_hw)
{
    int cwp;

#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
	static int count;
	fprintf(logfile, "%6d: v=%02x e=%04x i=%d pc=%08x npc=%08x SP=%08x\n",
                    count, intno, error_code, is_int,
                    env->pc,
                    env->npc, env->regwptr[6]);
#if 0
	cpu_dump_state(env, logfile, fprintf, 0);
	{
	    int i;
	    uint8_t *ptr;
	    fprintf(logfile, "       code=");
	    ptr = env->pc;
	    for(i = 0; i < 16; i++) {
		fprintf(logfile, " %02x", ldub(ptr + i));
	    }
	    fprintf(logfile, "\n");
	}
#endif
	count++;
    }
#endif
    env->psret = 0;
    cwp = (env->cwp - 1) & (NWINDOWS - 1); 
    set_cwp(cwp);
    env->regwptr[9] = env->pc;
    env->regwptr[10] = env->npc;
    env->psrps = env->psrs;
    env->psrs = 1;
    env->tbr = (env->tbr & TBR_BASE_MASK) | (intno << 4);
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}

void raise_exception_err(int exception_index, int error_code)
{
    raise_exception(exception_index);
}
