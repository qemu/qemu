/*
 *  SH4 emulation
 *
 *  Copyright (c) 2005 Samuel Tardieu
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
#include "hw/sh_intc.h"

#if defined(CONFIG_USER_ONLY)

void do_interrupt (CPUState *env)
{
  env->exception_index = -1;
}

int cpu_sh4_handle_mmu_fault(CPUState * env, target_ulong address, int rw,
			     int mmu_idx, int is_softmmu)
{
    env->tea = address;
    env->exception_index = 0;
    switch (rw) {
    case 0:
        env->exception_index = 0x0a0;
        break;
    case 1:
        env->exception_index = 0x0c0;
        break;
    case 2:
        env->exception_index = 0x0a0;
        break;
    }
    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
    return addr;
}

#else /* !CONFIG_USER_ONLY */

#define MMU_OK                   0
#define MMU_ITLB_MISS            (-1)
#define MMU_ITLB_MULTIPLE        (-2)
#define MMU_ITLB_VIOLATION       (-3)
#define MMU_DTLB_MISS_READ       (-4)
#define MMU_DTLB_MISS_WRITE      (-5)
#define MMU_DTLB_INITIAL_WRITE   (-6)
#define MMU_DTLB_VIOLATION_READ  (-7)
#define MMU_DTLB_VIOLATION_WRITE (-8)
#define MMU_DTLB_MULTIPLE        (-9)
#define MMU_DTLB_MISS            (-10)
#define MMU_IADDR_ERROR          (-11)
#define MMU_DADDR_ERROR_READ     (-12)
#define MMU_DADDR_ERROR_WRITE    (-13)

void do_interrupt(CPUState * env)
{
    int do_irq = env->interrupt_request & CPU_INTERRUPT_HARD;
    int do_exp, irq_vector = env->exception_index;

    /* prioritize exceptions over interrupts */

    do_exp = env->exception_index != -1;
    do_irq = do_irq && (env->exception_index == -1);

    if (env->sr & SR_BL) {
        if (do_exp && env->exception_index != 0x1e0) {
            env->exception_index = 0x000; /* masked exception -> reset */
        }
        if (do_irq && !env->intr_at_halt) {
            return; /* masked */
        }
        env->intr_at_halt = 0;
    }

    if (do_irq) {
        irq_vector = sh_intc_get_pending_vector(env->intc_handle,
						(env->sr >> 4) & 0xf);
        if (irq_vector == -1) {
            return; /* masked */
	}
    }

    if (loglevel & CPU_LOG_INT) {
	const char *expname;
	switch (env->exception_index) {
	case 0x0e0:
	    expname = "addr_error";
	    break;
	case 0x040:
	    expname = "tlb_miss";
	    break;
	case 0x0a0:
	    expname = "tlb_violation";
	    break;
	case 0x180:
	    expname = "illegal_instruction";
	    break;
	case 0x1a0:
	    expname = "slot_illegal_instruction";
	    break;
	case 0x800:
	    expname = "fpu_disable";
	    break;
	case 0x820:
	    expname = "slot_fpu";
	    break;
	case 0x100:
	    expname = "data_write";
	    break;
	case 0x060:
	    expname = "dtlb_miss_write";
	    break;
	case 0x0c0:
	    expname = "dtlb_violation_write";
	    break;
	case 0x120:
	    expname = "fpu_exception";
	    break;
	case 0x080:
	    expname = "initial_page_write";
	    break;
	case 0x160:
	    expname = "trapa";
	    break;
	default:
            expname = do_irq ? "interrupt" : "???";
            break;
	}
	fprintf(logfile, "exception 0x%03x [%s] raised\n",
		irq_vector, expname);
	cpu_dump_state(env, logfile, fprintf, 0);
    }

    env->ssr = env->sr;
    env->spc = env->pc;
    env->sgr = env->gregs[15];
    env->sr |= SR_BL | SR_MD | SR_RB;

    if (env->flags & (DELAY_SLOT | DELAY_SLOT_CONDITIONAL)) {
        /* Branch instruction should be executed again before delay slot. */
	env->spc -= 2;
	/* Clear flags for exception/interrupt routine. */
	env->flags &= ~(DELAY_SLOT | DELAY_SLOT_CONDITIONAL | DELAY_SLOT_TRUE);
    }
    if (env->flags & DELAY_SLOT_CLEARME)
        env->flags = 0;

    if (do_exp) {
        env->expevt = env->exception_index;
        switch (env->exception_index) {
        case 0x000:
        case 0x020:
        case 0x140:
            env->sr &= ~SR_FD;
            env->sr |= 0xf << 4; /* IMASK */
            env->pc = 0xa0000000;
            break;
        case 0x040:
        case 0x060:
            env->pc = env->vbr + 0x400;
            break;
        case 0x160:
            env->spc += 2; /* special case for TRAPA */
            /* fall through */
        default:
            env->pc = env->vbr + 0x100;
            break;
        }
        return;
    }

    if (do_irq) {
        env->intevt = irq_vector;
        env->pc = env->vbr + 0x600;
        return;
    }
}

