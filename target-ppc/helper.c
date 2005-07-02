/*
 *  PowerPC emulation helpers for qemu.
 * 
 *  Copyright (c) 2003-2005 Jocelyn Mayer
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

//#define DEBUG_MMU
//#define DEBUG_BATS
//#define DEBUG_EXCEPTIONS
/* accurate but slower TLB flush in exceptions */
//#define ACCURATE_TLB_FLUSH

/*****************************************************************************/
/* PowerPC MMU emulation */

/* Perform BAT hit & translation */
static int get_bat (CPUState *env, uint32_t *real, int *prot,
                    uint32_t virtual, int rw, int type)
{
    uint32_t *BATlt, *BATut, *BATu, *BATl;
    uint32_t base, BEPIl, BEPIu, bl;
    int i;
    int ret = -1;

#if defined (DEBUG_BATS)
    if (loglevel > 0) {
        fprintf(logfile, "%s: %cBAT v 0x%08x\n", __func__,
               type == ACCESS_CODE ? 'I' : 'D', virtual);
    }
#endif
    switch (type) {
    case ACCESS_CODE:
        BATlt = env->IBAT[1];
        BATut = env->IBAT[0];
        break;
    default:
        BATlt = env->DBAT[1];
        BATut = env->DBAT[0];
        break;
    }
#if defined (DEBUG_BATS)
    if (loglevel > 0) {
        fprintf(logfile, "%s...: %cBAT v 0x%08x\n", __func__,
               type == ACCESS_CODE ? 'I' : 'D', virtual);
    }
#endif
    base = virtual & 0xFFFC0000;
    for (i = 0; i < 4; i++) {
        BATu = &BATut[i];
        BATl = &BATlt[i];
        BEPIu = *BATu & 0xF0000000;
        BEPIl = *BATu & 0x0FFE0000;
        bl = (*BATu & 0x00001FFC) << 15;
#if defined (DEBUG_BATS)
        if (loglevel > 0) {
            fprintf(logfile, "%s: %cBAT%d v 0x%08x BATu 0x%08x BATl 0x%08x\n",
                    __func__, type == ACCESS_CODE ? 'I' : 'D', i, virtual,
                    *BATu, *BATl);
        }
#endif
        if ((virtual & 0xF0000000) == BEPIu &&
            ((virtual & 0x0FFE0000) & ~bl) == BEPIl) {
            /* BAT matches */
            if ((msr_pr == 0 && (*BATu & 0x00000002)) ||
                (msr_pr == 1 && (*BATu & 0x00000001))) {
                /* Get physical address */
                *real = (*BATl & 0xF0000000) |
                    ((virtual & 0x0FFE0000 & bl) | (*BATl & 0x0FFE0000)) |
                    (virtual & 0x0001F000);
                if (*BATl & 0x00000001)
                    *prot = PAGE_READ;
                if (*BATl & 0x00000002)
                    *prot = PAGE_WRITE | PAGE_READ;
#if defined (DEBUG_BATS)
                if (loglevel > 0) {
                    fprintf(logfile, "BAT %d match: r 0x%08x prot=%c%c\n",
                            i, *real, *prot & PAGE_READ ? 'R' : '-',
                            *prot & PAGE_WRITE ? 'W' : '-');
                }
#endif
                ret = 0;
                break;
            }
        }
    }
    if (ret < 0) {
#if defined (DEBUG_BATS)
        printf("no BAT match for 0x%08x:\n", virtual);
        for (i = 0; i < 4; i++) {
            BATu = &BATut[i];
            BATl = &BATlt[i];
            BEPIu = *BATu & 0xF0000000;
            BEPIl = *BATu & 0x0FFE0000;
            bl = (*BATu & 0x00001FFC) << 15;
            printf("%s: %cBAT%d v 0x%08x BATu 0x%08x BATl 0x%08x \n\t"
                   "0x%08x 0x%08x 0x%08x\n",
                   __func__, type == ACCESS_CODE ? 'I' : 'D', i, virtual,
                   *BATu, *BATl, BEPIu, BEPIl, bl);
        }
#endif
    }
    /* No hit */
    return ret;
}

