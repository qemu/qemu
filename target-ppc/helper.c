/*
 *  PPC emulation helpers for qemu.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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
#include <sys/mman.h>

#include "exec.h"
#if defined (USE_OPEN_FIRMWARE)
#include "of.h"
#endif

//#define DEBUG_MMU
//#define DEBUG_BATS
//#define DEBUG_EXCEPTIONS

extern FILE *logfile, *stderr;
void exit (int);
void abort (void);

int phys_ram_size;
int phys_ram_fd;
uint8_t *phys_ram_base;

void cpu_loop_exit(void)
{
    longjmp(env->jmp_env, 1);
}

void do_process_exceptions (void)
{
    cpu_loop_exit();
}

int check_exception_state (CPUState *env)
{
    int i;

    /* Process PPC exceptions */
    for (i = 1; i  < EXCP_PPC_MAX; i++) {
        if (env->exceptions & (1 << i)) {
            switch (i) {
            case EXCP_EXTERNAL:
            case EXCP_DECR:
                if (msr_ee == 0)
                    return 0;
                break;
            case EXCP_PROGRAM:
                if (env->errors[EXCP_PROGRAM] == EXCP_FP &&
                    msr_fe0 == 0 && msr_fe1 == 0)
                    return 0;
                break;
            default:
                break;
            }
            env->exception_index = i;
            env->error_code = env->errors[i];
            return 1;
        }
    }

    return 0;
}