static void update_itlb_use(CPUState * env, int itlbnb)
{
    uint8_t or_mask = 0, and_mask = (uint8_t) - 1;

    switch (itlbnb) {
    case 0:
	and_mask = 0x1f;
	break;
    case 1:
	and_mask = 0xe7;
	or_mask = 0x80;
	break;
    case 2:
	and_mask = 0xfb;
	or_mask = 0x50;
	break;
    case 3:
	or_mask = 0x2c;
	break;
    }

    env->mmucr &= (and_mask << 24) | 0x00ffffff;
    env->mmucr |= (or_mask << 24);
}

static int itlb_replacement(CPUState * env)
{
    if ((env->mmucr & 0xe0000000) == 0xe0000000)
	return 0;
    if ((env->mmucr & 0x98000000) == 0x18000000)
	return 1;
    if ((env->mmucr & 0x54000000) == 0x04000000)
	return 2;
    if ((env->mmucr & 0x2c000000) == 0x00000000)
	return 3;
    assert(0);
}

/* Find the corresponding entry in the right TLB
   Return entry, MMU_DTLB_MISS or MMU_DTLB_MULTIPLE
*/
static int find_tlb_entry(CPUState * env, target_ulong address,
			  tlb_t * entries, uint8_t nbtlb, int use_asid)
{
    int match = MMU_DTLB_MISS;
    uint32_t start, end;
    uint8_t asid;
    int i;

    asid = env->pteh & 0xff;

    for (i = 0; i < nbtlb; i++) {
	if (!entries[i].v)
	    continue;		/* Invalid entry */
	if (use_asid && entries[i].asid != asid)
	    continue;		/* Bad ASID */
#if 0
	switch (entries[i].sz) {
	case 0:
	    size = 1024;	/* 1kB */
	    break;
	case 1:
	    size = 4 * 1024;	/* 4kB */
	    break;
	case 2:
	    size = 64 * 1024;	/* 64kB */
	    break;
	case 3:
	    size = 1024 * 1024;	/* 1MB */
	    break;
	default:
	    assert(0);
	}
#endif
	start = (entries[i].vpn << 10) & ~(entries[i].size - 1);
	end = start + entries[i].size - 1;
	if (address >= start && address <= end) {	/* Match */
	    if (match != MMU_DTLB_MISS)
		return MMU_DTLB_MULTIPLE;	/* Multiple match */
	    match = i;
	}
    }
    return match;
}

static int same_tlb_entry_exists(const tlb_t * haystack, uint8_t nbtlb,
				 const tlb_t * needle)
{
    int i;
    for (i = 0; i < nbtlb; i++)
        if (!memcmp(&haystack[i], needle, sizeof(tlb_t)))
	    return 1;
    return 0;
}

static void increment_urc(CPUState * env)
{
    uint8_t urb, urc;

    /* Increment URC */
    urb = ((env->mmucr) >> 18) & 0x3f;
    urc = ((env->mmucr) >> 10) & 0x3f;
    urc++;
    if (urc == urb || urc == UTLB_SIZE - 1)
	urc = 0;
    env->mmucr = (env->mmucr & 0xffff03ff) | (urc << 10);
}