/* PTE table lookup */
static int find_pte (uint32_t *RPN, int *prot, uint32_t base, uint32_t va,
                     int h, int key, int rw)
{
    uint32_t pte0, pte1, keep = 0, access = 0;
    int i, good = -1, store = 0;
    int ret = -1; /* No entry found */

    for (i = 0; i < 8; i++) {
        pte0 = ldl_phys(base + (i * 8));
        pte1 =  ldl_phys(base + (i * 8) + 4);
#if defined (DEBUG_MMU)
        if (loglevel > 0) {
	    fprintf(logfile, "Load pte from 0x%08x => 0x%08x 0x%08x "
		    "%d %d %d 0x%08x\n", base + (i * 8), pte0, pte1,
		    pte0 >> 31, h, (pte0 >> 6) & 1, va);
	}
#endif
        /* Check validity and table match */
        if (pte0 & 0x80000000 && (h == ((pte0 >> 6) & 1))) {
            /* Check vsid & api */
            if ((pte0 & 0x7FFFFFBF) == va) {
                if (good == -1) {
                    good = i;
                    keep = pte1;
                } else {
                    /* All matches should have equal RPN, WIMG & PP */
                    if ((keep & 0xFFFFF07B) != (pte1 & 0xFFFFF07B)) {
			if (loglevel > 0)
			    fprintf(logfile, "Bad RPN/WIMG/PP\n");
                        return -1;
                    }
                }
                /* Check access rights */
                if (key == 0) {
                    access = PAGE_READ;
                    if ((pte1 & 0x00000003) != 0x3)
                        access |= PAGE_WRITE;
                } else {
                    switch (pte1 & 0x00000003) {
                    case 0x0:
                        access = 0;
                        break;
                    case 0x1:
                    case 0x3:
                        access = PAGE_READ;
                        break;
                    case 0x2:
                        access = PAGE_READ | PAGE_WRITE;
                        break;
                    }
                }
                if (ret < 0) {
		    if ((rw == 0 && (access & PAGE_READ)) ||
			(rw == 1 && (access & PAGE_WRITE))) {
#if defined (DEBUG_MMU)
			if (loglevel > 0)
			    fprintf(logfile, "PTE access granted !\n");
#endif
                        good = i;
                        keep = pte1;
                        ret = 0;
		    } else {
			/* Access right violation */
                        ret = -2;
#if defined (DEBUG_MMU)
			if (loglevel > 0)
			    fprintf(logfile, "PTE access rejected\n");
#endif
                    }
		    *prot = access;
		}
            }
        }
    }
    if (good != -1) {
        *RPN = keep & 0xFFFFF000;
#if defined (DEBUG_MMU)
        if (loglevel > 0) {
	    fprintf(logfile, "found PTE at addr 0x%08x prot=0x%01x ret=%d\n",
               *RPN, *prot, ret);
	}
#endif
        /* Update page flags */
        if (!(keep & 0x00000100)) {
	    /* Access flag */
            keep |= 0x00000100;
            store = 1;
        }
        if (!(keep & 0x00000080)) {
	    if (rw && ret == 0) {
		/* Change flag */
                keep |= 0x00000080;
                store = 1;
	    } else {
		/* Force page fault for first write access */
		*prot &= ~PAGE_WRITE;
            }
        }
        if (store) {
	    stl_phys_notdirty(base + (good * 8) + 4, keep);
	}
    }

    return ret;
}

static inline uint32_t get_pgaddr (uint32_t sdr1, uint32_t hash, uint32_t mask)
{
    return (sdr1 & 0xFFFF0000) | (hash & mask);
}

/* Perform segment based translation */
static int get_segment (CPUState *env, uint32_t *real, int *prot,
                        uint32_t virtual, int rw, int type)
{
    uint32_t pg_addr, sdr, ptem, vsid, pgidx;
    uint32_t hash, mask;
    uint32_t sr;
    int key;
    int ret = -1, ret2;

    sr = env->sr[virtual >> 28];
#if defined (DEBUG_MMU)
    if (loglevel > 0) {
	fprintf(logfile, "Check segment v=0x%08x %d 0x%08x nip=0x%08x "
		"lr=0x%08x ir=%d dr=%d pr=%d %d t=%d\n",
		virtual, virtual >> 28, sr, env->nip,
		env->lr, msr_ir, msr_dr, msr_pr, rw, type);
    }
#endif
    key = (((sr & 0x20000000) && msr_pr == 1) ||
        ((sr & 0x40000000) && msr_pr == 0)) ? 1 : 0;
    if ((sr & 0x80000000) == 0) {
#if defined (DEBUG_MMU)
    if (loglevel > 0) 
	    fprintf(logfile, "pte segment: key=%d n=0x%08x\n",
		    key, sr & 0x10000000);
#endif
        /* Check if instruction fetch is allowed, if needed */
        if (type != ACCESS_CODE || (sr & 0x10000000) == 0) {
            /* Page address translation */
            vsid = sr & 0x00FFFFFF;
            pgidx = (virtual >> 12) & 0xFFFF;
            sdr = env->sdr1;
            hash = ((vsid ^ pgidx) & 0x0007FFFF) << 6;
            mask = ((sdr & 0x000001FF) << 16) | 0xFFC0;
            pg_addr = get_pgaddr(sdr, hash, mask);
            ptem = (vsid << 7) | (pgidx >> 10);
#if defined (DEBUG_MMU)
	    if (loglevel > 0) {
		fprintf(logfile, "0 sdr1=0x%08x vsid=0x%06x api=0x%04x "
			"hash=0x%07x pg_addr=0x%08x\n", sdr, vsid, pgidx, hash,
			pg_addr);
	    }
#endif
            /* Primary table lookup */
            ret = find_pte(real, prot, pg_addr, ptem, 0, key, rw);
            if (ret < 0) {
                /* Secondary table lookup */
                hash = (~hash) & 0x01FFFFC0;
                pg_addr = get_pgaddr(sdr, hash, mask);
#if defined (DEBUG_MMU)
		if (virtual != 0xEFFFFFFF && loglevel > 0) {
		    fprintf(logfile, "1 sdr1=0x%08x vsid=0x%06x api=0x%04x "
			    "hash=0x%05x pg_addr=0x%08x\n", sdr, vsid, pgidx,
			    hash, pg_addr);
		}
#endif
                ret2 = find_pte(real, prot, pg_addr, ptem, 1, key, rw);
                if (ret2 != -1)
                    ret = ret2;
            }
        } else {
#if defined (DEBUG_MMU)
	    if (loglevel > 0)
		fprintf(logfile, "No access allowed\n");
#endif
	    ret = -3;
        }
    } else {
#if defined (DEBUG_MMU)
        if (loglevel > 0)
	    fprintf(logfile, "direct store...\n");
#endif
        /* Direct-store segment : absolutely *BUGGY* for now */
        switch (type) {
        case ACCESS_INT:
            /* Integer load/store : only access allowed */
            break;
        case ACCESS_CODE:
            /* No code fetch is allowed in direct-store areas */
            return -4;
        case ACCESS_FLOAT:
            /* Floating point load/store */
            return -4;
        case ACCESS_RES:
            /* lwarx, ldarx or srwcx. */
            return -4;
        case ACCESS_CACHE:
            /* dcba, dcbt, dcbtst, dcbf, dcbi, dcbst, dcbz, or icbi */
            /* Should make the instruction do no-op.
             * As it already do no-op, it's quite easy :-)
             */
            *real = virtual;
            return 0;
        case ACCESS_EXT:
            /* eciwx or ecowx */
            return -4;
        default:
            if (logfile) {
                fprintf(logfile, "ERROR: instruction should not need "
                        "address translation\n");
            }
            printf("ERROR: instruction should not need "
                   "address translation\n");
            return -4;
        }
        if ((rw == 1 || key != 1) && (rw == 0 || key != 0)) {
            *real = virtual;
            ret = 2;
        } else {
            ret = -2;
        }
    }

    return ret;
}