/*****************************************************************************/
/* PPC MMU emulation */
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
    printf("%s: %cBAT v 0x%08x\n", __func__,
           type == ACCESS_CODE ? 'I' : 'D', virtual);
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
    printf("%s...: %cBAT v 0x%08x\n", __func__,
           type == ACCESS_CODE ? 'I' : 'D', virtual);
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
        } else {
            printf("%s: %cBAT%d v 0x%08x BATu 0x%08x BATl 0x%08x\n",
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
                    (virtual & 0x0001FFFF);
                if (*BATl & 0x00000001)
                    *prot = PROT_READ;
                if (*BATl & 0x00000002)
                    *prot = PROT_WRITE | PROT_READ;
#if defined (DEBUG_BATS)
                if (loglevel > 0) {
                    fprintf(logfile, "BAT %d match: r 0x%08x prot=%c%c\n",
                            i, *real, *prot & PROT_READ ? 'R' : '-',
                            *prot & PROT_WRITE ? 'W' : '-');
                } else {
                    printf("BAT %d match: 0x%08x => 0x%08x prot=%c%c\n",
                           i, virtual, *real, *prot & PROT_READ ? 'R' : '-',
                           *prot & PROT_WRITE ? 'W' : '-');
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
        env->spr[DAR] = virtual;
    }
    /* No hit */
    return ret;
}

/* PTE table lookup */
static int find_pte (uint32_t *RPN, int *prot, uint32_t base, uint32_t va,
                     int h, int key, int rw)
{
    uint32_t pte0, pte1, keep = 0;
    int i, good = -1, store = 0;
    int ret = -1; /* No entry found */

    for (i = 0; i < 8; i++) {
        pte0 = ldl_raw((void *)((uint32_t)phys_ram_base + base + (i * 8)));
        pte1 =  ldl_raw((void *)((uint32_t)phys_ram_base + base + (i * 8) + 4));
#if defined (DEBUG_MMU)
        printf("Load pte from 0x%08x => 0x%08x 0x%08x\n", base + (i * 8),
               pte0, pte1);
#endif
        /* Check validity and table match */
        if (pte0 & 0x80000000 && (h == ((pte0 >> 6) & 1))) {
#if defined (DEBUG_MMU)
            printf("PTE is valid and table matches... compare 0x%08x:%08x\n",
                   pte0 & 0x7FFFFFBF, va);
#endif
            /* Check vsid & api */
            if ((pte0 & 0x7FFFFFBF) == va) {
#if defined (DEBUG_MMU)
                printf("PTE match !\n");
#endif
                if (good == -1) {
                    good = i;
                    keep = pte1;
                } else {
                    /* All matches should have equal RPN, WIMG & PP */
                    if ((keep & 0xFFFFF07B) != (pte1 & 0xFFFFF07B)) {
                        printf("Bad RPN/WIMG/PP\n");
                        return -1;
                    }
                }
                /* Check access rights */
                if (key == 0) {
                    *prot = PROT_READ;
                    if ((pte1 & 0x00000003) != 0x3)
                        *prot |= PROT_WRITE;
                } else {
                    switch (pte1 & 0x00000003) {
                    case 0x0:
                        *prot = 0;
                        break;
                    case 0x1:
                    case 0x3:
                        *prot = PROT_READ;
                        break;
                    case 0x2:
                        *prot = PROT_READ | PROT_WRITE;
                        break;
                    }
                }
                if ((rw == 0 && *prot != 0) ||
                    (rw == 1 && (*prot & PROT_WRITE))) {
#if defined (DEBUG_MMU)
                    printf("PTE access granted !\n");
#endif
                    good = i;
                    keep = pte1;
                    ret = 0;
                } else if (ret == -1) {
                    ret = -2; /* Access right violation */
#if defined (DEBUG_MMU)
                    printf("PTE access rejected\n");
#endif
                }
            }
        }
    }
    if (good != -1) {
        *RPN = keep & 0xFFFFF000;
#if defined (DEBUG_MMU)
        printf("found PTE at addr 0x%08x prot=0x%01x ret=%d\n",
               *RPN, *prot, ret);
#endif
        /* Update page flags */
        if (!(keep & 0x00000100)) {
            keep |= 0x00000100;
            store = 1;
        }
        if (rw) {
            if (!(keep & 0x00000080)) {
                keep |= 0x00000080;
                store = 1;
            }
        }
        if (store)
            stl_raw((void *)(base + (good * 2) + 1), keep);
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
    printf("Check segment v=0x%08x %d 0x%08x nip=0x%08x lr=0x%08x ir=%d dr=%d "
           "pr=%d t=%d\n", virtual, virtual >> 28, sr, env->nip,
           env->lr, msr_ir, msr_dr, msr_pr, type);
#endif
    key = ((sr & 0x20000000) && msr_pr == 1) ||
        ((sr & 0x40000000) && msr_pr == 0) ? 1 : 0;
    if ((sr & 0x80000000) == 0) {
#if defined (DEBUG_MMU)
        printf("pte segment: key=%d n=0x%08x\n", key, sr & 0x10000000);
#endif
        /* Check if instruction fetch is allowed, if needed */
        if (type != ACCESS_CODE || (sr & 0x10000000) == 0) {
            /* Page address translation */
            vsid = sr & 0x00FFFFFF;
            pgidx = (virtual >> 12) & 0xFFFF;
            sdr = env->spr[SDR1];
            hash = ((vsid ^ pgidx) & 0x07FFFF) << 6;
            mask = ((sdr & 0x000001FF) << 16) | 0xFFC0;
            pg_addr = get_pgaddr(sdr, hash, mask);
            ptem = (vsid << 7) | (pgidx >> 10);
#if defined (DEBUG_MMU)
            printf("0 sdr1=0x%08x vsid=0x%06x api=0x%04x hash=0x%07x "
                   "pg_addr=0x%08x\n", sdr, vsid, pgidx, hash, pg_addr);
#endif
            /* Primary table lookup */
            ret = find_pte(real, prot, pg_addr, ptem, 0, key, rw);
            if (ret < 0) {
                /* Secondary table lookup */
                hash = (~hash) & 0x01FFFFC0;
                pg_addr = get_pgaddr(sdr, hash, mask);
#if defined (DEBUG_MMU)
                printf("1 sdr1=0x%08x vsid=0x%06x api=0x%04x hash=0x%05x "
                       "pg_addr=0x%08x\n", sdr, vsid, pgidx, hash, pg_addr);
#endif
                ret2 = find_pte(real, prot, pg_addr, ptem, 1, key, rw);
                if (ret2 != -1)
                    ret = ret2;
            }
            if (ret != -1)
                *real |= (virtual & 0x00000FFF);
            if (ret == -2 && type == ACCESS_CODE && (sr & 0x10000000))
                ret = -3;
        } else {
#if defined (DEBUG_MMU)
            printf("No access allowed\n");
#endif
        }
    } else {
#if defined (DEBUG_MMU)
        printf("direct store...\n");
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

    if (loglevel > 0) {
        fprintf(logfile, "%s\n", __func__);
    }
    if ((access_type == ACCESS_CODE && msr_ir == 0) || msr_dr == 0) {
        /* No address translation */
        *physical = address;
        *prot = PROT_READ | PROT_WRITE;
        ret = 0;
    } else {
        /* Try to find a BAT */
        ret = get_bat(env, physical, prot, address, rw, access_type);
        if (ret < 0) {
            /* We didn't match any BAT entry */
            ret = get_segment(env, physical, prot, address, rw, access_type);
        }
    }
    
    return ret;
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
void tlb_fill(unsigned long addr, int is_write, int flags, void *retaddr)
{
    TranslationBlock *tb;
    int ret, is_user;
    unsigned long pc;
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;
    is_user = flags & 0x01;
    {
        unsigned long tlb_addrr, tlb_addrw;
        int index;
        index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addrr = env->tlb_read[is_user][index].address;
        tlb_addrw = env->tlb_write[is_user][index].address;
#if 0
        printf("%s 1 %p %p idx=%d addr=0x%08lx tbl_addr=0x%08lx 0x%08lx "
               "(0x%08lx 0x%08lx)\n", __func__, env,
               &env->tlb_read[is_user][index], index, addr,
               tlb_addrr, tlb_addrw, addr & TARGET_PAGE_MASK,
               tlb_addrr & (TARGET_PAGE_MASK | TLB_INVALID_MASK));
#endif
    }
    ret = cpu_handle_mmu_fault(env, addr, is_write, flags, 1);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc);
            }
        }
        do_queue_exception_err(env->exception_index, env->error_code);
        do_process_exceptions();
    }
    {
        unsigned long tlb_addrr, tlb_addrw;
        int index;
        index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
        tlb_addrr = env->tlb_read[is_user][index].address;
        tlb_addrw = env->tlb_write[is_user][index].address;
#if 0
        printf("%s 2 %p %p idx=%d addr=0x%08lx tbl_addr=0x%08lx 0x%08lx "
               "(0x%08lx 0x%08lx)\n", __func__, env,
               &env->tlb_read[is_user][index], index, addr,
               tlb_addrr, tlb_addrw, addr & TARGET_PAGE_MASK,
               tlb_addrr & (TARGET_PAGE_MASK | TLB_INVALID_MASK));
#endif
    }
    env = saved_env;
}

