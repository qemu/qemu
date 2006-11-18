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
//#define DEBUG_BATS
//#define DEBUG_EXCEPTIONS
//#define FLUSH_ALL_TLBS

/*****************************************************************************/
/* PowerPC MMU emulation */

#if defined(CONFIG_USER_ONLY) 
int cpu_ppc_handle_mmu_fault (CPUState *env, uint32_t address, int rw,
                              int is_user, int is_softmmu)
{
    int exception, error_code;
    
    if (rw == 2) {
        exception = EXCP_ISI;
        error_code = 0;
    } else {
        exception = EXCP_DSI;
        error_code = 0;
        if (rw)
            error_code |= 0x02000000;
        env->spr[SPR_DAR] = address;
        env->spr[SPR_DSISR] = error_code;
    }
    env->exception_index = exception;
    env->error_code = error_code;
    return 1;
}
target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}
#else
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

static int get_physical_address (CPUState *env, uint32_t *physical, int *prot,
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

target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    uint32_t phys_addr;
    int prot;

    if (get_physical_address(env, &phys_addr, &prot, addr, 0, ACCESS_INT) != 0)
        return -1;
    return phys_addr;
}

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
                error_code = 0x40000000;
                break;
            case -2:
                /* Access rights violation */
                error_code = 0x08000000;
                break;
            case -3:
		/* No execute protection violation */
                error_code = 0x10000000;
                break;
            case -4:
                /* Direct store exception */
                /* No code fetch is allowed in direct-store areas */
                error_code = 0x10000000;
                break;
            case -5:
                /* No match in segment table */
                exception = EXCP_ISEG;
                error_code = 0;
                break;
            }
        } else {
            exception = EXCP_DSI;
            switch (ret) {
            case -1:
                /* No matches in page tables */
                error_code = 0x40000000;
                break;
            case -2:
                /* Access rights violation */
                error_code = 0x08000000;
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
                    error_code = 0x04000000;
                    break;
                case ACCESS_EXT:
                    /* eciwx or ecowx */
                    error_code = 0x04100000;
                    break;
                default:
		    printf("DSI: invalid exception (%d)\n", ret);
                    exception = EXCP_PROGRAM;
                    error_code = EXCP_INVAL | EXCP_INVAL_INVAL;
                    break;
                }
                break;
            case -5:
                /* No match in segment table */
                exception = EXCP_DSEG;
                error_code = 0;
                break;
            }
            if (exception == EXCP_DSI && rw == 1)
                error_code |= 0x02000000;
	    /* Store fault address */
	    env->spr[SPR_DAR] = address;
            env->spr[SPR_DSISR] = error_code;
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
#endif

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
    int enter_pm;

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

    enter_pm = 0;
    switch (PPC_EXCP(env)) {
    case PPC_FLAGS_EXCP_7x0:
	if (msr_pow == 1 && (env->spr[SPR_HID0] & 0x00E00000) != 0)
            enter_pm = 1;
        break;
    default:
        break;
    }
    if (enter_pm) {
        /* power save: exit cpu loop */
        env->halted = 1;
        env->exception_index = EXCP_HLT;
        cpu_loop_exit();
    }
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
    target_ulong msr, *srr_0, *srr_1, tmp;
    int excp;

    excp = env->exception_index;
    msr = do_load_msr(env);
    /* The default is to use SRR0 & SRR1 to save the exception context */
    srr_0 = &env->spr[SPR_SRR0];
    srr_1 = &env->spr[SPR_SRR1];
#if defined (DEBUG_EXCEPTIONS)
    if ((excp == EXCP_PROGRAM || excp == EXCP_DSI) && msr_pr == 1) {
        if (loglevel != 0) {
            fprintf(logfile, "Raise exception at 0x%08lx => 0x%08x (%02x)\n",
                    (unsigned long)env->nip, excp, env->error_code);
 	    cpu_dump_state(env, logfile, fprintf, 0);
        }
    }