int get_physical_address (CPUState *env, uint32_t *physical, int *prot,
                          uint32_t address, int rw, int access_type)
{
    int ret;
#if 0
    if (loglevel > 0) {
        fprintf(logfile, "%s\n", __func__);
    }
#endif    
    if ((access_type == ACCESS_CODE && msr_ir == 0) ||
        (access_type != ACCESS_CODE && msr_dr == 0)) {
        /* No address translation */
        *physical = address & ~0xFFF;
        *prot = PAGE_READ | PAGE_WRITE;
        ret = 0;
    } else {
        /* Try to find a BAT */
        ret = get_bat(env, physical, prot, address, rw, access_type);
        if (ret < 0) {
            /* We didn't match any BAT entry */
            ret = get_segment(env, physical, prot, address, rw, access_type);
        }
    }
#if 0
    if (loglevel > 0) {
        fprintf(logfile, "%s address %08x => %08x\n",
		__func__, address, *physical);
    }
#endif    
    return ret;
}

#if defined(CONFIG_USER_ONLY) 
target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}
#else
target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    uint32_t phys_addr;
    int prot;

    if (get_physical_address(env, &phys_addr, &prot, addr, 0, ACCESS_INT) != 0)
        return -1;
    return phys_addr;
}
#endif

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
void tlb_fill(target_ulong addr, int is_write, int is_user, void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
#if 0
    {
        unsigned long tlb_addrr, tlb_addrw;
        int index;
        index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addrr = env->tlb_read[is_user][index].address;
        tlb_addrw = env->tlb_write[is_user][index].address;
        if (loglevel) {
            fprintf(logfile,
                    "%s 1 %p %p idx=%d addr=0x%08lx tbl_addr=0x%08lx 0x%08lx "
               "(0x%08lx 0x%08lx)\n", __func__, env,
               &env->tlb_read[is_user][index], index, addr,
               tlb_addrr, tlb_addrw, addr & TARGET_PAGE_MASK,
               tlb_addrr & (TARGET_PAGE_MASK | TLB_INVALID_MASK));
        }
    }
#endif
    ret = cpu_ppc_handle_mmu_fault(env, addr, is_write, is_user, 1);
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
        do_raise_exception_err(env->exception_index, env->error_code);
    }
#if 0
    {
        unsigned long tlb_addrr, tlb_addrw;
        int index;
        index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addrr = env->tlb_read[is_user][index].address;
        tlb_addrw = env->tlb_write[is_user][index].address;
        printf("%s 2 %p %p idx=%d addr=0x%08lx tbl_addr=0x%08lx 0x%08lx "
               "(0x%08lx 0x%08lx)\n", __func__, env,
               &env->tlb_read[is_user][index], index, addr,
               tlb_addrr, tlb_addrw, addr & TARGET_PAGE_MASK,
               tlb_addrr & (TARGET_PAGE_MASK | TLB_INVALID_MASK));
    }