void cpu_ppc_init_mmu(CPUPPCState *env)
{
    /* Nothing to do: all translation are disabled */
}
#endif

/* Perform address translation */
int cpu_ppc_handle_mmu_fault (CPUState *env, uint32_t address, int rw,
                              int flags, int is_softmmu)
{
    uint32_t physical;
    int prot;
    int exception = 0, error_code = 0;
    int is_user, access_type;
    int ret = 0;

//    printf("%s 0\n", __func__);
    is_user = flags & 0x01;
    access_type = flags & ~0x01;
    if (env->user_mode_only) {
        /* user mode only emulation */
        ret = -1;
        goto do_fault;
    }
    ret = get_physical_address(env, &physical, &prot,
                               address, rw, access_type);
    if (ret == 0) {
        ret = tlb_set_page(env, address, physical, prot, is_user, is_softmmu);
    } else if (ret < 0) {
    do_fault:
#if defined (DEBUG_MMU)
        printf("%s 5\n", __func__);
        printf("nip=0x%08x LR=0x%08x CTR=0x%08x MSR=0x%08x TBL=0x%08x\n",
               env->nip, env->lr, env->ctr, /*msr*/0, env->tb[0]);
        {
            int  i;
            for (i = 0; i < 32; i++) {
                if ((i & 7) == 0)
                    printf("GPR%02d:", i);
                printf(" %08x", env->gpr[i]);
                if ((i & 7) == 7)
                    printf("\n");
            }
            printf("CR: 0x");
            for (i = 0; i < 8; i++)
                printf("%01x", env->crf[i]);
            printf("  [");
            for (i = 0; i < 8; i++) {
                char a = '-';
                if (env->crf[i] & 0x08)
                    a = 'L';
                else if (env->crf[i] & 0x04)
                    a = 'G';
                else if (env->crf[i] & 0x02)
                    a = 'E';
                printf(" %c%c", a, env->crf[i] & 0x01 ? 'O' : ' ');
            }
            printf(" ] ");
        }
        printf("TB: 0x%08x %08x\n", env->tb[1], env->tb[0]);
        printf("SRR0 0x%08x SRR1 0x%08x\n", env->spr[SRR0], env->spr[SRR1]);
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
                error_code = EXCP_ISI_NOEXEC;
                break;
            case -4:
                /* Direct store exception */
                /* No code fetch is allowed in direct-store areas */
                exception = EXCP_ISI;
                error_code = EXCP_ISI_NOEXEC;
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
                    if (rw)
                        error_code |= EXCP_DSI_STORE;
                    break;
                case ACCESS_EXT:
                    /* eciwx or ecowx */
                    exception = EXCP_DSI;
                    error_code = EXCP_DSI_NOTSUP | EXCP_DSI_DIRECT | EXCP_ECXW;
                    break;
                default:
                    exception = EXCP_PROGRAM;
                    error_code = EXCP_INVAL | EXCP_INVAL_INVAL;
                    break;
                }
            }
            if (rw)
                error_code |= EXCP_DSI_STORE;
            /* Should find a better solution:
             * this will be invalid for some exception if more than one
             * exception occurs for one instruction
             */
            env->spr[DSISR] = 0;
            if (error_code & EXCP_DSI_DIRECT) {
                env->spr[DSISR] |= 0x80000000;
                if (access_type == ACCESS_EXT ||
                    access_type == ACCESS_RES)
                    env->spr[DSISR] |= 0x04000000;
            }
            if ((error_code & 0xF) == EXCP_DSI_TRANSLATE)
                env->spr[DSISR] |= 0x40000000;
            if (error_code & EXCP_DSI_PROT)
                env->spr[DSISR] |= 0x08000000;
            if (error_code & EXCP_DSI_STORE)
                env->spr[DSISR] |= 0x02000000;
            if ((error_code & 0xF) == EXCP_DSI_DABR)
                env->spr[DSISR] |= 0x00400000;
            if (access_type == ACCESS_EXT)
                env->spr[DSISR] |= 0x00100000;
        }
