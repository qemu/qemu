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

//#define DEBUG_PCALL
//#define DEBUG_MMU

/* Sparc MMU emulation */

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

#if defined(CONFIG_USER_ONLY) 

int cpu_sparc_handle_mmu_fault(CPUState *env, target_ulong address, int rw,
                               int is_user, int is_softmmu)
{
    env->mmuregs[4] = address;
    env->exception_index = 0; /* XXX: must be incorrect */
    env->error_code = -2; /* XXX: is it really used ! */
    return 1;
}

#else

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
void tlb_fill(target_ulong addr, int is_write, int is_user, void *retaddr)
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
        raise_exception(ret);
    }
    env = saved_env;
}

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

int get_physical_address (CPUState *env, target_phys_addr_t *physical, int *prot,
			  int *access_index, target_ulong address, int rw,
			  int is_user)
{
    int access_perms = 0;
    target_phys_addr_t pde_ptr;
    uint32_t pde;
    target_ulong virt_addr;
    int error_code = 0, is_dirty;
    unsigned long page_offset;

    virt_addr = address & TARGET_PAGE_MASK;
    if ((env->mmuregs[0] & MMU_E) == 0) { /* MMU disabled */
	*physical = address;
        *prot = PAGE_READ | PAGE_WRITE;
        return 0;
    }

    /* SPARC reference MMU table walk: Context table->L1->L2->PTE */
    /* Context base + context number */
    pde_ptr = (env->mmuregs[1] << 4) + (env->mmuregs[2] << 4);
    pde = ldl_phys(pde_ptr);

    /* Ctx pde */
    switch (pde & PTE_ENTRYTYPE_MASK) {
    default:
    case 0: /* Invalid */
	return 1;
    case 2: /* L0 PTE, maybe should not happen? */
    case 3: /* Reserved */
        return 4;
    case 1: /* L0 PDE */
	pde_ptr = ((address >> 22) & ~3) + ((pde & ~3) << 4);
        pde = ldl_phys(pde_ptr);

	switch (pde & PTE_ENTRYTYPE_MASK) {
	default:
	case 0: /* Invalid */
	    return 1;
	case 3: /* Reserved */
	    return 4;
	case 1: /* L1 PDE */
	    pde_ptr = ((address & 0xfc0000) >> 16) + ((pde & ~3) << 4);
            pde = ldl_phys(pde_ptr);

	    switch (pde & PTE_ENTRYTYPE_MASK) {
	    default:
	    case 0: /* Invalid */
		return 1;
	    case 3: /* Reserved */
		return 4;
	    case 1: /* L2 PDE */
		pde_ptr = ((address & 0x3f000) >> 10) + ((pde & ~3) << 4);
                pde = ldl_phys(pde_ptr);

		switch (pde & PTE_ENTRYTYPE_MASK) {
		default:
		case 0: /* Invalid */
		    return 1;
		case 1: /* PDE, should not happen */
		case 3: /* Reserved */
		    return 4;
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
        stl_phys_notdirty(pde_ptr, pde);
    }
    /* check access */
    *access_index = ((rw & 1) << 2) | (rw & 2) | (is_user? 0 : 1);
    access_perms = (pde & PTE_ACCESS_MASK) >> PTE_ACCESS_SHIFT;
    error_code = access_table[*access_index][access_perms];
    if (error_code)
	return error_code;

    /* the page can be put in the TLB */
    *prot = PAGE_READ;
    if (pde & PG_MODIFIED_MASK) {
        /* only set write access if already dirty... otherwise wait
           for dirty access */
	if (rw_table[is_user][access_perms])
	        *prot |= PAGE_WRITE;
    }

    /* Even if large ptes, we map only one 4KB page in the cache to
       avoid filling it too fast */
    *physical = ((pde & PTE_ADDR_MASK) << 4) + page_offset;
    return 0;
}

/* Perform address translation */
int cpu_sparc_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int is_user, int is_softmmu)
{
    int exception = 0;
    target_ulong virt_addr;
    target_phys_addr_t paddr;
    unsigned long vaddr;
    int error_code = 0, prot, ret = 0, access_index;

    error_code = get_physical_address(env, &paddr, &prot, &access_index, address, rw, is_user);
    if (error_code == 0) {
	virt_addr = address & TARGET_PAGE_MASK;
	vaddr = virt_addr + ((address & TARGET_PAGE_MASK) & (TARGET_PAGE_SIZE - 1));
	ret = tlb_set_page(env, vaddr, paddr, prot, is_user, is_softmmu);
	return ret;
    }

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
#endif

void memcpy32(target_ulong *dst, const target_ulong *src)
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

void cpu_set_cwp(CPUState *env1, int new_cwp)
{
    CPUState *saved_env;
    saved_env = env;
    env = env1;
    set_cwp(new_cwp);
    env = saved_env;
}

void do_interrupt(int intno, int error_code)
{
    int cwp;

#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
	static int count;
	fprintf(logfile, "%6d: v=%02x e=%04x pc=%08x npc=%08x SP=%08x\n",
                count, intno, error_code,
                env->pc,
                env->npc, env->regwptr[6]);
#if 1
	cpu_dump_state(env, logfile, fprintf, 0);
	{
	    int i;
	    uint8_t *ptr;

	    fprintf(logfile, "       code=");
	    ptr = (uint8_t *)env->pc;
	    for(i = 0; i < 16; i++) {
		fprintf(logfile, " %02x", ldub(ptr + i));
	    }
	    fprintf(logfile, "\n");
	}
#endif
	count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY) 
    if (env->psret == 0) {
        cpu_abort(cpu_single_env, "Trap while interrupts disabled, Error state");
	return;
    }
#endif
    env->psret = 0;
    cwp = (env->cwp - 1) & (NWINDOWS - 1); 
    set_cwp(cwp);
    if (intno & 0x80) {
	env->regwptr[9] = env->pc;
	env->regwptr[10] = env->npc;
    } else {
        /* XXX: this code is clearly incorrect - npc should have the
           incorrect value */
	env->regwptr[9] = env->pc - 4; // XXX?
	env->regwptr[10] = env->pc;
    }
    env->psrps = env->psrs;
    env->psrs = 1;
    env->tbr = (env->tbr & TBR_BASE_MASK) | (intno << 4);
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}

target_ulong mmu_probe(target_ulong address, int mmulev)
{
    target_phys_addr_t pde_ptr;
    uint32_t pde;

    /* Context base + context number */
    pde_ptr = (env->mmuregs[1] << 4) + (env->mmuregs[2] << 4);
    pde = ldl_phys(pde_ptr);

    switch (pde & PTE_ENTRYTYPE_MASK) {
    default:
    case 0: /* Invalid */
    case 2: /* PTE, maybe should not happen? */
    case 3: /* Reserved */
	return 0;
    case 1: /* L1 PDE */
	if (mmulev == 3)
	    return pde;
	pde_ptr = ((address >> 22) & ~3) + ((pde & ~3) << 4);
        pde = ldl_phys(pde_ptr);

	switch (pde & PTE_ENTRYTYPE_MASK) {
	default:
	case 0: /* Invalid */
	case 3: /* Reserved */
	    return 0;
	case 2: /* L1 PTE */
	    return pde;
	case 1: /* L2 PDE */
	    if (mmulev == 2)
		return pde;
	    pde_ptr = ((address & 0xfc0000) >> 16) + ((pde & ~3) << 4);
            pde = ldl_phys(pde_ptr);

	    switch (pde & PTE_ENTRYTYPE_MASK) {
	    default:
	    case 0: /* Invalid */
	    case 3: /* Reserved */
		return 0;
	    case 2: /* L2 PTE */
		return pde;
	    case 1: /* L3 PDE */
		if (mmulev == 1)
		    return pde;
		pde_ptr = ((address & 0x3f000) >> 10) + ((pde & ~3) << 4);
                pde = ldl_phys(pde_ptr);

		switch (pde & PTE_ENTRYTYPE_MASK) {
		default:
		case 0: /* Invalid */
		case 1: /* PDE, should not happen */
		case 3: /* Reserved */
		    return 0;
		case 2: /* L3 PTE */
		    return pde;
		}
	    }
	}
    }
    return 0;
}

void dump_mmu(void)
{
#ifdef DEBUG_MMU
     target_ulong va, va1, va2;
     unsigned int n, m, o;
     target_phys_addr_t pde_ptr, pa;
    uint32_t pde;

    printf("MMU dump:\n");
    pde_ptr = (env->mmuregs[1] << 4) + (env->mmuregs[2] << 4);
    pde = ldl_phys(pde_ptr);
    printf("Root ptr: " TARGET_FMT_lx ", ctx: %d\n", env->mmuregs[1] << 4, env->mmuregs[2]);
    for (n = 0, va = 0; n < 256; n++, va += 16 * 1024 * 1024) {
	pde_ptr = mmu_probe(va, 2);
	if (pde_ptr) {
	    pa = cpu_get_phys_page_debug(env, va);
 	    printf("VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_lx " PDE: " TARGET_FMT_lx "\n", va, pa, pde_ptr);
	    for (m = 0, va1 = va; m < 64; m++, va1 += 256 * 1024) {
		pde_ptr = mmu_probe(va1, 1);
		if (pde_ptr) {
		    pa = cpu_get_phys_page_debug(env, va1);
 		    printf(" VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_lx " PDE: " TARGET_FMT_lx "\n", va1, pa, pde_ptr);
		    for (o = 0, va2 = va1; o < 64; o++, va2 += 4 * 1024) {
			pde_ptr = mmu_probe(va2, 0);
			if (pde_ptr) {
			    pa = cpu_get_phys_page_debug(env, va2);
 			    printf("  VA: " TARGET_FMT_lx ", PA: " TARGET_FMT_lx " PTE: " TARGET_FMT_lx "\n", va2, pa, pde_ptr);
			}
		    }
		}
	    }
	}
    }
    printf("MMU dump ends\n");
#endif
}