#endif
    env = saved_env;
}

void cpu_ppc_init_mmu(CPUState *env)
{
    /* Nothing to do: all translation are disabled */
}
#endif

/* Perform address translation */
int cpu_ppc_handle_mmu_fault (CPUState *env, uint32_t address, int rw,
                              int is_user, int is_softmmu)
{
    uint32_t physical;
    int prot;
    int exception = 0, error_code = 0;
    int access_type;
    int ret = 0;

    if (rw == 2) {
        /* code access */
        rw = 0;
        access_type = ACCESS_CODE;
    } else {
        /* data access */
        /* XXX: put correct access by using cpu_restore_state()
           correctly */
        access_type = ACCESS_INT;
        //        access_type = env->access_type;
    }
    if (env->user_mode_only) {
        /* user mode only emulation */
        ret = -2;
        goto do_fault;
    }
    ret = get_physical_address(env, &physical, &prot,
                               address, rw, access_type);
    if (ret == 0) {
	ret = tlb_set_page(env, address & ~0xFFF, physical, prot,
			   is_user, is_softmmu);
    } else if (ret < 0) {
    do_fault:
#if defined (DEBUG_MMU)
	if (loglevel > 0)
	    cpu_dump_state(env, logfile, fprintf, 0);
#endif
        if (access_type == ACCESS_CODE) {
            exception = EXCP_ISI;
            switch (ret) {
            case -1:
                /* No matches in page tables */
                error_code = EXCP_ISI_TRANSLATE;
                break;
            case -2:
                /* Access rights violation */
                error_code = EXCP_ISI_PROT;
                break;
            case -3:
		/* No execute protection violation */
                error_code = EXCP_ISI_NOEXEC;
                break;
            case -4:
                /* Direct store exception */
                /* No code fetch is allowed in direct-store areas */
                error_code = EXCP_ISI_DIRECT;
                break;
            }
        } else {
            exception = EXCP_DSI;
            switch (ret) {
            case -1:
                /* No matches in page tables */
                error_code = EXCP_DSI_TRANSLATE;
                break;
            case -2:
                /* Access rights violation */
                error_code = EXCP_DSI_PROT;
                break;
            case -4:
                /* Direct store exception */
                switch (access_type) {
                case ACCESS_FLOAT:
                    /* Floating point load/store */
                    exception = EXCP_ALIGN;
                    error_code = EXCP_ALIGN_FP;
                    break;
                case ACCESS_RES:
                    /* lwarx, ldarx or srwcx. */
                    exception = EXCP_DSI;
                    error_code = EXCP_DSI_NOTSUP | EXCP_DSI_DIRECT;
                    break;
                case ACCESS_EXT:
                    /* eciwx or ecowx */
                    exception = EXCP_DSI;
                    error_code = EXCP_DSI_NOTSUP | EXCP_DSI_DIRECT |
			EXCP_DSI_ECXW;
                    break;
                default:
		    printf("DSI: invalid exception (%d)\n", ret);
                    exception = EXCP_PROGRAM;
                    error_code = EXCP_INVAL | EXCP_INVAL_INVAL;
                    break;
                }
            }
            if (rw)
                error_code |= EXCP_DSI_STORE;
	    /* Store fault address */
	    env->spr[SPR_DAR] = address;
        }
#if 0
        printf("%s: set exception to %d %02x\n",
               __func__, exception, error_code);
#endif
        env->exception_index = exception;
        env->error_code = error_code;
        ret = 1;
    }
    return ret;
}

/*****************************************************************************/
/* BATs management */
#if !defined(FLUSH_ALL_TLBS)
static inline void do_invalidate_BAT (CPUPPCState *env,
                                      target_ulong BATu, target_ulong mask)
{
    target_ulong base, end, page;
    base = BATu & ~0x0001FFFF;
    end = base + mask + 0x00020000;
#if defined (DEBUG_BATS)
    if (loglevel != 0)
        fprintf(logfile, "Flush BAT from %08x to %08x (%08x)\n", base, end, mask);
#endif
    for (page = base; page != end; page += TARGET_PAGE_SIZE)
        tlb_flush_page(env, page);
#if defined (DEBUG_BATS)
    if (loglevel != 0)
        fprintf(logfile, "Flush done\n");
#endif
}
#endif

static inline void dump_store_bat (CPUPPCState *env, char ID, int ul, int nr,
                                   target_ulong value)
{
#if defined (DEBUG_BATS)
    if (loglevel != 0) {
        fprintf(logfile, "Set %cBAT%d%c to 0x%08lx (0x%08lx)\n",
                ID, nr, ul == 0 ? 'u' : 'l', (unsigned long)value,
                (unsigned long)env->nip);
    }
#endif
}

target_ulong do_load_ibatu (CPUPPCState *env, int nr)
{
    return env->IBAT[0][nr];
}