#if 0
        printf("%s: set exception to %d %02x\n",
               __func__, exception, error_code);
#endif
        env->exception_index = exception;
        env->error_code = error_code;
        /* Store fault address */
        env->spr[DAR] = address;
        ret = 1;
    }

    return ret;
}

uint32_t _load_xer (void)
{
    return (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC);
}

void _store_xer (uint32_t value)
{
    xer_so = (value >> XER_SO) & 0x01;
    xer_ov = (value >> XER_OV) & 0x01;
    xer_ca = (value >> XER_CA) & 0x01;
    xer_bc = (value >> XER_BC) & 0x1f;
}

uint32_t _load_msr (void)
{
    return (msr_pow << MSR_POW) |
        (msr_ile << MSR_ILE) |
        (msr_ee << MSR_EE) |
        (msr_pr << MSR_PR) |
        (msr_fp << MSR_FP) |
        (msr_me << MSR_ME) |
        (msr_fe0 << MSR_FE0) |
        (msr_se << MSR_SE) |
        (msr_be << MSR_BE) |
        (msr_fe1 << MSR_FE1) |
        (msr_ip << MSR_IP) |
        (msr_ir << MSR_IR) |
        (msr_dr << MSR_DR) |
        (msr_ri << MSR_RI) |
        (msr_le << MSR_LE);
}

void _store_msr (uint32_t value)
{
    msr_pow = (value >> MSR_POW) & 0x03;
    msr_ile = (value >> MSR_ILE) & 0x01;
    msr_ee = (value >> MSR_EE) & 0x01;
    msr_pr = (value >> MSR_PR) & 0x01;
    msr_fp = (value >> MSR_FP) & 0x01;
    msr_me = (value >> MSR_ME) & 0x01;
    msr_fe0 = (value >> MSR_FE0) & 0x01;
    msr_se = (value >> MSR_SE) & 0x01;
    msr_be = (value >> MSR_BE) & 0x01;
    msr_fe1 = (value >> MSR_FE1) & 0x01;
    msr_ip = (value >> MSR_IP) & 0x01;
    msr_ir = (value >> MSR_IR) & 0x01;
    msr_dr = (value >> MSR_DR) & 0x01;
    msr_ri = (value >> MSR_RI) & 0x01;
    msr_le = (value >> MSR_LE) & 0x01;
}

