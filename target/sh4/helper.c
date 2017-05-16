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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/log.h"
#include "sysemu/sysemu.h"

#if !defined(CONFIG_USER_ONLY)
#include "hw/sh4/sh_intc.h"
#endif

#if defined(CONFIG_USER_ONLY)

void superh_cpu_do_interrupt(CPUState *cs)
{
    cs->exception_index = -1;
}

int superh_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                                int mmu_idx)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    CPUSH4State *env = &cpu->env;

    env->tea = address;
    cs->exception_index = -1;
    switch (rw) {
    case 0:
        cs->exception_index = 0x0a0;
        break;
    case 1:
        cs->exception_index = 0x0c0;
        break;
    case 2:
        cs->exception_index = 0x0a0;
        break;
    }
    return 1;
}

int cpu_sh4_is_cached(CPUSH4State * env, target_ulong addr)
{
    /* For user mode, only U0 area is cacheable. */
    return !(addr & 0x80000000);
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

void superh_cpu_do_interrupt(CPUState *cs)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    CPUSH4State *env = &cpu->env;
    int do_irq = cs->interrupt_request & CPU_INTERRUPT_HARD;
    int do_exp, irq_vector = cs->exception_index;

    /* prioritize exceptions over interrupts */

    do_exp = cs->exception_index != -1;
    do_irq = do_irq && (cs->exception_index == -1);

    if (env->sr & (1u << SR_BL)) {
        if (do_exp && cs->exception_index != 0x1e0) {
            /* In theory a masked exception generates a reset exception,
               which in turn jumps to the reset vector. However this only
               works when using a bootloader. When using a kernel and an
               initrd, they need to be reloaded and the program counter
               should be loaded with the kernel entry point.
               qemu_system_reset_request takes care of that.  */
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return;
        }
        if (do_irq && !env->in_sleep) {
            return; /* masked */
        }
    }
    env->in_sleep = 0;

    if (do_irq) {
        irq_vector = sh_intc_get_pending_vector(env->intc_handle,
						(env->sr >> 4) & 0xf);
        if (irq_vector == -1) {
            return; /* masked */
	}
    }

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
	const char *expname;
        switch (cs->exception_index) {
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
	qemu_log("exception 0x%03x [%s] raised\n",
		  irq_vector, expname);
        log_cpu_state(cs, 0);
    }

    env->ssr = cpu_read_sr(env);
    env->spc = env->pc;
    env->sgr = env->gregs[15];
    env->sr |= (1u << SR_BL) | (1u << SR_MD) | (1u << SR_RB);

    if (env->flags & DELAY_SLOT_MASK) {
        /* Branch instruction should be executed again before delay slot. */
	env->spc -= 2;
	/* Clear flags for exception/interrupt routine. */
        env->flags &= ~DELAY_SLOT_MASK;
    }

    if (do_exp) {
        env->expevt = cs->exception_index;
        switch (cs->exception_index) {
        case 0x000:
        case 0x020:
        case 0x140:
            env->sr &= ~(1u << SR_FD);
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

static void update_itlb_use(CPUSH4State * env, int itlbnb)
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

static int itlb_replacement(CPUSH4State * env)
{
    SuperHCPU *cpu = sh_env_get_cpu(env);

    if ((env->mmucr & 0xe0000000) == 0xe0000000) {
	return 0;
    }
    if ((env->mmucr & 0x98000000) == 0x18000000) {
	return 1;
    }
    if ((env->mmucr & 0x54000000) == 0x04000000) {
	return 2;
    }
    if ((env->mmucr & 0x2c000000) == 0x00000000) {
	return 3;
    }
    cpu_abort(CPU(cpu), "Unhandled itlb_replacement");
}

/* Find the corresponding entry in the right TLB
   Return entry, MMU_DTLB_MISS or MMU_DTLB_MULTIPLE
*/
static int find_tlb_entry(CPUSH4State * env, target_ulong address,
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
	if (!entries[i].sh && use_asid && entries[i].asid != asid)
	    continue;		/* Bad ASID */
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

static void increment_urc(CPUSH4State * env)
{
    uint8_t urb, urc;

    /* Increment URC */
    urb = ((env->mmucr) >> 18) & 0x3f;
    urc = ((env->mmucr) >> 10) & 0x3f;
    urc++;
    if ((urb > 0 && urc > urb) || urc > (UTLB_SIZE - 1))
	urc = 0;
    env->mmucr = (env->mmucr & 0xffff03ff) | (urc << 10);
}

/* Copy and utlb entry into itlb
   Return entry
*/
static int copy_utlb_entry_itlb(CPUSH4State *env, int utlb)
{
    int itlb;

    tlb_t * ientry;
    itlb = itlb_replacement(env);
    ientry = &env->itlb[itlb];
    if (ientry->v) {
        tlb_flush_page(CPU(sh_env_get_cpu(env)), ientry->vpn << 10);
    }
    *ientry = env->utlb[utlb];
    update_itlb_use(env, itlb);
    return itlb;
}

/* Find itlb entry
   Return entry, MMU_ITLB_MISS, MMU_ITLB_MULTIPLE or MMU_DTLB_MULTIPLE
*/
static int find_itlb_entry(CPUSH4State * env, target_ulong address,
                           int use_asid)
{
    int e;

    e = find_tlb_entry(env, address, env->itlb, ITLB_SIZE, use_asid);
    if (e == MMU_DTLB_MULTIPLE) {
	e = MMU_ITLB_MULTIPLE;
    } else if (e == MMU_DTLB_MISS) {
	e = MMU_ITLB_MISS;
    } else if (e >= 0) {
	update_itlb_use(env, e);
    }
    return e;
}

/* Find utlb entry
   Return entry, MMU_DTLB_MISS, MMU_DTLB_MULTIPLE */
static int find_utlb_entry(CPUSH4State * env, target_ulong address, int use_asid)
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
static int get_mmu_address(CPUSH4State * env, target_ulong * physical,
			   int *prot, target_ulong address,
			   int rw, int access_type)
{
    int use_asid, n;
    tlb_t *matching = NULL;

    use_asid = !(env->mmucr & MMUCR_SV) || !(env->sr & (1u << SR_MD));

    if (rw == 2) {
        n = find_itlb_entry(env, address, use_asid);
	if (n >= 0) {
	    matching = &env->itlb[n];
            if (!(env->sr & (1u << SR_MD)) && !(matching->pr & 2)) {
		n = MMU_ITLB_VIOLATION;
            } else {
		*prot = PAGE_EXEC;
            }
        } else {
            n = find_utlb_entry(env, address, use_asid);
            if (n >= 0) {
                n = copy_utlb_entry_itlb(env, n);
                matching = &env->itlb[n];
                if (!(env->sr & (1u << SR_MD)) && !(matching->pr & 2)) {
                    n = MMU_ITLB_VIOLATION;
                } else {
                    *prot = PAGE_READ | PAGE_EXEC;
                    if ((matching->pr & 1) && matching->d) {
                        *prot |= PAGE_WRITE;
                    }
                }
            } else if (n == MMU_DTLB_MULTIPLE) {
                n = MMU_ITLB_MULTIPLE;
            } else if (n == MMU_DTLB_MISS) {
                n = MMU_ITLB_MISS;
            }
	}
    } else {
	n = find_utlb_entry(env, address, use_asid);
	if (n >= 0) {
	    matching = &env->utlb[n];
            if (!(env->sr & (1u << SR_MD)) && !(matching->pr & 2)) {
                n = (rw == 1) ? MMU_DTLB_VIOLATION_WRITE :
                    MMU_DTLB_VIOLATION_READ;
            } else if ((rw == 1) && !(matching->pr & 1)) {
                n = MMU_DTLB_VIOLATION_WRITE;
            } else if ((rw == 1) && !matching->d) {
                n = MMU_DTLB_INITIAL_WRITE;
            } else {
                *prot = PAGE_READ;
                if ((matching->pr & 1) && matching->d) {
                    *prot |= PAGE_WRITE;
                }
            }
	} else if (n == MMU_DTLB_MISS) {
	    n = (rw == 1) ? MMU_DTLB_MISS_WRITE :
		MMU_DTLB_MISS_READ;
	}
    }
    if (n >= 0) {
	n = MMU_OK;
	*physical = ((matching->ppn << 10) & ~(matching->size - 1)) |
	    (address & (matching->size - 1));
    }
    return n;
}

static int get_physical_address(CPUSH4State * env, target_ulong * physical,
                                int *prot, target_ulong address,
                                int rw, int access_type)
{
    /* P1, P2 and P4 areas do not use translation */
    if ((address >= 0x80000000 && address < 0xc0000000) ||
	address >= 0xe0000000) {
        if (!(env->sr & (1u << SR_MD))
	    && (address < 0xe0000000 || address >= 0xe4000000)) {
	    /* Unauthorized access in user mode (only store queues are available) */
            qemu_log_mask(LOG_GUEST_ERROR, "Unauthorized access\n");
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
	} else {
	    *physical = address;
	}
	*prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
	return MMU_OK;
    }

    /* If MMU is disabled, return the corresponding physical page */
    if (!(env->mmucr & MMUCR_AT)) {
	*physical = address & 0x1FFFFFFF;
	*prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
	return MMU_OK;
    }

    /* We need to resort to the MMU */
    return get_mmu_address(env, physical, prot, address, rw, access_type);
}

int superh_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw,
                                int mmu_idx)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    CPUSH4State *env = &cpu->env;
    target_ulong physical;
    int prot, ret, access_type;

    access_type = ACCESS_INT;
    ret =
	get_physical_address(env, &physical, &prot, address, rw,
			     access_type);

    if (ret != MMU_OK) {
	env->tea = address;
	if (ret != MMU_DTLB_MULTIPLE && ret != MMU_ITLB_MULTIPLE) {
	    env->pteh = (env->pteh & PTEH_ASID_MASK) |
		    (address & PTEH_VPN_MASK);
	}
	switch (ret) {
	case MMU_ITLB_MISS:
	case MMU_DTLB_MISS_READ:
            cs->exception_index = 0x040;
	    break;
	case MMU_DTLB_MULTIPLE:
	case MMU_ITLB_MULTIPLE:
            cs->exception_index = 0x140;
	    break;
	case MMU_ITLB_VIOLATION:
            cs->exception_index = 0x0a0;
	    break;
	case MMU_DTLB_MISS_WRITE:
            cs->exception_index = 0x060;
	    break;
	case MMU_DTLB_INITIAL_WRITE:
            cs->exception_index = 0x080;
	    break;
	case MMU_DTLB_VIOLATION_READ:
            cs->exception_index = 0x0a0;
	    break;
	case MMU_DTLB_VIOLATION_WRITE:
            cs->exception_index = 0x0c0;
	    break;
	case MMU_IADDR_ERROR:
	case MMU_DADDR_ERROR_READ:
            cs->exception_index = 0x0e0;
	    break;
	case MMU_DADDR_ERROR_WRITE:
            cs->exception_index = 0x100;
	    break;
	default:
            cpu_abort(cs, "Unhandled MMU fault");
	}
	return 1;
    }

    address &= TARGET_PAGE_MASK;
    physical &= TARGET_PAGE_MASK;

    tlb_set_page(cs, address, physical, prot, mmu_idx, TARGET_PAGE_SIZE);
    return 0;
}

hwaddr superh_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);
    target_ulong physical;
    int prot;

    get_physical_address(&cpu->env, &physical, &prot, addr, 0, 0);
    return physical;
}