target_ulong do_load_ibatl (CPUPPCState *env, int nr)
{
    return env->IBAT[1][nr];
}

void do_store_ibatu (CPUPPCState *env, int nr, target_ulong value)
{
    target_ulong mask;

    dump_store_bat(env, 'I', 0, nr, value);
    if (env->IBAT[0][nr] != value) {
        mask = (value << 15) & 0x0FFE0000UL;
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#endif
        /* When storing valid upper BAT, mask BEPI and BRPN
         * and invalidate all TLBs covered by this BAT
         */
        mask = (value << 15) & 0x0FFE0000UL;
        env->IBAT[0][nr] = (value & 0x00001FFFUL) |
            (value & ~0x0001FFFFUL & ~mask);
        env->IBAT[1][nr] = (env->IBAT[1][nr] & 0x0000007B) |
            (env->IBAT[1][nr] & ~0x0001FFFF & ~mask);
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->IBAT[0][nr], mask);
#endif
#if defined(FLUSH_ALL_TLBS)
        tlb_flush(env, 1);
#endif
    }
}

void do_store_ibatl (CPUPPCState *env, int nr, target_ulong value)
{
    dump_store_bat(env, 'I', 1, nr, value);
    env->IBAT[1][nr] = value;
}

target_ulong do_load_dbatu (CPUPPCState *env, int nr)
{
    return env->DBAT[0][nr];
}

target_ulong do_load_dbatl (CPUPPCState *env, int nr)
{
    return env->DBAT[1][nr];
}

void do_store_dbatu (CPUPPCState *env, int nr, target_ulong value)
{
    target_ulong mask;

    dump_store_bat(env, 'D', 0, nr, value);
    if (env->DBAT[0][nr] != value) {
        /* When storing valid upper BAT, mask BEPI and BRPN
         * and invalidate all TLBs covered by this BAT
         */
        mask = (value << 15) & 0x0FFE0000UL;
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->DBAT[0][nr], mask);
#endif
        mask = (value << 15) & 0x0FFE0000UL;
        env->DBAT[0][nr] = (value & 0x00001FFFUL) |
            (value & ~0x0001FFFFUL & ~mask);
        env->DBAT[1][nr] = (env->DBAT[1][nr] & 0x0000007B) |
            (env->DBAT[1][nr] & ~0x0001FFFF & ~mask);
#if !defined(FLUSH_ALL_TLBS)
        do_invalidate_BAT(env, env->DBAT[0][nr], mask);
#else
        tlb_flush(env, 1);
#endif
    }
}

void do_store_dbatl (CPUPPCState *env, int nr, target_ulong value)
{
    dump_store_bat(env, 'D', 1, nr, value);
    env->DBAT[1][nr] = value;
}

static inline void invalidate_all_tlbs (CPUPPCState *env)
{
    /* XXX: this needs to be completed for sotware driven TLB support */
    tlb_flush(env, 1);
}

/*****************************************************************************/
/* Special registers manipulation */
target_ulong do_load_nip (CPUPPCState *env)
{
    return env->nip;
}

void do_store_nip (CPUPPCState *env, target_ulong value)
{
    env->nip = value;
}

target_ulong do_load_sdr1 (CPUPPCState *env)
{
    return env->sdr1;
}

void do_store_sdr1 (CPUPPCState *env, target_ulong value)
{
#if defined (DEBUG_MMU)
    if (loglevel != 0) {
        fprintf(logfile, "%s: 0x%08lx\n", __func__, (unsigned long)value);
    }
#endif
    if (env->sdr1 != value) {
        env->sdr1 = value;
        invalidate_all_tlbs(env);
    }
}

target_ulong do_load_sr (CPUPPCState *env, int srnum)
{
    return env->sr[srnum];
}

void do_store_sr (CPUPPCState *env, int srnum, target_ulong value)
{
#if defined (DEBUG_MMU)
    if (loglevel != 0) {
        fprintf(logfile, "%s: reg=%d 0x%08lx %08lx\n",
                __func__, srnum, (unsigned long)value, env->sr[srnum]);
    }
#endif
    if (env->sr[srnum] != value) {
        env->sr[srnum] = value;
#if !defined(FLUSH_ALL_TLBS) && 0
        {
            target_ulong page, end;
            /* Invalidate 256 MB of virtual memory */
            page = (16 << 20) * srnum;
            end = page + (16 << 20);
            for (; page != end; page += TARGET_PAGE_SIZE)
                tlb_flush_page(env, page);
        }
#else
        invalidate_all_tlbs(env);
#endif
    }
}

uint32_t do_load_cr (CPUPPCState *env)
{
    return (env->crf[0] << 28) |
        (env->crf[1] << 24) |
        (env->crf[2] << 20) |
        (env->crf[3] << 16) |
        (env->crf[4] << 12) |
        (env->crf[5] << 8) |
        (env->crf[6] << 4) |
        (env->crf[7] << 0);
}