/* Find itlb entry - update itlb from utlb if necessary and asked for
   Return entry, MMU_ITLB_MISS, MMU_ITLB_MULTIPLE or MMU_DTLB_MULTIPLE
   Update the itlb from utlb if update is not 0
*/
int find_itlb_entry(CPUState * env, target_ulong address,
		    int use_asid, int update)
{
    int e, n;

    e = find_tlb_entry(env, address, env->itlb, ITLB_SIZE, use_asid);
    if (e == MMU_DTLB_MULTIPLE)
	e = MMU_ITLB_MULTIPLE;
    else if (e == MMU_DTLB_MISS && update) {
	e = find_tlb_entry(env, address, env->utlb, UTLB_SIZE, use_asid);
	if (e >= 0) {
	    tlb_t * ientry;
	    n = itlb_replacement(env);
	    ientry = &env->itlb[n];
	    if (ientry->v) {
		if (!same_tlb_entry_exists(env->utlb, UTLB_SIZE, ientry))
		    tlb_flush_page(env, ientry->vpn << 10);
	    }
	    *ientry = env->utlb[e];
	    e = n;
	} else if (e == MMU_DTLB_MISS)
	    e = MMU_ITLB_MISS;
    } else if (e == MMU_DTLB_MISS)
	e = MMU_ITLB_MISS;
    if (e >= 0)
	update_itlb_use(env, e);
    return e;
}

/* Find utlb entry
   Return entry, MMU_DTLB_MISS, MMU_DTLB_MULTIPLE */
int find_utlb_entry(CPUState * env, target_ulong address, int use_asid)
{
    /* per utlb access */
    increment_urc(env);

    /* Return entry */
    return find_tlb_entry(env, address, env->utlb, UTLB_SIZE, use_asid);
}

/* Match address against MMU
   Return MMU_OK, MMU_DTLB_MISS_READ, MMU_DTLB_MISS_WRITE,
   MMU_DTLB_INITIAL_WRITE, MMU_DTLB_VIOLATION_READ,
   MMU_DTLB_VIOLATION_WRITE, MMU_ITLB_MISS,
   MMU_ITLB_MULTIPLE, MMU_ITLB_VIOLATION,
   MMU_IADDR_ERROR, MMU_DADDR_ERROR_READ, MMU_DADDR_ERROR_WRITE.
*/
static int get_mmu_address(CPUState * env, target_ulong * physical,
			   int *prot, target_ulong address,
			   int rw, int access_type)
{
    int use_asid, n;
    tlb_t *matching = NULL;

    use_asid = (env->mmucr & MMUCR_SV) == 0 || (env->sr & SR_MD) == 0;

    if (rw == 2) {
	n = find_itlb_entry(env, address, use_asid, 1);
	if (n >= 0) {
	    matching = &env->itlb[n];
	    if ((env->sr & SR_MD) & !(matching->pr & 2))
		n = MMU_ITLB_VIOLATION;
	    else
		*prot = PAGE_READ;
	}
    } else {
	n = find_utlb_entry(env, address, use_asid);
	if (n >= 0) {
	    matching = &env->utlb[n];
	    switch ((matching->pr << 1) | ((env->sr & SR_MD) ? 1 : 0)) {
	    case 0:		/* 000 */
	    case 2:		/* 010 */
		n = (rw == 1) ? MMU_DTLB_VIOLATION_WRITE :
		    MMU_DTLB_VIOLATION_READ;
		break;
	    case 1:		/* 001 */
	    case 4:		/* 100 */
	    case 5:		/* 101 */
		if (rw == 1)
		    n = MMU_DTLB_VIOLATION_WRITE;
		else
		    *prot = PAGE_READ;
		break;
	    case 3:		/* 011 */
	    case 6:		/* 110 */
	    case 7:		/* 111 */
		*prot = (rw == 1)? PAGE_WRITE : PAGE_READ;
		break;
	    }
	} else if (n == MMU_DTLB_MISS) {
	    n = (rw == 1) ? MMU_DTLB_MISS_WRITE :
		MMU_DTLB_MISS_READ;
	}
    }
    if (n >= 0) {
	*physical = ((matching->ppn << 10) & ~(matching->size - 1)) |
	    (address & (matching->size - 1));
	if ((rw == 1) & !matching->d)
	    n = MMU_DTLB_INITIAL_WRITE;
	else
	    n = MMU_OK;
    }
    return n;
}