void do_interrupt (CPUState *env)
{
#if defined (CONFIG_USER_ONLY)
    env->exception_index |= 0x100;
#else
    uint32_t msr;
    int excp = env->exception_index;

    /* Dequeue PPC exceptions */
    if (excp < EXCP_PPC_MAX)
        env->exceptions &= ~(1 << excp);
    msr = _load_msr();
#if defined (DEBUG_EXCEPTIONS)
    if (excp != EXCP_DECR && excp == EXCP_PROGRAM && excp < EXCP_PPC_MAX) 
    {
        if (loglevel > 0) {
            fprintf(logfile, "Raise exception at 0x%08x => 0x%08x (%02x)\n",
                    env->nip, excp << 8, env->error_code);
        } else {
            printf("Raise exception at 0x%08x => 0x%08x (%02x)\n",
                   env->nip, excp << 8, env->error_code);
        }
        printf("nip=0x%08x LR=0x%08x CTR=0x%08x MSR=0x%08x DECR=0x%08x\n",
               env->nip, env->lr, env->ctr, msr, env->decr);
        {
    int i;
            for (i = 0; i < 32; i++) {
                if ((i & 7) == 0)
                    printf("GPR%02d:", i);
                printf(" %08x", env->gpr[i]);
                if ((i & 7) == 7)
                    printf("\n");
    }
            printf("CR: 0x");
    for (i = 0; i < 8; i++)
                printf("%01x", env->crf[i]);
            printf("  [");
            for (i = 0; i < 8; i++) {
                char a = '-';
                if (env->crf[i] & 0x08)
                    a = 'L';
                else if (env->crf[i] & 0x04)
                    a = 'G';
                else if (env->crf[i] & 0x02)
                    a = 'E';
                printf(" %c%c", a, env->crf[i] & 0x01 ? 'O' : ' ');
    }
            printf(" ] ");
    }
        printf("TB: 0x%08x %08x\n", env->tb[1], env->tb[0]);
        printf("XER 0x%08x SRR0 0x%08x SRR1 0x%08x\n",
               _load_xer(), env->spr[SRR0], env->spr[SRR1]);
    }
#endif
    /* Generate informations in save/restore registers */
    switch (excp) {
    case EXCP_OFCALL:
#if defined (USE_OPEN_FIRMWARE)
        env->gpr[3] = OF_client_entry((void *)env->gpr[3]);
#endif
        return;
    case EXCP_RTASCALL:
#if defined (USE_OPEN_FIRMWARE)
        printf("RTAS call !\n");
        env->gpr[3] = RTAS_entry((void *)env->gpr[3]);
        printf("RTAS call done\n");
#endif
        return;
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
            printf("Machine check exception while not allowed !\n");
            if (loglevel) {
                fprintf(logfile,
                        "Machine check exception while not allowed !\n");
        }
            abort();
    }
        msr_me = 0;
        break;
    case EXCP_DSI:
        /* Store exception cause */
        /* data location address has been stored
         * when the fault has been detected
     */
        goto store_current;
    case EXCP_ISI:
        /* Store exception cause */
        if (env->error_code == EXCP_ISI_TRANSLATE)
            msr |= 0x40000000;
        else if (env->error_code == EXCP_ISI_NOEXEC ||
            env->error_code == EXCP_ISI_GUARD)
            msr |= 0x10000000;
        else
            msr |= 0x08000000;
        goto store_next;
    case EXCP_EXTERNAL:
        if (msr_ee == 0) {
#if defined (DEBUG_EXCEPTIONS)
            if (loglevel > 0) {
                fprintf(logfile, "Skipping hardware interrupt\n");
            } else {
                printf("Skipping hardware interrupt\n");
    }
#endif
            return;
            }
        goto store_next;
    case EXCP_ALIGN:
        /* Store exception cause */
        /* Get rS/rD and rA from faulting opcode */
        env->spr[DSISR] |=
            (ldl_code((void *)(env->nip - 4)) & 0x03FF0000) >> 16;
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
        goto store_current;
    case EXCP_DECR:
        if (msr_ee == 0) {
            /* Requeue it */
            do_queue_exception(EXCP_DECR);
            return;
        }
        goto store_next;
    case EXCP_SYSCALL:
#if defined (DEBUG_EXCEPTIONS)
        printf("syscall %d 0x%08x 0x%08x 0x%08x 0x%08x\n",
               env->gpr[0], env->gpr[3], env->gpr[4], env->gpr[5], env->gpr[6]);
#endif
        goto store_next;
    case EXCP_TRACE:
        goto store_next;
    case EXCP_FP_ASSIST:
        goto store_next;
    case EXCP_MTMSR:
        /* Nothing to do */
#if defined (DEBUG_EXCEPTIONS)
        printf("%s: escape EXCP_MTMSR\n", __func__);
#endif
        return;
    case EXCP_BRANCH:
        /* Nothing to do */
#if defined (DEBUG_EXCEPTIONS)
        printf("%s: escape EXCP_BRANCH\n", __func__);
#endif
        return;
    case EXCP_RFI:
        /* Restore user-mode state */
#if defined (DEBUG_EXCEPTIONS)
        printf("%s: escape EXCP_RFI\n", __func__);
#endif
        return;
    store_current:
        /* SRR0 is set to current instruction */
        env->spr[SRR0] = (uint32_t)env->nip - 4;
        break;
    store_next:
        /* SRR0 is set to next instruction */
        env->spr[SRR0] = (uint32_t)env->nip;
        break;
    }
    env->spr[SRR1] = msr;
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
    /* Jump to handler */
    env->nip = excp << 8;
    env->exception_index = EXCP_NONE;
    /* Invalidate all TLB as we may have changed translation mode */
    do_tlbia();
    /* ensure that no TB jump will be modified as
       the program flow was changed */
#ifdef __sparc__
    tmp_T0 = 0;
#else
    T0 = 0;
#endif
#endif
}