void do_store_cr (CPUPPCState *env, uint32_t value, uint32_t mask)
{
    int i, sh;

    for (i = 0, sh = 7; i < 8; i++, sh --) {
        if (mask & (1 << sh))
            env->crf[i] = (value >> (sh * 4)) & 0xFUL;
    }
}

uint32_t do_load_xer (CPUPPCState *env)
{
    return (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC) |
        (xer_cmp << XER_CMP);
}

void do_store_xer (CPUPPCState *env, uint32_t value)
{
    xer_so = (value >> XER_SO) & 0x01;
    xer_ov = (value >> XER_OV) & 0x01;
    xer_ca = (value >> XER_CA) & 0x01;
    xer_cmp = (value >> XER_CMP) & 0xFF;
    xer_bc = (value >> XER_BC) & 0x3F;
}

target_ulong do_load_msr (CPUPPCState *env)
{
    return (msr_vr << MSR_VR)  |
        (msr_ap  << MSR_AP)  |
        (msr_sa  << MSR_SA)  |
        (msr_key << MSR_KEY) |
        (msr_pow << MSR_POW) |
        (msr_tlb << MSR_TLB) |
        (msr_ile << MSR_ILE) |
        (msr_ee << MSR_EE) |
        (msr_pr << MSR_PR) |
        (msr_fp << MSR_FP) |
        (msr_me << MSR_ME) |
        (msr_fe0 << MSR_FE0) |
        (msr_se << MSR_SE) |
        (msr_be << MSR_BE) |
        (msr_fe1 << MSR_FE1) |
        (msr_al  << MSR_AL)  |
        (msr_ip << MSR_IP) |
        (msr_ir << MSR_IR) |
        (msr_dr << MSR_DR) |
        (msr_pe  << MSR_PE)  |
        (msr_px  << MSR_PX)  |
        (msr_ri << MSR_RI) |
        (msr_le << MSR_LE);
}

void do_compute_hflags (CPUPPCState *env)
{
    /* Compute current hflags */
    env->hflags = (msr_pr << MSR_PR) | (msr_le << MSR_LE) |
        (msr_fp << MSR_FP) | (msr_fe0 << MSR_FE0) | (msr_fe1 << MSR_FE1) |
        (msr_vr << MSR_VR) | (msr_ap << MSR_AP) | (msr_sa << MSR_SA) | 
        (msr_se << MSR_SE) | (msr_be << MSR_BE);
}

void do_store_msr (CPUPPCState *env, target_ulong value)
    {
    value &= env->msr_mask;
    if (((value >> MSR_IR) & 1) != msr_ir ||
        ((value >> MSR_DR) & 1) != msr_dr) {
        /* Flush all tlb when changing translation mode
         * When using software driven TLB, we may also need to reload
         * all defined TLBs
         */
        tlb_flush(env, 1);
        env->interrupt_request |= CPU_INTERRUPT_EXITTB;
    }
#if 0
    if (loglevel != 0) {
        fprintf(logfile, "%s: T0 %08lx\n", __func__, value);
    }
#endif
    msr_vr  = (value >> MSR_VR)  & 1;
    msr_ap  = (value >> MSR_AP)  & 1;
    msr_sa  = (value >> MSR_SA)  & 1;
    msr_key = (value >> MSR_KEY) & 1;
    msr_pow = (value >> MSR_POW) & 1;
    msr_tlb = (value >> MSR_TLB)  & 1;
    msr_ile = (value >> MSR_ILE) & 1;
    msr_ee  = (value >> MSR_EE)  & 1;
    msr_pr  = (value >> MSR_PR)  & 1;
    msr_fp  = (value >> MSR_FP)  & 1;
    msr_me  = (value >> MSR_ME)  & 1;
    msr_fe0 = (value >> MSR_FE0) & 1;
    msr_se  = (value >> MSR_SE)  & 1;
    msr_be  = (value >> MSR_BE)  & 1;
    msr_fe1 = (value >> MSR_FE1) & 1;
    msr_al  = (value >> MSR_AL)  & 1;
    msr_ip  = (value >> MSR_IP)  & 1;
    msr_ir  = (value >> MSR_IR)  & 1;
    msr_dr  = (value >> MSR_DR)  & 1;
    msr_pe  = (value >> MSR_PE)  & 1;
    msr_px  = (value >> MSR_PX)  & 1;
    msr_ri  = (value >> MSR_RI)  & 1;
    msr_le  = (value >> MSR_LE)  & 1;
    do_compute_hflags(env);
}

float64 do_load_fpscr (CPUPPCState *env)
{
    /* The 32 MSB of the target fpr are undefined.
     * They'll be zero...
     */
    union {
        float64 d;
        struct {
            uint32_t u[2];
        } s;
    } u;
    int i;

#ifdef WORDS_BIGENDIAN
#define WORD0 0
#define WORD1 1
#else
#define WORD0 1
#define WORD1 0
#endif
    u.s.u[WORD0] = 0;
    u.s.u[WORD1] = 0;
    for (i = 0; i < 8; i++)
        u.s.u[WORD1] |= env->fpscr[i] << (4 * i);
    return u.d;
}