int get_physical_address(CPUState * env, target_ulong * physical,
			 int *prot, target_ulong address,
			 int rw, int access_type)
{
    /* P1, P2 and P4 areas do not use translation */
    if ((address >= 0x80000000 && address < 0xc0000000) ||
	address >= 0xe0000000) {
	if (!(env->sr & SR_MD)
	    && (address < 0xe0000000 || address > 0xe4000000)) {
	    /* Unauthorized access in user mode (only store queues are available) */
	    fprintf(stderr, "Unauthorized access\n");
	    if (rw == 0)
		return MMU_DADDR_ERROR_READ;
	    else if (rw == 1)
		return MMU_DADDR_ERROR_WRITE;
	    else
		return MMU_IADDR_ERROR;
	}
	if (address >= 0x80000000 && address < 0xc0000000) {
	    /* Mask upper 3 bits for P1 and P2 areas */
	    *physical = address & 0x1fffffff;
	} else if (address >= 0xfc000000) {
	    /*
	     * Mask upper 3 bits for control registers in P4 area,
	     * to unify access to control registers via P0-P3 area.
	     * The addresses for cache store queue, TLB address array
	     * are not masked.
	     */
	*physical = address & 0x1fffffff;
	} else {
	    /* access to cache store queue, or TLB address array. */
	    *physical = address;
	}
	*prot = PAGE_READ | PAGE_WRITE;
	return MMU_OK;
    }

    /* If MMU is disabled, return the corresponding physical page */
    if (!env->mmucr & MMUCR_AT) {
	*physical = address & 0x1FFFFFFF;
	*prot = PAGE_READ | PAGE_WRITE;
	return MMU_OK;
    }

    /* We need to resort to the MMU */
    return get_mmu_address(env, physical, prot, address, rw, access_type);
}

int cpu_sh4_handle_mmu_fault(CPUState * env, target_ulong address, int rw,
			     int mmu_idx, int is_softmmu)
{
    target_ulong physical, page_offset, page_size;
    int prot, ret, access_type;

    access_type = ACCESS_INT;
    ret =
	get_physical_address(env, &physical, &prot, address, rw,
			     access_type);

    if (ret != MMU_OK) {
	env->tea = address;
	switch (ret) {
	case MMU_ITLB_MISS:
	case MMU_DTLB_MISS_READ:
	    env->exception_index = 0x040;
	    break;
	case MMU_DTLB_MULTIPLE:
	case MMU_ITLB_MULTIPLE:
	    env->exception_index = 0x140;
	    break;
	case MMU_ITLB_VIOLATION:
	    env->exception_index = 0x0a0;
	    break;
	case MMU_DTLB_MISS_WRITE:
	    env->exception_index = 0x060;
	    break;
	case MMU_DTLB_INITIAL_WRITE:
	    env->exception_index = 0x080;
	    break;
	case MMU_DTLB_VIOLATION_READ:
	    env->exception_index = 0x0a0;
	    break;
	case MMU_DTLB_VIOLATION_WRITE:
	    env->exception_index = 0x0c0;
	    break;
	case MMU_IADDR_ERROR:
	case MMU_DADDR_ERROR_READ:
	    env->exception_index = 0x0c0;
	    break;
	case MMU_DADDR_ERROR_WRITE:
	    env->exception_index = 0x100;
	    break;
	default:
	    assert(0);
	}
	return 1;
    }

    page_size = TARGET_PAGE_SIZE;
    page_offset =
	(address - (address & TARGET_PAGE_MASK)) & ~(page_size - 1);
    address = (address & TARGET_PAGE_MASK) + page_offset;
    physical = (physical & TARGET_PAGE_MASK) + page_offset;

    return tlb_set_page(env, address, physical, prot, mmu_idx, is_softmmu);
}

target_phys_addr_t cpu_get_phys_page_debug(CPUState * env, target_ulong addr)
{
    target_ulong physical;
    int prot;

    get_physical_address(env, &physical, &prot, addr, 0, 0);
    return physical;
}