#endif
    if (loglevel & CPU_LOG_INT) {
        fprintf(logfile, "Raise exception at 0x%08lx => 0x%08x (%02x)\n",
                (unsigned long)env->nip, excp, env->error_code);
    }
    msr_pow = 0;
    /* Generate informations in save/restore registers */
    switch (excp) {
        /* Generic PowerPC exceptions */
    case EXCP_RESET: /* 0x0100 */
        if (PPC_EXCP(env) != PPC_FLAGS_EXCP_40x) {
            if (msr_ip)
                excp += 0xFFC00;
            excp |= 0xFFC00000;
        } else {
            srr_0 = &env->spr[SPR_40x_SRR2];
            srr_1 = &env->spr[SPR_40x_SRR3];
        }
        goto store_next;
    case EXCP_MACHINE_CHECK: /* 0x0200 */
        if (msr_me == 0) {
            cpu_abort(env, "Machine check exception while not allowed\n");
        }
        if (PPC_EXCP(env) == PPC_FLAGS_EXCP_40x) {
            srr_0 = &env->spr[SPR_40x_SRR2];
            srr_1 = &env->spr[SPR_40x_SRR3];
        }
        msr_me = 0;
        break;
    case EXCP_DSI: /* 0x0300 */
        /* Store exception cause */
        /* data location address has been stored
         * when the fault has been detected
         */
	msr &= ~0xFFFF0000;
#if defined (DEBUG_EXCEPTIONS)
	if (loglevel) {
	    fprintf(logfile, "DSI exception: DSISR=0x%08x, DAR=0x%08x\n",
		    env->spr[SPR_DSISR], env->spr[SPR_DAR]);
	} else {
	    printf("DSI exception: DSISR=0x%08x, DAR=0x%08x\n",
		   env->spr[SPR_DSISR], env->spr[SPR_DAR]);
	}
#endif
        goto store_next;
    case EXCP_ISI: /* 0x0400 */
        /* Store exception cause */
	msr &= ~0xFFFF0000;
        msr |= env->error_code;
#if defined (DEBUG_EXCEPTIONS)
	if (loglevel != 0) {
	    fprintf(logfile, "ISI exception: msr=0x%08x, nip=0x%08x\n",
		    msr, env->nip);
	}
#endif
        goto store_next;
    case EXCP_EXTERNAL: /* 0x0500 */
        if (msr_ee == 0) {
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel > 0) {
                fprintf(logfile, "Skipping hardware interrupt\n");
            }
#endif
            /* Requeue it */
            env->interrupt_request |= CPU_INTERRUPT_HARD;
            return;
        }
        goto store_next;
    case EXCP_ALIGN: /* 0x0600 */
        if (PPC_EXCP(env) != PPC_FLAGS_EXCP_601) {
            /* Store exception cause */
            /* Get rS/rD and rA from faulting opcode */
            env->spr[SPR_DSISR] |=
                (ldl_code((env->nip - 4)) & 0x03FF0000) >> 16;
            /* data location address has been stored
             * when the fault has been detected
             */
        } else {
            /* IO error exception on PowerPC 601 */
            /* XXX: TODO */
            cpu_abort(env,
                      "601 IO error exception is not implemented yet !\n");
        }
        goto store_current;
    case EXCP_PROGRAM: /* 0x0700 */
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
    case EXCP_NO_FP: /* 0x0800 */
        msr &= ~0xFFFF0000;
        goto store_current;
    case EXCP_DECR:
        if (msr_ee == 0) {
#if 1
            /* Requeue it */
            env->interrupt_request |= CPU_INTERRUPT_TIMER;
#endif
            return;
        }
        goto store_next;
    case EXCP_SYSCALL: /* 0x0C00 */
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
    case EXCP_TRACE: /* 0x0D00 */
        /* XXX: TODO */
        cpu_abort(env, "Trace exception is not implemented yet !\n");
        goto store_next;
    case EXCP_PERF: /* 0x0F00 */
        /* XXX: TODO */
        cpu_abort(env,
                  "Performance counter exception is not implemented yet !\n");
        goto store_next;
    /* 32 bits PowerPC specific exceptions */
    case EXCP_FP_ASSIST: /* 0x0E00 */
        /* XXX: TODO */
        cpu_abort(env, "Floating point assist exception "
                  "is not implemented yet !\n");
        goto store_next;
    /* 64 bits PowerPC exceptions */
    case EXCP_DSEG: /* 0x0380 */
        /* XXX: TODO */
        cpu_abort(env, "Data segment exception is not implemented yet !\n");
        goto store_next;
    case EXCP_ISEG: /* 0x0480 */
        /* XXX: TODO */
        cpu_abort(env,
                  "Instruction segment exception is not implemented yet !\n");
        goto store_next;
    case EXCP_HDECR: /* 0x0980 */
        if (msr_ee == 0) {
#if 1
            /* Requeue it */
            env->interrupt_request |= CPU_INTERRUPT_TIMER;
#endif
        return;
        }
        cpu_abort(env,
                  "Hypervisor decrementer exception is not implemented yet !\n");
        goto store_next;
    /* Implementation specific exceptions */
    case 0x0A00:
        if (PPC_EXCP(env) != PPC_FLAGS_EXCP_602) {
            /* Critical interrupt on G2 */
            /* XXX: TODO */
            cpu_abort(env, "G2 critical interrupt is not implemented yet !\n");
            goto store_next;
        } else {
            cpu_abort(env, "Invalid exception 0x0A00 !\n");
        }
        return;
    case 0x0F20:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_40x:
            /* APU unavailable on 405 */
            /* XXX: TODO */
            cpu_abort(env,
                      "APU unavailable exception is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_74xx:
            /* Altivec unavailable */
            /* XXX: TODO */
            cpu_abort(env, "Altivec unavailable exception "
                      "is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x0F20 !\n");
            break;
        }
        return;
    case 0x1000:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_40x:
            /* PIT on 4xx */
            /* XXX: TODO */
            cpu_abort(env, "40x PIT exception is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_602:
        case PPC_FLAGS_EXCP_603:
            /* ITLBMISS on 602/603 */
            msr &= ~0xF00F0000;
            msr_tgpr = 1;
            goto store_gprs;
        default:
            cpu_abort(env, "Invalid exception 0x1000 !\n");
            break;
        }
        return;
    case 0x1010:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_40x:
            /* FIT on 4xx */
            cpu_abort(env, "40x FIT exception is not implemented yet !\n");
            /* XXX: TODO */
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1010 !\n");
            break;
        }
        return;
    case 0x1020:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_40x:
            /* Watchdog on 4xx */
            /* XXX: TODO */
            cpu_abort(env,
                      "40x watchdog exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1020 !\n");
            break;
        }
        return;
    case 0x1100:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_40x:
            /* DTLBMISS on 4xx */
            /* XXX: TODO */
            cpu_abort(env,
                      "40x DTLBMISS exception is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_602:
        case PPC_FLAGS_EXCP_603:
            /* DLTLBMISS on 602/603 */
            msr &= ~0xF00F0000;
            msr_tgpr = 1;
            goto store_gprs;
        default:
            cpu_abort(env, "Invalid exception 0x1100 !\n");
            break;
        }
        return;
    case 0x1200:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_40x:
            /* ITLBMISS on 4xx */
            /* XXX: TODO */
            cpu_abort(env,
                      "40x ITLBMISS exception is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_602:
        case PPC_FLAGS_EXCP_603:
            /* DSTLBMISS on 602/603 */
            msr &= ~0xF00F0000;
            msr_tgpr = 1;
        store_gprs:
#if defined (DEBUG_SOFTWARE_TLB)
            if (loglevel != 0) {
                fprintf(logfile, "6xx %sTLB miss: IM %08x DM %08x IC %08x "
                        "DC %08x H1 %08x H2 %08x %08x\n",
                        excp == 0x1000 ? "I" : excp == 0x1100 ? "DL" : "DS",
                        env->spr[SPR_IMISS], env->spr[SPR_DMISS],
                        env->spr[SPR_ICMP], env->spr[SPR_DCMP],
                        env->spr[SPR_DHASH1], env->spr[SPR_DHASH2],
                        env->error_code);
            }
#endif
            /* Swap temporary saved registers with GPRs */
            tmp = env->gpr[0];
            env->gpr[0] = env->tgpr[0];
            env->tgpr[0] = tmp;
            tmp = env->gpr[1];
            env->gpr[1] = env->tgpr[1];
            env->tgpr[1] = tmp;
            tmp = env->gpr[2];
            env->gpr[2] = env->tgpr[2];
            env->tgpr[2] = tmp;
            tmp = env->gpr[3];
            env->gpr[3] = env->tgpr[3];
            env->tgpr[3] = tmp;
            msr |= env->crf[0] << 28;
            msr |= env->error_code; /* key, D/I, S/L bits */
            /* Set way using a LRU mechanism */
            msr |= (env->last_way ^ 1) << 17;
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1200 !\n");
            break;
        }
        return;
    case 0x1300:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_601:
        case PPC_FLAGS_EXCP_602:
        case PPC_FLAGS_EXCP_603:
        case PPC_FLAGS_EXCP_604:
        case PPC_FLAGS_EXCP_7x0:
        case PPC_FLAGS_EXCP_7x5:
            /* IABR on 6xx/7xx */
            /* XXX: TODO */
            cpu_abort(env, "IABR exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1300 !\n");
            break;
        }
        return;
    case 0x1400:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_601:
        case PPC_FLAGS_EXCP_602:
        case PPC_FLAGS_EXCP_603:
        case PPC_FLAGS_EXCP_604:
        case PPC_FLAGS_EXCP_7x0:
        case PPC_FLAGS_EXCP_7x5:
            /* SMI on 6xx/7xx */
            /* XXX: TODO */
            cpu_abort(env, "SMI exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1400 !\n");
            break;
        }
        return;
    case 0x1500:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_602:
            /* Watchdog on 602 */
            cpu_abort(env,
                      "602 watchdog exception is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_970:
            /* Soft patch exception on 970 */
            /* XXX: TODO */
            cpu_abort(env,
                      "970 soft-patch exception is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_74xx:
            /* VPU assist on 74xx */
            /* XXX: TODO */
            cpu_abort(env, "VPU assist exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1500 !\n");
            break;
        }
        return;
    case 0x1600:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_602:
            /* Emulation trap on 602 */
            /* XXX: TODO */
            cpu_abort(env, "602 emulation trap exception "
                      "is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_970:
            /* Maintenance exception on 970 */
            /* XXX: TODO */
            cpu_abort(env,
                      "970 maintenance exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1600 !\n");
            break;
        }
        return;
    case 0x1700:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_7x0:
        case PPC_FLAGS_EXCP_7x5:
            /* Thermal management interrupt on G3 */
            /* XXX: TODO */
            cpu_abort(env, "G3 thermal management exception "
                      "is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_970:
            /* VPU assist on 970 */
            /* XXX: TODO */
            cpu_abort(env,
                      "970 VPU assist exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1700 !\n");
            break;
        }
        return;
    case 0x1800:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_970:
            /* Thermal exception on 970 */
            /* XXX: TODO */
            cpu_abort(env, "970 thermal management exception "
                      "is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1800 !\n");
            break;
        }
        return;
    case 0x2000:
        switch (PPC_EXCP(env)) {
        case PPC_FLAGS_EXCP_40x:
            /* DEBUG on 4xx */
            /* XXX: TODO */
            cpu_abort(env, "40x debug exception is not implemented yet !\n");
            goto store_next;
        case PPC_FLAGS_EXCP_601:
            /* Run mode exception on 601 */
            /* XXX: TODO */
            cpu_abort(env,
                      "601 run mode exception is not implemented yet !\n");
            goto store_next;
        default:
            cpu_abort(env, "Invalid exception 0x1800 !\n");
            break;
        }
        return;
    /* Other exceptions */
    /* Qemu internal exceptions:
     * we should never come here with those values: abort execution
     */
    default:
        cpu_abort(env, "Invalid exception: code %d (%04x)\n", excp, excp);
        return;
    store_current:
        /* save current instruction location */
        *srr_0 = (env->nip - 4) & 0xFFFFFFFFULL;
        break;
    store_next:
        /* save next instruction location */
        *srr_0 = env->nip & 0xFFFFFFFFULL;
        break;
    }
    /* Save msr */
    *srr_1 = msr;
    /* If we disactivated any translation, flush TLBs */
    if (msr_ir || msr_dr) {
        tlb_flush(env, 1);
    }
    /* reload MSR with correct bits */
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
    msr_sf = msr_isf;
    do_compute_hflags(env);
    /* Jump to handler */
    env->nip = excp;
    env->exception_index = EXCP_NONE;
}
#endif /* !CONFIG_USER_ONLY */