void do_store_fpscr (CPUPPCState *env, float64 f, uint32_t mask)
{
    /*
     * We use only the 32 LSB of the incoming fpr
     */
    union {
        double d;
        struct {
            uint32_t u[2];
        } s;
    } u;
    int i, rnd_type;

    u.d = f;
    if (mask & 0x80)
        env->fpscr[0] = (env->fpscr[0] & 0x9) | ((u.s.u[WORD1] >> 28) & ~0x9);
    for (i = 1; i < 7; i++) {
        if (mask & (1 << (7 - i)))
            env->fpscr[i] = (u.s.u[WORD1] >> (4 * (7 - i))) & 0xF;
    }
    /* TODO: update FEX & VX */
    /* Set rounding mode */
    switch (env->fpscr[0] & 0x3) {
    case 0:
        /* Best approximation (round to nearest) */
        rnd_type = float_round_nearest_even;
        break;
    case 1:
        /* Smaller magnitude (round toward zero) */
        rnd_type = float_round_to_zero;
        break;
    case 2:
        /* Round toward +infinite */
        rnd_type = float_round_up;
        break;
    default:
    case 3:
        /* Round toward -infinite */
        rnd_type = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_type, &env->fp_status);
}

/*****************************************************************************/
/* Exception processing */
#if defined (CONFIG_USER_ONLY)
void do_interrupt (CPUState *env)
{
    env->exception_index = -1;
}
#else
static void dump_syscall(CPUState *env)
{
    fprintf(logfile, "syscall r0=0x%08x r3=0x%08x r4=0x%08x r5=0x%08x r6=0x%08x nip=0x%08x\n",
            env->gpr[0], env->gpr[3], env->gpr[4],
            env->gpr[5], env->gpr[6], env->nip);
}