void cpu_load_tlb(CPUState * env)
{
    int n = cpu_mmucr_urc(env->mmucr);
    tlb_t * entry = &env->utlb[n];

    if (entry->v) {
        /* Overwriting valid entry in utlb. */
        target_ulong address = entry->vpn << 10;
	if (!same_tlb_entry_exists(env->itlb, ITLB_SIZE, entry)) {
	    tlb_flush_page(env, address);
	}
    }

    /* per utlb access cannot implemented. */
    increment_urc(env);

    /* Take values into cpu status from registers. */
    entry->asid = (uint8_t)cpu_pteh_asid(env->pteh);
    entry->vpn  = cpu_pteh_vpn(env->pteh);
    entry->v    = (uint8_t)cpu_ptel_v(env->ptel);
    entry->ppn  = cpu_ptel_ppn(env->ptel);
    entry->sz   = (uint8_t)cpu_ptel_sz(env->ptel);
    switch (entry->sz) {
    case 0: /* 00 */
        entry->size = 1024; /* 1K */
        break;
    case 1: /* 01 */
        entry->size = 1024 * 4; /* 4K */
        break;
    case 2: /* 10 */
        entry->size = 1024 * 64; /* 64K */
        break;
    case 3: /* 11 */
        entry->size = 1024 * 1024; /* 1M */
        break;
    default:
        assert(0);
        break;
    }
    entry->sh   = (uint8_t)cpu_ptel_sh(env->ptel);
    entry->c    = (uint8_t)cpu_ptel_c(env->ptel);
    entry->pr   = (uint8_t)cpu_ptel_pr(env->ptel);
    entry->d    = (uint8_t)cpu_ptel_d(env->ptel);
    entry->wt   = (uint8_t)cpu_ptel_wt(env->ptel);
    entry->sa   = (uint8_t)cpu_ptea_sa(env->ptea);
    entry->tc   = (uint8_t)cpu_ptea_tc(env->ptea);
}

void cpu_sh4_write_mmaped_utlb_addr(CPUSH4State *s, target_phys_addr_t addr,
				    uint32_t mem_value)
{
    int associate = addr & 0x0000080;
    uint32_t vpn = (mem_value & 0xfffffc00) >> 10;
    uint8_t d = (uint8_t)((mem_value & 0x00000200) >> 9);
    uint8_t v = (uint8_t)((mem_value & 0x00000100) >> 8);
    uint8_t asid = (uint8_t)(mem_value & 0x000000ff);

    if (associate) {
        int i;
	tlb_t * utlb_match_entry = NULL;
	int needs_tlb_flush = 0;

	/* search UTLB */
	for (i = 0; i < UTLB_SIZE; i++) {
            tlb_t * entry = &s->utlb[i];
            if (!entry->v)
	        continue;

            if (entry->vpn == vpn && entry->asid == asid) {
	        if (utlb_match_entry) {
		    /* Multiple TLB Exception */
		    s->exception_index = 0x140;
		    s->tea = addr;
		    break;
	        }
		if (entry->v && !v)
		    needs_tlb_flush = 1;
		entry->v = v;
		entry->d = d;
	        utlb_match_entry = entry;
	    }
	    increment_urc(s); /* per utlb access */
	}

	/* search ITLB */
	for (i = 0; i < ITLB_SIZE; i++) {
            tlb_t * entry = &s->itlb[i];
            if (entry->vpn == vpn && entry->asid == asid) {
	        if (entry->v && !v)
		    needs_tlb_flush = 1;
	        if (utlb_match_entry)
		    *entry = *utlb_match_entry;
	        else
		    entry->v = v;
		break;
	    }
	}

	if (needs_tlb_flush)
	    tlb_flush_page(s, vpn << 10);
        
    } else {
        int index = (addr & 0x00003f00) >> 8;
        tlb_t * entry = &s->utlb[index];
	if (entry->v) {
	    /* Overwriting valid entry in utlb. */
            target_ulong address = entry->vpn << 10;
	    if (!same_tlb_entry_exists(s->itlb, ITLB_SIZE, entry)) {
	        tlb_flush_page(s, address);
	    }
	}
	entry->asid = asid;
	entry->vpn = vpn;
	entry->d = d;
	entry->v = v;
	increment_urc(s);
    }
}

#endif