void cpu_load_tlb(CPUSH4State * env)
{
    SuperHCPU *cpu = sh_env_get_cpu(env);
    int n = cpu_mmucr_urc(env->mmucr);
    tlb_t * entry = &env->utlb[n];

    if (entry->v) {
        /* Overwriting valid entry in utlb. */
        target_ulong address = entry->vpn << 10;
        tlb_flush_page(CPU(cpu), address);
    }

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
        cpu_abort(CPU(cpu), "Unhandled load_tlb");
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

 void cpu_sh4_invalidate_tlb(CPUSH4State *s)
{
    int i;

    /* UTLB */
    for (i = 0; i < UTLB_SIZE; i++) {
        tlb_t * entry = &s->utlb[i];
        entry->v = 0;
    }
    /* ITLB */
    for (i = 0; i < ITLB_SIZE; i++) {
        tlb_t * entry = &s->itlb[i];
        entry->v = 0;
    }

    tlb_flush(CPU(sh_env_get_cpu(s)));
}

uint32_t cpu_sh4_read_mmaped_itlb_addr(CPUSH4State *s,
                                       hwaddr addr)
{
    int index = (addr & 0x00000300) >> 8;
    tlb_t * entry = &s->itlb[index];

    return (entry->vpn  << 10) |
           (entry->v    <<  8) |
           (entry->asid);
}

void cpu_sh4_write_mmaped_itlb_addr(CPUSH4State *s, hwaddr addr,
				    uint32_t mem_value)
{
    uint32_t vpn = (mem_value & 0xfffffc00) >> 10;
    uint8_t v = (uint8_t)((mem_value & 0x00000100) >> 8);
    uint8_t asid = (uint8_t)(mem_value & 0x000000ff);

    int index = (addr & 0x00000300) >> 8;
    tlb_t * entry = &s->itlb[index];
    if (entry->v) {
        /* Overwriting valid entry in itlb. */
        target_ulong address = entry->vpn << 10;
        tlb_flush_page(CPU(sh_env_get_cpu(s)), address);
    }
    entry->asid = asid;
    entry->vpn = vpn;
    entry->v = v;
}

uint32_t cpu_sh4_read_mmaped_itlb_data(CPUSH4State *s,
                                       hwaddr addr)
{
    int array = (addr & 0x00800000) >> 23;
    int index = (addr & 0x00000300) >> 8;
    tlb_t * entry = &s->itlb[index];

    if (array == 0) {
        /* ITLB Data Array 1 */
        return (entry->ppn << 10) |
               (entry->v   <<  8) |
               (entry->pr  <<  5) |
               ((entry->sz & 1) <<  6) |
               ((entry->sz & 2) <<  4) |
               (entry->c   <<  3) |
               (entry->sh  <<  1);
    } else {
        /* ITLB Data Array 2 */
        return (entry->tc << 1) |
               (entry->sa);
    }
}

void cpu_sh4_write_mmaped_itlb_data(CPUSH4State *s, hwaddr addr,
                                    uint32_t mem_value)
{
    int array = (addr & 0x00800000) >> 23;
    int index = (addr & 0x00000300) >> 8;
    tlb_t * entry = &s->itlb[index];

    if (array == 0) {
        /* ITLB Data Array 1 */
        if (entry->v) {
            /* Overwriting valid entry in utlb. */
            target_ulong address = entry->vpn << 10;
            tlb_flush_page(CPU(sh_env_get_cpu(s)), address);
        }
        entry->ppn = (mem_value & 0x1ffffc00) >> 10;
        entry->v   = (mem_value & 0x00000100) >> 8;
        entry->sz  = (mem_value & 0x00000080) >> 6 |
                     (mem_value & 0x00000010) >> 4;
        entry->pr  = (mem_value & 0x00000040) >> 5;
        entry->c   = (mem_value & 0x00000008) >> 3;
        entry->sh  = (mem_value & 0x00000002) >> 1;
    } else {
        /* ITLB Data Array 2 */
        entry->tc  = (mem_value & 0x00000008) >> 3;
        entry->sa  = (mem_value & 0x00000007);
    }
}

uint32_t cpu_sh4_read_mmaped_utlb_addr(CPUSH4State *s,
                                       hwaddr addr)
{
    int index = (addr & 0x00003f00) >> 8;
    tlb_t * entry = &s->utlb[index];

    increment_urc(s); /* per utlb access */

    return (entry->vpn  << 10) |
           (entry->v    <<  8) |
           (entry->asid);
}

void cpu_sh4_write_mmaped_utlb_addr(CPUSH4State *s, hwaddr addr,
				    uint32_t mem_value)
{
    int associate = addr & 0x0000080;
    uint32_t vpn = (mem_value & 0xfffffc00) >> 10;
    uint8_t d = (uint8_t)((mem_value & 0x00000200) >> 9);
    uint8_t v = (uint8_t)((mem_value & 0x00000100) >> 8);
    uint8_t asid = (uint8_t)(mem_value & 0x000000ff);
    int use_asid = !(s->mmucr & MMUCR_SV) || !(s->sr & (1u << SR_MD));

    if (associate) {
        int i;
	tlb_t * utlb_match_entry = NULL;
	int needs_tlb_flush = 0;

	/* search UTLB */
	for (i = 0; i < UTLB_SIZE; i++) {
            tlb_t * entry = &s->utlb[i];
            if (!entry->v)
	        continue;

            if (entry->vpn == vpn
                && (!use_asid || entry->asid == asid || entry->sh)) {
	        if (utlb_match_entry) {
                    CPUState *cs = CPU(sh_env_get_cpu(s));

		    /* Multiple TLB Exception */
                    cs->exception_index = 0x140;
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
            if (entry->vpn == vpn
                && (!use_asid || entry->asid == asid || entry->sh)) {
	        if (entry->v && !v)
		    needs_tlb_flush = 1;
	        if (utlb_match_entry)
		    *entry = *utlb_match_entry;
	        else
		    entry->v = v;
		break;
	    }
	}

        if (needs_tlb_flush) {
            tlb_flush_page(CPU(sh_env_get_cpu(s)), vpn << 10);
        }
        
    } else {
        int index = (addr & 0x00003f00) >> 8;
        tlb_t * entry = &s->utlb[index];
	if (entry->v) {
            CPUState *cs = CPU(sh_env_get_cpu(s));

	    /* Overwriting valid entry in utlb. */
            target_ulong address = entry->vpn << 10;
            tlb_flush_page(cs, address);
	}
	entry->asid = asid;
	entry->vpn = vpn;
	entry->d = d;
	entry->v = v;
	increment_urc(s);
    }
}

uint32_t cpu_sh4_read_mmaped_utlb_data(CPUSH4State *s,
                                       hwaddr addr)
{
    int array = (addr & 0x00800000) >> 23;
    int index = (addr & 0x00003f00) >> 8;
    tlb_t * entry = &s->utlb[index];

    increment_urc(s); /* per utlb access */

    if (array == 0) {
        /* ITLB Data Array 1 */
        return (entry->ppn << 10) |
               (entry->v   <<  8) |
               (entry->pr  <<  5) |
               ((entry->sz & 1) <<  6) |
               ((entry->sz & 2) <<  4) |
               (entry->c   <<  3) |
               (entry->d   <<  2) |
               (entry->sh  <<  1) |
               (entry->wt);
    } else {
        /* ITLB Data Array 2 */
        return (entry->tc << 1) |
               (entry->sa);
    }
}

void cpu_sh4_write_mmaped_utlb_data(CPUSH4State *s, hwaddr addr,
                                    uint32_t mem_value)
{
    int array = (addr & 0x00800000) >> 23;
    int index = (addr & 0x00003f00) >> 8;
    tlb_t * entry = &s->utlb[index];

    increment_urc(s); /* per utlb access */

    if (array == 0) {
        /* UTLB Data Array 1 */
        if (entry->v) {
            /* Overwriting valid entry in utlb. */
            target_ulong address = entry->vpn << 10;
            tlb_flush_page(CPU(sh_env_get_cpu(s)), address);
        }
        entry->ppn = (mem_value & 0x1ffffc00) >> 10;
        entry->v   = (mem_value & 0x00000100) >> 8;
        entry->sz  = (mem_value & 0x00000080) >> 6 |
                     (mem_value & 0x00000010) >> 4;
        entry->pr  = (mem_value & 0x00000060) >> 5;
        entry->c   = (mem_value & 0x00000008) >> 3;
        entry->d   = (mem_value & 0x00000004) >> 2;
        entry->sh  = (mem_value & 0x00000002) >> 1;
        entry->wt  = (mem_value & 0x00000001);
    } else {
        /* UTLB Data Array 2 */
        entry->tc = (mem_value & 0x00000008) >> 3;
        entry->sa = (mem_value & 0x00000007);
    }
}

int cpu_sh4_is_cached(CPUSH4State * env, target_ulong addr)
{
    int n;
    int use_asid = !(env->mmucr & MMUCR_SV) || !(env->sr & (1u << SR_MD));

    /* check area */
    if (env->sr & (1u << SR_MD)) {
        /* For privileged mode, P2 and P4 area is not cacheable. */
        if ((0xA0000000 <= addr && addr < 0xC0000000) || 0xE0000000 <= addr)
            return 0;
    } else {
        /* For user mode, only U0 area is cacheable. */
        if (0x80000000 <= addr)
            return 0;
    }

    /*
     * TODO : Evaluate CCR and check if the cache is on or off.
     *        Now CCR is not in CPUSH4State, but in SH7750State.
     *        When you move the ccr into CPUSH4State, the code will be
     *        as follows.
     */
#if 0
    /* check if operand cache is enabled or not. */
    if (!(env->ccr & 1))
        return 0;
#endif

    /* if MMU is off, no check for TLB. */
    if (env->mmucr & MMUCR_AT)
        return 1;

    /* check TLB */
    n = find_tlb_entry(env, addr, env->itlb, ITLB_SIZE, use_asid);
    if (n >= 0)
        return env->itlb[n].c;

    n = find_tlb_entry(env, addr, env->utlb, UTLB_SIZE, use_asid);
    if (n >= 0)
        return env->utlb[n].c;

    return 0;
}

#endif

bool superh_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        SuperHCPU *cpu = SUPERH_CPU(cs);
        CPUSH4State *env = &cpu->env;

        /* Delay slots are indivisible, ignore interrupts */
        if (env->flags & DELAY_SLOT_MASK) {
            return false;
        } else {
            superh_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}