void do_interrupt (CPUState *env)
{
    uint32_t msr;
    int excp;

    excp = env->exception_index;
    msr = do_load_msr(env);
#if defined (DEBUG_EXCEPTIONS)
    if ((excp == EXCP_PROGRAM || excp == EXCP_DSI) && msr_pr == 1) 
    {
        if (loglevel > 0) {
            fprintf(logfile, "Raise exception at 0x%08x => 0x%08x (%02x)\n",
                    env->nip, excp << 8, env->error_code);
        }
	if (loglevel > 0)
	    cpu_dump_state(env, logfile, fprintf, 0);
    }
#endif
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "Raise exception at 0x%08x => 0x%08x (%02x)\n",
                env->nip, excp << 8, env->error_code);
    }
    /* Generate informations in save/restore registers */
    switch (excp) {
    case EXCP_NONE:
        /* Do nothing */
#if defined (DEBUG_EXCEPTIONS)
        printf("%s: escape EXCP_NONE\n", __func__);
#endif
        return;
    case EXCP_RESET:
        if (msr_ip)
            excp += 0xFFC00;
        goto store_next;
    case EXCP_MACHINE_CHECK:
        if (msr_me == 0) {
            cpu_abort(env, "Machine check exception while not allowed\n");
        }
        msr_me = 0;
        break;
    case EXCP_DSI:
        /* Store exception cause */
        /* data location address has been stored
         * when the fault has been detected
     */
	msr &= ~0xFFFF0000;
	env->spr[SPR_DSISR] = 0;
	if ((env->error_code & 0x0f) ==  EXCP_DSI_TRANSLATE)
	    env->spr[SPR_DSISR] |= 0x40000000;
	else if ((env->error_code & 0x0f) ==  EXCP_DSI_PROT)
	    env->spr[SPR_DSISR] |= 0x08000000;
	else if ((env->error_code & 0x0f) ==  EXCP_DSI_NOTSUP) {
	    env->spr[SPR_DSISR] |= 0x80000000;
	    if (env->error_code & EXCP_DSI_DIRECT)
		env->spr[SPR_DSISR] |= 0x04000000;
	}
	if (env->error_code & EXCP_DSI_STORE)
	    env->spr[SPR_DSISR] |= 0x02000000;
	if ((env->error_code & 0xF) == EXCP_DSI_DABR)
	    env->spr[SPR_DSISR] |= 0x00400000;
	if (env->error_code & EXCP_DSI_ECXW)
	    env->spr[SPR_DSISR] |= 0x00100000;
#if defined (DEBUG_EXCEPTIONS)
	if (loglevel) {
	    fprintf(logfile, "DSI exception: DSISR=0x%08x, DAR=0x%08x\n",
		    env->spr[SPR_DSISR], env->spr[SPR_DAR]);
	} else {
	    printf("DSI exception: DSISR=0x%08x, DAR=0x%08x nip=0x%08x\n",
		   env->spr[SPR_DSISR], env->spr[SPR_DAR], env->nip);
	}
#endif
        goto store_next;
    case EXCP_ISI:
        /* Store exception cause */
	msr &= ~0xFFFF0000;
        if (env->error_code == EXCP_ISI_TRANSLATE)
            msr |= 0x40000000;
        else if (env->error_code == EXCP_ISI_NOEXEC ||
		 env->error_code == EXCP_ISI_GUARD ||
		 env->error_code == EXCP_ISI_DIRECT)
            msr |= 0x10000000;
        else
            msr |= 0x08000000;
#if defined (DEBUG_EXCEPTIONS)
	if (loglevel) {
	    fprintf(logfile, "ISI exception: msr=0x%08x, nip=0x%08x\n",
		    msr, env->nip);
	} else {
	    printf("ISI exception: msr=0x%08x, nip=0x%08x tbl:0x%08x\n",
		   msr, env->nip, env->spr[V_TBL]);
	}
#endif
        goto store_next;
    case EXCP_EXTERNAL:
        if (msr_ee == 0) {
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel > 0) {
                fprintf(logfile, "Skipping hardware interrupt\n");
    }
#endif
            /* Requeue it */
            do_raise_exception(EXCP_EXTERNAL);
            return;
            }
        goto store_next;
    case EXCP_ALIGN:
        /* Store exception cause */
        /* Get rS/rD and rA from faulting opcode */
        env->spr[SPR_DSISR] |=
            (ldl_code((env->nip - 4)) & 0x03FF0000) >> 16;
        /* data location address has been stored
         * when the fault has been detected
         */
        goto store_current;
    case EXCP_PROGRAM:
        msr &= ~0xFFFF0000;
        switch (env->error_code & ~0xF) {
        case EXCP_FP:
            if (msr_fe0 == 0 && msr_fe1 == 0) {
#if defined (DEBUG_EXCEPTIONS)
                printf("Ignore floating point exception\n");
#endif
                return;
        }
            msr |= 0x00100000;
            /* Set FX */
            env->fpscr[7] |= 0x8;
            /* Finally, update FEX */
            if ((((env->fpscr[7] & 0x3) << 3) | (env->fpscr[6] >> 1)) &
                ((env->fpscr[1] << 1) | (env->fpscr[0] >> 3)))
                env->fpscr[7] |= 0x4;
        break;
        case EXCP_INVAL:
            //	    printf("Invalid instruction at 0x%08x\n", env->nip);
            msr |= 0x00080000;
        break;
        case EXCP_PRIV:
            msr |= 0x00040000;
        break;
        case EXCP_TRAP:
            msr |= 0x00020000;
            break;
        default:
            /* Should never occur */
        break;
    }
        msr |= 0x00010000;
        goto store_current;
    case EXCP_NO_FP:
        msr &= ~0xFFFF0000;
        goto store_current;
    case EXCP_DECR:
        if (msr_ee == 0) {
            /* Requeue it */
            do_raise_exception(EXCP_DECR);
            return;
        }
        goto store_next;
    case EXCP_SYSCALL:
        /* NOTE: this is a temporary hack to support graphics OSI
           calls from the MOL driver */
        if (env->gpr[3] == 0x113724fa && env->gpr[4] == 0x77810f9b &&
            env->osi_call) {
            if (env->osi_call(env) != 0)
                return;
        }
        if (loglevel & CPU_LOG_INT) {
            dump_syscall(env);
        }
        goto store_next;
    case EXCP_TRACE:
        goto store_next;
    case EXCP_FP_ASSIST:
        goto store_next;
    case EXCP_MTMSR:
        /* Nothing to do */
        return;
    case EXCP_BRANCH:
        /* Nothing to do */
        return;
    case EXCP_RFI:
        /* Restore user-mode state */
#if defined (DEBUG_EXCEPTIONS)
	if (msr_pr == 1)
	    printf("Return from exception => 0x%08x\n", (uint32_t)env->nip);
#endif
        return;
    store_current:
        /* SRR0 is set to current instruction */
        env->spr[SPR_SRR0] = (uint32_t)env->nip - 4;
        break;
    store_next:
        /* SRR0 is set to next instruction */
        env->spr[SPR_SRR0] = (uint32_t)env->nip;
        break;
    }
    env->spr[SPR_SRR1] = msr;
    /* reload MSR with correct bits */
    msr_pow = 0;
    msr_ee = 0;
    msr_pr = 0;
    msr_fp = 0;
    msr_fe0 = 0;
    msr_se = 0;
    msr_be = 0;
    msr_fe1 = 0;
    msr_ir = 0;
    msr_dr = 0;
    msr_ri = 0;
    msr_le = msr_ile;
    do_compute_hflags(env);
    /* Jump to handler */
    env->nip = excp << 8;
    env->exception_index = EXCP_NONE;
    /* Invalidate all TLB as we may have changed translation mode */
#ifdef ACCURATE_TLB_FLUSH
    tlb_flush(env, 1);
#endif
    /* ensure that no TB jump will be modified as
       the program flow was changed */
#ifdef __sparc__
    tmp_T0 = 0;
#else
    T0 = 0;
#endif
    env->exception_index = -1;
}
#endif /* !CONFIG_USER_ONLY */
