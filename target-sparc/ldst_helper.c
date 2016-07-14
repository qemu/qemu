/*
 * Helpers for loads and stores
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "tcg.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "asi.h"

//#define DEBUG_MMU
//#define DEBUG_MXCC
//#define DEBUG_UNALIGNED
//#define DEBUG_UNASSIGNED
//#define DEBUG_ASI
//#define DEBUG_CACHE_CONTROL

#ifdef DEBUG_MMU
#define DPRINTF_MMU(fmt, ...)                                   \
    do { printf("MMU: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF_MMU(fmt, ...) do {} while (0)
#endif

#ifdef DEBUG_MXCC
#define DPRINTF_MXCC(fmt, ...)                                  \
    do { printf("MXCC: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF_MXCC(fmt, ...) do {} while (0)
#endif

#ifdef DEBUG_ASI
#define DPRINTF_ASI(fmt, ...)                                   \
    do { printf("ASI: " fmt , ## __VA_ARGS__); } while (0)
#endif

#ifdef DEBUG_CACHE_CONTROL
#define DPRINTF_CACHE_CONTROL(fmt, ...)                                 \
    do { printf("CACHE_CONTROL: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF_CACHE_CONTROL(fmt, ...) do {} while (0)
#endif

#ifdef TARGET_SPARC64
#ifndef TARGET_ABI32
#define AM_CHECK(env1) ((env1)->pstate & PS_AM)
#else
#define AM_CHECK(env1) (1)
#endif
#endif

#define QT0 (env->qt0)
#define QT1 (env->qt1)

#if defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY)
/* Calculates TSB pointer value for fault page size 8k or 64k */
static uint64_t ultrasparc_tsb_pointer(uint64_t tsb_register,
                                       uint64_t tag_access_register,
                                       int page_size)
{
    uint64_t tsb_base = tsb_register & ~0x1fffULL;
    int tsb_split = (tsb_register & 0x1000ULL) ? 1 : 0;
    int tsb_size  = tsb_register & 0xf;

    /* discard lower 13 bits which hold tag access context */
    uint64_t tag_access_va = tag_access_register & ~0x1fffULL;

    /* now reorder bits */
    uint64_t tsb_base_mask = ~0x1fffULL;
    uint64_t va = tag_access_va;

    /* move va bits to correct position */
    if (page_size == 8*1024) {
        va >>= 9;
    } else if (page_size == 64*1024) {
        va >>= 12;
    }

    if (tsb_size) {
        tsb_base_mask <<= tsb_size;
    }

    /* calculate tsb_base mask and adjust va if split is in use */
    if (tsb_split) {
        if (page_size == 8*1024) {
            va &= ~(1ULL << (13 + tsb_size));
        } else if (page_size == 64*1024) {
            va |= (1ULL << (13 + tsb_size));
        }
        tsb_base_mask <<= 1;
    }

    return ((tsb_base & tsb_base_mask) | (va & ~tsb_base_mask)) & ~0xfULL;
}

/* Calculates tag target register value by reordering bits
   in tag access register */
static uint64_t ultrasparc_tag_target(uint64_t tag_access_register)
{
    return ((tag_access_register & 0x1fff) << 48) | (tag_access_register >> 22);
}

static void replace_tlb_entry(SparcTLBEntry *tlb,
                              uint64_t tlb_tag, uint64_t tlb_tte,
                              CPUSPARCState *env1)
{
    target_ulong mask, size, va, offset;

    /* flush page range if translation is valid */
    if (TTE_IS_VALID(tlb->tte)) {
        CPUState *cs = CPU(sparc_env_get_cpu(env1));

        mask = 0xffffffffffffe000ULL;
        mask <<= 3 * ((tlb->tte >> 61) & 3);
        size = ~mask + 1;

        va = tlb->tag & mask;

        for (offset = 0; offset < size; offset += TARGET_PAGE_SIZE) {
            tlb_flush_page(cs, va + offset);
        }
    }

    tlb->tag = tlb_tag;
    tlb->tte = tlb_tte;
}

static void demap_tlb(SparcTLBEntry *tlb, target_ulong demap_addr,
                      const char *strmmu, CPUSPARCState *env1)
{
    unsigned int i;
    target_ulong mask;
    uint64_t context;

    int is_demap_context = (demap_addr >> 6) & 1;

    /* demap context */
    switch ((demap_addr >> 4) & 3) {
    case 0: /* primary */
        context = env1->dmmu.mmu_primary_context;
        break;
    case 1: /* secondary */
        context = env1->dmmu.mmu_secondary_context;
        break;
    case 2: /* nucleus */
        context = 0;
        break;
    case 3: /* reserved */
    default:
        return;
    }

    for (i = 0; i < 64; i++) {
        if (TTE_IS_VALID(tlb[i].tte)) {

            if (is_demap_context) {
                /* will remove non-global entries matching context value */
                if (TTE_IS_GLOBAL(tlb[i].tte) ||
                    !tlb_compare_context(&tlb[i], context)) {
                    continue;
                }
            } else {
                /* demap page
                   will remove any entry matching VA */
                mask = 0xffffffffffffe000ULL;
                mask <<= 3 * ((tlb[i].tte >> 61) & 3);

                if (!compare_masked(demap_addr, tlb[i].tag, mask)) {
                    continue;
                }

                /* entry should be global or matching context value */
                if (!TTE_IS_GLOBAL(tlb[i].tte) &&
                    !tlb_compare_context(&tlb[i], context)) {
                    continue;
                }
            }

            replace_tlb_entry(&tlb[i], 0, 0, env1);
#ifdef DEBUG_MMU
            DPRINTF_MMU("%s demap invalidated entry [%02u]\n", strmmu, i);
            dump_mmu(stdout, fprintf, env1);
#endif
        }
    }
}

static void replace_tlb_1bit_lru(SparcTLBEntry *tlb,
                                 uint64_t tlb_tag, uint64_t tlb_tte,
                                 const char *strmmu, CPUSPARCState *env1)
{
    unsigned int i, replace_used;

    /* Try replacing invalid entry */
    for (i = 0; i < 64; i++) {
        if (!TTE_IS_VALID(tlb[i].tte)) {
            replace_tlb_entry(&tlb[i], tlb_tag, tlb_tte, env1);
#ifdef DEBUG_MMU
            DPRINTF_MMU("%s lru replaced invalid entry [%i]\n", strmmu, i);
            dump_mmu(stdout, fprintf, env1);
#endif
            return;
        }
    }

    /* All entries are valid, try replacing unlocked entry */

    for (replace_used = 0; replace_used < 2; ++replace_used) {

        /* Used entries are not replaced on first pass */

        for (i = 0; i < 64; i++) {
            if (!TTE_IS_LOCKED(tlb[i].tte) && !TTE_IS_USED(tlb[i].tte)) {

                replace_tlb_entry(&tlb[i], tlb_tag, tlb_tte, env1);
#ifdef DEBUG_MMU
                DPRINTF_MMU("%s lru replaced unlocked %s entry [%i]\n",
                            strmmu, (replace_used ? "used" : "unused"), i);
                dump_mmu(stdout, fprintf, env1);
#endif
                return;
            }
        }

        /* Now reset used bit and search for unused entries again */

        for (i = 0; i < 64; i++) {
            TTE_SET_UNUSED(tlb[i].tte);
        }
    }

#ifdef DEBUG_MMU
    DPRINTF_MMU("%s lru replacement failed: no entries available\n", strmmu);
#endif
    /* error state? */
}

#endif

#if defined(TARGET_SPARC64) || defined(CONFIG_USER_ONLY)
static inline target_ulong address_mask(CPUSPARCState *env1, target_ulong addr)
{
#ifdef TARGET_SPARC64
    if (AM_CHECK(env1)) {
        addr &= 0xffffffffULL;
    }
#endif
    return addr;
}
#endif

#ifdef TARGET_SPARC64
/* returns true if access using this ASI is to have address translated by MMU
   otherwise access is to raw physical address */
/* TODO: check sparc32 bits */
static inline int is_translating_asi(int asi)
{
    /* Ultrasparc IIi translating asi
       - note this list is defined by cpu implementation
    */
    switch (asi) {
    case 0x04 ... 0x11:
    case 0x16 ... 0x19:
    case 0x1E ... 0x1F:
    case 0x24 ... 0x2C:
    case 0x70 ... 0x73:
    case 0x78 ... 0x79:
    case 0x80 ... 0xFF:
        return 1;

    default:
        return 0;
    }
}

static inline target_ulong asi_address_mask(CPUSPARCState *env,
                                            int asi, target_ulong addr)
{
    if (is_translating_asi(asi)) {
        return address_mask(env, addr);
    } else {
        return addr;
    }
}
#endif

void helper_check_align(CPUSPARCState *env, target_ulong addr, uint32_t align)
{
    if (addr & align) {
#ifdef DEBUG_UNALIGNED
        printf("Unaligned access to 0x" TARGET_FMT_lx " from 0x" TARGET_FMT_lx
               "\n", addr, env->pc);
#endif
        helper_raise_exception(env, TT_UNALIGNED);
    }
}

#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY) &&   \
    defined(DEBUG_MXCC)
static void dump_mxcc(CPUSPARCState *env)
{
    printf("mxccdata: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
           "\n",
           env->mxccdata[0], env->mxccdata[1],
           env->mxccdata[2], env->mxccdata[3]);
    printf("mxccregs: %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
           "\n"
           "          %016" PRIx64 " %016" PRIx64 " %016" PRIx64 " %016" PRIx64
           "\n",
           env->mxccregs[0], env->mxccregs[1],
           env->mxccregs[2], env->mxccregs[3],
           env->mxccregs[4], env->mxccregs[5],
           env->mxccregs[6], env->mxccregs[7]);
}
#endif

#if (defined(TARGET_SPARC64) || !defined(CONFIG_USER_ONLY))     \
    && defined(DEBUG_ASI)
static void dump_asi(const char *txt, target_ulong addr, int asi, int size,
                     uint64_t r1)
{
    switch (size) {
    case 1:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %02" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xff);
        break;
    case 2:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %04" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffff);
        break;
    case 4:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %08" PRIx64 "\n", txt,
                    addr, asi, r1 & 0xffffffff);
        break;
    case 8:
        DPRINTF_ASI("%s "TARGET_FMT_lx " asi 0x%02x = %016" PRIx64 "\n", txt,
                    addr, asi, r1);
        break;
    }
}
#endif

#ifndef TARGET_SPARC64
#ifndef CONFIG_USER_ONLY


/* Leon3 cache control */

static void leon3_cache_control_st(CPUSPARCState *env, target_ulong addr,
                                   uint64_t val, int size)
{
    DPRINTF_CACHE_CONTROL("st addr:%08x, val:%" PRIx64 ", size:%d\n",
                          addr, val, size);

    if (size != 4) {
        DPRINTF_CACHE_CONTROL("32bits only\n");
        return;
    }

    switch (addr) {
    case 0x00:              /* Cache control */

        /* These values must always be read as zeros */
        val &= ~CACHE_CTRL_FD;
        val &= ~CACHE_CTRL_FI;
        val &= ~CACHE_CTRL_IB;
        val &= ~CACHE_CTRL_IP;
        val &= ~CACHE_CTRL_DP;

        env->cache_control = val;
        break;
    case 0x04:              /* Instruction cache configuration */
    case 0x08:              /* Data cache configuration */
        /* Read Only */
        break;
    default:
        DPRINTF_CACHE_CONTROL("write unknown register %08x\n", addr);
        break;
    };
}

static uint64_t leon3_cache_control_ld(CPUSPARCState *env, target_ulong addr,
                                       int size)
{
    uint64_t ret = 0;

    if (size != 4) {
        DPRINTF_CACHE_CONTROL("32bits only\n");
        return 0;
    }

    switch (addr) {
    case 0x00:              /* Cache control */
        ret = env->cache_control;
        break;

        /* Configuration registers are read and only always keep those
           predefined values */

    case 0x04:              /* Instruction cache configuration */
        ret = 0x10220000;
        break;
    case 0x08:              /* Data cache configuration */
        ret = 0x18220000;
        break;
    default:
        DPRINTF_CACHE_CONTROL("read unknown register %08x\n", addr);
        break;
    };
    DPRINTF_CACHE_CONTROL("ld addr:%08x, ret:0x%" PRIx64 ", size:%d\n",
                          addr, ret, size);
    return ret;
}

uint64_t helper_ld_asi(CPUSPARCState *env, target_ulong addr,
                       int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    int sign = memop & MO_SIGN;
    CPUState *cs = CPU(sparc_env_get_cpu(env));
    uint64_t ret = 0;
#if defined(DEBUG_MXCC) || defined(DEBUG_ASI)
    uint32_t last_addr = addr;
#endif

    helper_check_align(env, addr, size - 1);
    switch (asi) {
    case ASI_M_MXCC: /* SuperSparc MXCC registers, or... */
    /* case ASI_LEON_CACHEREGS:  Leon3 cache control */
        switch (addr) {
        case 0x00:          /* Leon3 Cache Control */
        case 0x08:          /* Leon3 Instruction Cache config */
        case 0x0C:          /* Leon3 Date Cache config */
            if (env->def->features & CPU_FEATURE_CACHE_CTRL) {
                ret = leon3_cache_control_ld(env, addr, size);
            }
            break;
        case 0x01c00a00: /* MXCC control register */
            if (size == 8) {
                ret = env->mxccregs[3];
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4) {
                ret = env->mxccregs[3];
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00c00: /* Module reset register */
            if (size == 8) {
                ret = env->mxccregs[5];
                /* should we do something here? */
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8) {
                ret = env->mxccregs[7];
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "%08x: unimplemented address, size: %d\n", addr,
                          size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, sign = %d, "
                     "addr = %08x -> ret = %" PRIx64 ","
                     "addr = %08x\n", asi, size, sign, last_addr, ret, addr);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case ASI_M_FLUSH_PROBE: /* SuperSparc MMU probe */
    case ASI_LEON_MMUFLUSH: /* LEON3 MMU probe */
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            if (mmulev > 4) {
                ret = 0;
            } else {
                ret = mmu_probe(env, addr, mmulev);
            }
            DPRINTF_MMU("mmu_probe: 0x%08x (lev %d) -> 0x%08" PRIx64 "\n",
                        addr, mmulev, ret);
        }
        break;
    case ASI_M_MMUREGS: /* SuperSparc MMU regs */
    case ASI_LEON_MMUREGS: /* LEON3 MMU regs */
        {
            int reg = (addr >> 8) & 0x1f;

            ret = env->mmuregs[reg];
            if (reg == 3) { /* Fault status cleared on read */
                env->mmuregs[3] = 0;
            } else if (reg == 0x13) { /* Fault status read */
                ret = env->mmuregs[3];
            } else if (reg == 0x14) { /* Fault address read */
                ret = env->mmuregs[4];
            }
            DPRINTF_MMU("mmu_read: reg[%d] = 0x%08" PRIx64 "\n", reg, ret);
        }
        break;
    case ASI_M_TLBDIAG: /* Turbosparc ITLB Diagnostic */
    case ASI_M_DIAGS:   /* Turbosparc DTLB Diagnostic */
    case ASI_M_IODIAG:  /* Turbosparc IOTLB Diagnostic */
        break;
    case ASI_KERNELTXT: /* Supervisor code access */
        switch (size) {
        case 1:
            ret = cpu_ldub_code(env, addr);
            break;
        case 2:
            ret = cpu_lduw_code(env, addr);
            break;
        default:
        case 4:
            ret = cpu_ldl_code(env, addr);
            break;
        case 8:
            ret = cpu_ldq_code(env, addr);
            break;
        }
        break;
    case ASI_USERDATA: /* User data access */
        switch (size) {
        case 1:
            ret = cpu_ldub_user(env, addr);
            break;
        case 2:
            ret = cpu_lduw_user(env, addr);
            break;
        default:
        case 4:
            ret = cpu_ldl_user(env, addr);
            break;
        case 8:
            ret = cpu_ldq_user(env, addr);
            break;
        }
        break;
    case ASI_KERNELDATA: /* Supervisor data access */
    case ASI_P: /* Implicit primary context data access (v9 only?) */
        switch (size) {
        case 1:
            ret = cpu_ldub_kernel(env, addr);
            break;
        case 2:
            ret = cpu_lduw_kernel(env, addr);
            break;
        default:
        case 4:
            ret = cpu_ldl_kernel(env, addr);
            break;
        case 8:
            ret = cpu_ldq_kernel(env, addr);
            break;
        }
        break;
    case ASI_M_TXTC_TAG:   /* SparcStation 5 I-cache tag */
    case ASI_M_TXTC_DATA:  /* SparcStation 5 I-cache data */
    case ASI_M_DATAC_TAG:  /* SparcStation 5 D-cache tag */
    case ASI_M_DATAC_DATA: /* SparcStation 5 D-cache data */
        break;
    case ASI_M_BYPASS:    /* MMU passthrough */
    case ASI_LEON_BYPASS: /* LEON MMU passthrough */
        switch (size) {
        case 1:
            ret = ldub_phys(cs->as, addr);
            break;
        case 2:
            ret = lduw_phys(cs->as, addr);
            break;
        default:
        case 4:
            ret = ldl_phys(cs->as, addr);
            break;
        case 8:
            ret = ldq_phys(cs->as, addr);
            break;
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        switch (size) {
        case 1:
            ret = ldub_phys(cs->as, (hwaddr)addr
                            | ((hwaddr)(asi & 0xf) << 32));
            break;
        case 2:
            ret = lduw_phys(cs->as, (hwaddr)addr
                            | ((hwaddr)(asi & 0xf) << 32));
            break;
        default:
        case 4:
            ret = ldl_phys(cs->as, (hwaddr)addr
                           | ((hwaddr)(asi & 0xf) << 32));
            break;
        case 8:
            ret = ldq_phys(cs->as, (hwaddr)addr
                           | ((hwaddr)(asi & 0xf) << 32));
            break;
        }
        break;
    case 0x30: /* Turbosparc secondary cache diagnostic */
    case 0x31: /* Turbosparc RAM snoop */
    case 0x32: /* Turbosparc page table descriptor diagnostic */
    case 0x39: /* data cache diagnostic register */
        ret = 0;
        break;
    case 0x38: /* SuperSPARC MMU Breakpoint Control Registers */
        {
            int reg = (addr >> 8) & 3;

            switch (reg) {
            case 0: /* Breakpoint Value (Addr) */
                ret = env->mmubpregs[reg];
                break;
            case 1: /* Breakpoint Mask */
                ret = env->mmubpregs[reg];
                break;
            case 2: /* Breakpoint Control */
                ret = env->mmubpregs[reg];
                break;
            case 3: /* Breakpoint Status */
                ret = env->mmubpregs[reg];
                env->mmubpregs[reg] = 0ULL;
                break;
            }
            DPRINTF_MMU("read breakpoint reg[%d] 0x%016" PRIx64 "\n", reg,
                        ret);
        }
        break;
    case 0x49: /* SuperSPARC MMU Counter Breakpoint Value */
        ret = env->mmubpctrv;
        break;
    case 0x4a: /* SuperSPARC MMU Counter Breakpoint Control */
        ret = env->mmubpctrc;
        break;
    case 0x4b: /* SuperSPARC MMU Counter Breakpoint Status */
        ret = env->mmubpctrs;
        break;
    case 0x4c: /* SuperSPARC MMU Breakpoint Action */
        ret = env->mmubpaction;
        break;
    case ASI_USERTXT: /* User code access, XXX */
    default:
        cpu_unassigned_access(cs, addr, false, false, asi, size);
        ret = 0;
        break;
    }
    if (sign) {
        switch (size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(CPUSPARCState *env, target_ulong addr, uint64_t val,
                   int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    SPARCCPU *cpu = sparc_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    helper_check_align(env, addr, size - 1);
    switch (asi) {
    case ASI_M_MXCC: /* SuperSparc MXCC registers, or... */
    /* case ASI_LEON_CACHEREGS:  Leon3 cache control */
        switch (addr) {
        case 0x00:          /* Leon3 Cache Control */
        case 0x08:          /* Leon3 Instruction Cache config */
        case 0x0C:          /* Leon3 Date Cache config */
            if (env->def->features & CPU_FEATURE_CACHE_CTRL) {
                leon3_cache_control_st(env, addr, val, size);
            }
            break;

        case 0x01c00000: /* MXCC stream data register 0 */
            if (size == 8) {
                env->mxccdata[0] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00008: /* MXCC stream data register 1 */
            if (size == 8) {
                env->mxccdata[1] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00010: /* MXCC stream data register 2 */
            if (size == 8) {
                env->mxccdata[2] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00018: /* MXCC stream data register 3 */
            if (size == 8) {
                env->mxccdata[3] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00100: /* MXCC stream source */
            if (size == 8) {
                env->mxccregs[0] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            env->mxccdata[0] = ldq_phys(cs->as,
                                        (env->mxccregs[0] & 0xffffffffULL) +
                                        0);
            env->mxccdata[1] = ldq_phys(cs->as,
                                        (env->mxccregs[0] & 0xffffffffULL) +
                                        8);
            env->mxccdata[2] = ldq_phys(cs->as,
                                        (env->mxccregs[0] & 0xffffffffULL) +
                                        16);
            env->mxccdata[3] = ldq_phys(cs->as,
                                        (env->mxccregs[0] & 0xffffffffULL) +
                                        24);
            break;
        case 0x01c00200: /* MXCC stream destination */
            if (size == 8) {
                env->mxccregs[1] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            stq_phys(cs->as, (env->mxccregs[1] & 0xffffffffULL) +  0,
                     env->mxccdata[0]);
            stq_phys(cs->as, (env->mxccregs[1] & 0xffffffffULL) +  8,
                     env->mxccdata[1]);
            stq_phys(cs->as, (env->mxccregs[1] & 0xffffffffULL) + 16,
                     env->mxccdata[2]);
            stq_phys(cs->as, (env->mxccregs[1] & 0xffffffffULL) + 24,
                     env->mxccdata[3]);
            break;
        case 0x01c00a00: /* MXCC control register */
            if (size == 8) {
                env->mxccregs[3] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4) {
                env->mxccregs[3] = (env->mxccregs[3] & 0xffffffff00000000ULL)
                    | val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00e00: /* MXCC error register  */
            /* writing a 1 bit clears the error */
            if (size == 8) {
                env->mxccregs[6] &= ~val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8) {
                env->mxccregs[7] = val;
            } else {
                qemu_log_mask(LOG_UNIMP,
                              "%08x: unimplemented access size: %d\n", addr,
                              size);
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                          "%08x: unimplemented address, size: %d\n", addr,
                          size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, addr = %08x, val = %" PRIx64 "\n",
                     asi, size, addr, val);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case ASI_M_FLUSH_PROBE: /* SuperSparc MMU flush */
    case ASI_LEON_MMUFLUSH: /* LEON3 MMU flush */
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            DPRINTF_MMU("mmu flush level %d\n", mmulev);
            switch (mmulev) {
            case 0: /* flush page */
                tlb_flush_page(CPU(cpu), addr & 0xfffff000);
                break;
            case 1: /* flush segment (256k) */
            case 2: /* flush region (16M) */
            case 3: /* flush context (4G) */
            case 4: /* flush entire */
                tlb_flush(CPU(cpu), 1);
                break;
            default:
                break;
            }
#ifdef DEBUG_MMU
            dump_mmu(stdout, fprintf, env);
#endif
        }
        break;
    case ASI_M_MMUREGS: /* write MMU regs */
    case ASI_LEON_MMUREGS: /* LEON3 write MMU regs */
        {
            int reg = (addr >> 8) & 0x1f;
            uint32_t oldreg;

            oldreg = env->mmuregs[reg];
            switch (reg) {
            case 0: /* Control Register */
                env->mmuregs[reg] = (env->mmuregs[reg] & 0xff000000) |
                    (val & 0x00ffffff);
                /* Mappings generated during no-fault mode or MMU
                   disabled mode are invalid in normal mode */
                if ((oldreg & (MMU_E | MMU_NF | env->def->mmu_bm)) !=
                    (env->mmuregs[reg] & (MMU_E | MMU_NF | env->def->mmu_bm))) {
                    tlb_flush(CPU(cpu), 1);
                }
                break;
            case 1: /* Context Table Pointer Register */
                env->mmuregs[reg] = val & env->def->mmu_ctpr_mask;
                break;
            case 2: /* Context Register */
                env->mmuregs[reg] = val & env->def->mmu_cxr_mask;
                if (oldreg != env->mmuregs[reg]) {
                    /* we flush when the MMU context changes because
                       QEMU has no MMU context support */
                    tlb_flush(CPU(cpu), 1);
                }
                break;
            case 3: /* Synchronous Fault Status Register with Clear */
            case 4: /* Synchronous Fault Address Register */
                break;
            case 0x10: /* TLB Replacement Control Register */
                env->mmuregs[reg] = val & env->def->mmu_trcr_mask;
                break;
            case 0x13: /* Synchronous Fault Status Register with Read
                          and Clear */
                env->mmuregs[3] = val & env->def->mmu_sfsr_mask;
                break;
            case 0x14: /* Synchronous Fault Address Register */
                env->mmuregs[4] = val;
                break;
            default:
                env->mmuregs[reg] = val;
                break;
            }
            if (oldreg != env->mmuregs[reg]) {
                DPRINTF_MMU("mmu change reg[%d]: 0x%08x -> 0x%08x\n",
                            reg, oldreg, env->mmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(stdout, fprintf, env);
#endif
        }
        break;
    case ASI_M_TLBDIAG: /* Turbosparc ITLB Diagnostic */
    case ASI_M_DIAGS:   /* Turbosparc DTLB Diagnostic */
    case ASI_M_IODIAG:  /* Turbosparc IOTLB Diagnostic */
        break;
    case ASI_USERDATA: /* User data access */
        switch (size) {
        case 1:
            cpu_stb_user(env, addr, val);
            break;
        case 2:
            cpu_stw_user(env, addr, val);
            break;
        default:
        case 4:
            cpu_stl_user(env, addr, val);
            break;
        case 8:
            cpu_stq_user(env, addr, val);
            break;
        }
        break;
    case ASI_KERNELDATA: /* Supervisor data access */
    case ASI_P:
        switch (size) {
        case 1:
            cpu_stb_kernel(env, addr, val);
            break;
        case 2:
            cpu_stw_kernel(env, addr, val);
            break;
        default:
        case 4:
            cpu_stl_kernel(env, addr, val);
            break;
        case 8:
            cpu_stq_kernel(env, addr, val);
            break;
        }
        break;
    case ASI_M_TXTC_TAG:   /* I-cache tag */
    case ASI_M_TXTC_DATA:  /* I-cache data */
    case ASI_M_DATAC_TAG:  /* D-cache tag */
    case ASI_M_DATAC_DATA: /* D-cache data */
    case ASI_M_FLUSH_PAGE:   /* I/D-cache flush page */
    case ASI_M_FLUSH_SEG:    /* I/D-cache flush segment */
    case ASI_M_FLUSH_REGION: /* I/D-cache flush region */
    case ASI_M_FLUSH_CTX:    /* I/D-cache flush context */
    case ASI_M_FLUSH_USER:   /* I/D-cache flush user */
        break;
    case ASI_M_BCOPY: /* Block copy, sta access */
        {
            /* val = src
               addr = dst
               copy 32 bytes */
            unsigned int i;
            uint32_t src = val & ~3, dst = addr & ~3, temp;

            for (i = 0; i < 32; i += 4, src += 4, dst += 4) {
                temp = cpu_ldl_kernel(env, src);
                cpu_stl_kernel(env, dst, temp);
            }
        }
        break;
    case ASI_M_BFILL: /* Block fill, stda access */
        {
            /* addr = dst
               fill 32 bytes with val */
            unsigned int i;
            uint32_t dst = addr & ~7;

            for (i = 0; i < 32; i += 8, dst += 8) {
                cpu_stq_kernel(env, dst, val);
            }
        }
        break;
    case ASI_M_BYPASS:    /* MMU passthrough */
    case ASI_LEON_BYPASS: /* LEON MMU passthrough */
        {
            switch (size) {
            case 1:
                stb_phys(cs->as, addr, val);
                break;
            case 2:
                stw_phys(cs->as, addr, val);
                break;
            case 4:
            default:
                stl_phys(cs->as, addr, val);
                break;
            case 8:
                stq_phys(cs->as, addr, val);
                break;
            }
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        {
            switch (size) {
            case 1:
                stb_phys(cs->as, (hwaddr)addr
                         | ((hwaddr)(asi & 0xf) << 32), val);
                break;
            case 2:
                stw_phys(cs->as, (hwaddr)addr
                         | ((hwaddr)(asi & 0xf) << 32), val);
                break;
            case 4:
            default:
                stl_phys(cs->as, (hwaddr)addr
                         | ((hwaddr)(asi & 0xf) << 32), val);
                break;
            case 8:
                stq_phys(cs->as, (hwaddr)addr
                         | ((hwaddr)(asi & 0xf) << 32), val);
                break;
            }
        }
        break;
    case 0x30: /* store buffer tags or Turbosparc secondary cache diagnostic */
    case 0x31: /* store buffer data, Ross RT620 I-cache flush or
                  Turbosparc snoop RAM */
    case 0x32: /* store buffer control or Turbosparc page table
                  descriptor diagnostic */
    case 0x36: /* I-cache flash clear */
    case 0x37: /* D-cache flash clear */
        break;
    case 0x38: /* SuperSPARC MMU Breakpoint Control Registers*/
        {
            int reg = (addr >> 8) & 3;

            switch (reg) {
            case 0: /* Breakpoint Value (Addr) */
                env->mmubpregs[reg] = (val & 0xfffffffffULL);
                break;
            case 1: /* Breakpoint Mask */
                env->mmubpregs[reg] = (val & 0xfffffffffULL);
                break;
            case 2: /* Breakpoint Control */
                env->mmubpregs[reg] = (val & 0x7fULL);
                break;
            case 3: /* Breakpoint Status */
                env->mmubpregs[reg] = (val & 0xfULL);
                break;
            }
            DPRINTF_MMU("write breakpoint reg[%d] 0x%016x\n", reg,
                        env->mmuregs[reg]);
        }
        break;
    case 0x49: /* SuperSPARC MMU Counter Breakpoint Value */
        env->mmubpctrv = val & 0xffffffff;
        break;
    case 0x4a: /* SuperSPARC MMU Counter Breakpoint Control */
        env->mmubpctrc = val & 0x3;
        break;
    case 0x4b: /* SuperSPARC MMU Counter Breakpoint Status */
        env->mmubpctrs = val & 0x3;
        break;
    case 0x4c: /* SuperSPARC MMU Breakpoint Action */
        env->mmubpaction = val & 0x1fff;
        break;
    case ASI_USERTXT: /* User code access, XXX */
    case ASI_KERNELTXT: /* Supervisor code access, XXX */
    default:
        cpu_unassigned_access(CPU(sparc_env_get_cpu(env)),
                              addr, true, false, asi, size);
        break;
    }
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
}

#endif /* CONFIG_USER_ONLY */
#else /* TARGET_SPARC64 */

#ifdef CONFIG_USER_ONLY
uint64_t helper_ld_asi(CPUSPARCState *env, target_ulong addr,
                       int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    int sign = memop & MO_SIGN;
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    if (asi < 0x80) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    helper_check_align(env, addr, size - 1);
    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
    case ASI_PNF:  /* Primary no-fault */
    case ASI_PNFL: /* Primary no-fault LE */
        if (page_check_range(addr, size, PAGE_READ) == -1) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        /* Fall through */
    case ASI_P: /* Primary */
    case ASI_PL: /* Primary LE */
        {
            switch (size) {
            case 1:
                ret = cpu_ldub_data(env, addr);
                break;
            case 2:
                ret = cpu_lduw_data(env, addr);
                break;
            case 4:
                ret = cpu_ldl_data(env, addr);
                break;
            default:
            case 8:
                ret = cpu_ldq_data(env, addr);
                break;
            }
        }
        break;
    case ASI_SNF:  /* Secondary no-fault */
    case ASI_SNFL: /* Secondary no-fault LE */
        if (page_check_range(addr, size, PAGE_READ) == -1) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        /* Fall through */
    case ASI_S:  /* Secondary */
    case ASI_SL: /* Secondary LE */
        /* XXX */
        break;
    default:
        break;
    }

    /* Convert from little endian */
    switch (asi) {
    case ASI_PL:   /* Primary LE */
    case ASI_SL:   /* Secondary LE */
    case ASI_PNFL: /* Primary no-fault LE */
    case ASI_SNFL: /* Secondary no-fault LE */
        switch (size) {
        case 2:
            ret = bswap16(ret);
            break;
        case 4:
            ret = bswap32(ret);
            break;
        case 8:
            ret = bswap64(ret);
            break;
        default:
            break;
        }
    default:
        break;
    }

    /* Convert to signed number */
    if (sign) {
        switch (size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(CPUSPARCState *env, target_ulong addr, target_ulong val,
                   int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
    if (asi < 0x80) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    helper_check_align(env, addr, size - 1);
    addr = asi_address_mask(env, asi, addr);

    /* Convert to little endian */
    switch (asi) {
    case ASI_PL: /* Primary LE */
    case ASI_SL: /* Secondary LE */
        switch (size) {
        case 2:
            val = bswap16(val);
            break;
        case 4:
            val = bswap32(val);
            break;
        case 8:
            val = bswap64(val);
            break;
        default:
            break;
        }
    default:
        break;
    }

    switch (asi) {
    case ASI_P:  /* Primary */
    case ASI_PL: /* Primary LE */
        {
            switch (size) {
            case 1:
                cpu_stb_data(env, addr, val);
                break;
            case 2:
                cpu_stw_data(env, addr, val);
                break;
            case 4:
                cpu_stl_data(env, addr, val);
                break;
            case 8:
            default:
                cpu_stq_data(env, addr, val);
                break;
            }
        }
        break;
    case ASI_S:  /* Secondary */
    case ASI_SL: /* Secondary LE */
        /* XXX */
        return;

    case ASI_PNF:  /* Primary no-fault, RO */
    case ASI_SNF:  /* Secondary no-fault, RO */
    case ASI_PNFL: /* Primary no-fault LE, RO */
    case ASI_SNFL: /* Secondary no-fault LE, RO */
    default:
        helper_raise_exception(env, TT_DATA_ACCESS);
        return;
    }
}

#else /* CONFIG_USER_ONLY */

uint64_t helper_ld_asi(CPUSPARCState *env, target_ulong addr,
                       int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    int sign = memop & MO_SIGN;
    CPUState *cs = CPU(sparc_env_get_cpu(env));
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    asi &= 0xff;

    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || (cpu_has_hypervisor(env)
            && asi >= 0x30 && asi < 0x80
            && !(env->hpstate & HS_PRIV))) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    helper_check_align(env, addr, size - 1);
    addr = asi_address_mask(env, asi, addr);

    /* process nonfaulting loads first */
    if ((asi & 0xf6) == 0x82) {
        int mmu_idx;

        /* secondary space access has lowest asi bit equal to 1 */
        if (env->pstate & PS_PRIV) {
            mmu_idx = (asi & 1) ? MMU_KERNEL_SECONDARY_IDX : MMU_KERNEL_IDX;
        } else {
            mmu_idx = (asi & 1) ? MMU_USER_SECONDARY_IDX : MMU_USER_IDX;
        }

        if (cpu_get_phys_page_nofault(env, addr, mmu_idx) == -1ULL) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            /* env->exception_index is set in get_physical_address_data(). */
            helper_raise_exception(env, cs->exception_index);
        }

        /* convert nonfaulting load ASIs to normal load ASIs */
        asi &= ~0x02;
    }

    switch (asi) {
    case ASI_AIUP:  /* As if user primary */
    case ASI_AIUS:  /* As if user secondary */
    case ASI_AIUPL: /* As if user primary LE */
    case ASI_AIUSL: /* As if user secondary LE */
    case ASI_P:  /* Primary */
    case ASI_S:  /* Secondary */
    case ASI_PL: /* Primary LE */
    case ASI_SL: /* Secondary LE */
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if (cpu_hypervisor_mode(env)) {
                switch (size) {
                case 1:
                    ret = cpu_ldub_hypv(env, addr);
                    break;
                case 2:
                    ret = cpu_lduw_hypv(env, addr);
                    break;
                case 4:
                    ret = cpu_ldl_hypv(env, addr);
                    break;
                default:
                case 8:
                    ret = cpu_ldq_hypv(env, addr);
                    break;
                }
            } else {
                /* secondary space access has lowest asi bit equal to 1 */
                if (asi & 1) {
                    switch (size) {
                    case 1:
                        ret = cpu_ldub_kernel_secondary(env, addr);
                        break;
                    case 2:
                        ret = cpu_lduw_kernel_secondary(env, addr);
                        break;
                    case 4:
                        ret = cpu_ldl_kernel_secondary(env, addr);
                        break;
                    default:
                    case 8:
                        ret = cpu_ldq_kernel_secondary(env, addr);
                        break;
                    }
                } else {
                    switch (size) {
                    case 1:
                        ret = cpu_ldub_kernel(env, addr);
                        break;
                    case 2:
                        ret = cpu_lduw_kernel(env, addr);
                        break;
                    case 4:
                        ret = cpu_ldl_kernel(env, addr);
                        break;
                    default:
                    case 8:
                        ret = cpu_ldq_kernel(env, addr);
                        break;
                    }
                }
            }
        } else {
            /* secondary space access has lowest asi bit equal to 1 */
            if (asi & 1) {
                switch (size) {
                case 1:
                    ret = cpu_ldub_user_secondary(env, addr);
                    break;
                case 2:
                    ret = cpu_lduw_user_secondary(env, addr);
                    break;
                case 4:
                    ret = cpu_ldl_user_secondary(env, addr);
                    break;
                default:
                case 8:
                    ret = cpu_ldq_user_secondary(env, addr);
                    break;
                }
            } else {
                switch (size) {
                case 1:
                    ret = cpu_ldub_user(env, addr);
                    break;
                case 2:
                    ret = cpu_lduw_user(env, addr);
                    break;
                case 4:
                    ret = cpu_ldl_user(env, addr);
                    break;
                default:
                case 8:
                    ret = cpu_ldq_user(env, addr);
                    break;
                }
            }
        }
        break;
    case ASI_REAL:        /* Bypass */
    case ASI_REAL_IO:   /* Bypass, non-cacheable */
    case ASI_REAL_L:      /* Bypass LE */
    case ASI_REAL_IO_L: /* Bypass, non-cacheable LE */
        {
            switch (size) {
            case 1:
                ret = ldub_phys(cs->as, addr);
                break;
            case 2:
                ret = lduw_phys(cs->as, addr);
                break;
            case 4:
                ret = ldl_phys(cs->as, addr);
                break;
            default:
            case 8:
                ret = ldq_phys(cs->as, addr);
                break;
            }
            break;
        }
    case ASI_N:  /* Nucleus */
    case ASI_NL: /* Nucleus Little Endian (LE) */
        {
            switch (size) {
            case 1:
                ret = cpu_ldub_nucleus(env, addr);
                break;
            case 2:
                ret = cpu_lduw_nucleus(env, addr);
                break;
            case 4:
                ret = cpu_ldl_nucleus(env, addr);
                break;
            default:
            case 8:
                ret = cpu_ldq_nucleus(env, addr);
                break;
            }
            break;
        }
    case ASI_UPA_CONFIG: /* UPA config */
        /* XXX */
        break;
    case ASI_LSU_CONTROL: /* LSU */
        ret = env->lsu;
        break;
    case ASI_IMMU: /* I-MMU regs */
        {
            int reg = (addr >> 3) & 0xf;

            if (reg == 0) {
                /* I-TSB Tag Target register */
                ret = ultrasparc_tag_target(env->immu.tag_access);
            } else {
                ret = env->immuregs[reg];
            }

            break;
        }
    case ASI_IMMU_TSB_8KB_PTR: /* I-MMU 8k TSB pointer */
        {
            /* env->immuregs[5] holds I-MMU TSB register value
               env->immuregs[6] holds I-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->immu.tsb, env->immu.tag_access,
                                         8*1024);
            break;
        }
    case ASI_IMMU_TSB_64KB_PTR: /* I-MMU 64k TSB pointer */
        {
            /* env->immuregs[5] holds I-MMU TSB register value
               env->immuregs[6] holds I-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->immu.tsb, env->immu.tag_access,
                                         64*1024);
            break;
        }
    case ASI_ITLB_DATA_ACCESS: /* I-MMU data access */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb[reg].tte;
            break;
        }
    case ASI_ITLB_TAG_READ: /* I-MMU tag read */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb[reg].tag;
            break;
        }
    case ASI_DMMU: /* D-MMU regs */
        {
            int reg = (addr >> 3) & 0xf;

            if (reg == 0) {
                /* D-TSB Tag Target register */
                ret = ultrasparc_tag_target(env->dmmu.tag_access);
            } else {
                ret = env->dmmuregs[reg];
            }
            break;
        }
    case ASI_DMMU_TSB_8KB_PTR: /* D-MMU 8k TSB pointer */
        {
            /* env->dmmuregs[5] holds D-MMU TSB register value
               env->dmmuregs[6] holds D-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->dmmu.tsb, env->dmmu.tag_access,
                                         8*1024);
            break;
        }
    case ASI_DMMU_TSB_64KB_PTR: /* D-MMU 64k TSB pointer */
        {
            /* env->dmmuregs[5] holds D-MMU TSB register value
               env->dmmuregs[6] holds D-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->dmmu.tsb, env->dmmu.tag_access,
                                         64*1024);
            break;
        }
    case ASI_DTLB_DATA_ACCESS: /* D-MMU data access */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb[reg].tte;
            break;
        }
    case ASI_DTLB_TAG_READ: /* D-MMU tag read */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb[reg].tag;
            break;
        }
    case ASI_INTR_DISPATCH_STAT: /* Interrupt dispatch, RO */
        break;
    case ASI_INTR_RECEIVE: /* Interrupt data receive */
        ret = env->ivec_status;
        break;
    case ASI_INTR_R: /* Incoming interrupt vector, RO */
        {
            int reg = (addr >> 4) & 0x3;
            if (reg < 3) {
                ret = env->ivec_data[reg];
            }
            break;
        }
    case ASI_DCACHE_DATA:     /* D-cache data */
    case ASI_DCACHE_TAG:      /* D-cache tag access */
    case ASI_ESTATE_ERROR_EN: /* E-cache error enable */
    case ASI_AFSR:            /* E-cache asynchronous fault status */
    case ASI_AFAR:            /* E-cache asynchronous fault address */
    case ASI_EC_TAG_DATA:     /* E-cache tag data */
    case ASI_IC_INSTR:        /* I-cache instruction access */
    case ASI_IC_TAG:          /* I-cache tag access */
    case ASI_IC_PRE_DECODE:   /* I-cache predecode */
    case ASI_IC_NEXT_FIELD:   /* I-cache LRU etc. */
    case ASI_EC_W:            /* E-cache tag */
    case ASI_EC_R:            /* E-cache tag */
        break;
    case ASI_DMMU_TSB_DIRECT_PTR: /* D-MMU data pointer */
    case ASI_ITLB_DATA_IN:        /* I-MMU data in, WO */
    case ASI_IMMU_DEMAP:          /* I-MMU demap, WO */
    case ASI_DTLB_DATA_IN:        /* D-MMU data in, WO */
    case ASI_DMMU_DEMAP:          /* D-MMU demap, WO */
    case ASI_INTR_W:              /* Interrupt vector, WO */
    default:
        cpu_unassigned_access(cs, addr, false, false, 1, size);
        ret = 0;
        break;

    case ASI_NUCLEUS_QUAD_LDD:   /* Nucleus quad LDD 128 bit atomic */
    case ASI_NUCLEUS_QUAD_LDD_L: /* Nucleus quad LDD 128 bit atomic LE */
    case ASI_TWINX_AIUP:   /* As if user primary, twinx */
    case ASI_TWINX_AIUS:   /* As if user secondary, twinx */
    case ASI_TWINX_REAL:   /* Real address, twinx */
    case ASI_TWINX_AIUP_L: /* As if user primary, twinx, LE */
    case ASI_TWINX_AIUS_L: /* As if user secondary, twinx, LE */
    case ASI_TWINX_REAL_L: /* Real address, twinx, LE */
    case ASI_TWINX_N:  /* Nucleus, twinx */
    case ASI_TWINX_NL: /* Nucleus, twinx, LE */
    /* ??? From the UA2011 document; overlaps BLK_INIT_QUAD_LDD_* */
    case ASI_TWINX_P:  /* Primary, twinx */
    case ASI_TWINX_PL: /* Primary, twinx, LE */
    case ASI_TWINX_S:  /* Secondary, twinx */
    case ASI_TWINX_SL: /* Secondary, twinx, LE */
        /* These are all 128-bit atomic; only ldda (now ldtxa) allowed */
        helper_raise_exception(env, TT_ILL_INSN);
        return 0;
    }

    /* Convert from little endian */
    switch (asi) {
    case ASI_NL: /* Nucleus Little Endian (LE) */
    case ASI_AIUPL: /* As if user primary LE */
    case ASI_AIUSL: /* As if user secondary LE */
    case ASI_REAL_L:      /* Bypass LE */
    case ASI_REAL_IO_L: /* Bypass, non-cacheable LE */
    case ASI_PL: /* Primary LE */
    case ASI_SL: /* Secondary LE */
        switch(size) {
        case 2:
            ret = bswap16(ret);
            break;
        case 4:
            ret = bswap32(ret);
            break;
        case 8:
            ret = bswap64(ret);
            break;
        default:
            break;
        }
    default:
        break;
    }

    /* Convert to signed number */
    if (sign) {
        switch (size) {
        case 1:
            ret = (int8_t) ret;
            break;
        case 2:
            ret = (int16_t) ret;
            break;
        case 4:
            ret = (int32_t) ret;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG_ASI
    dump_asi("read ", last_addr, asi, size, ret);
#endif
    return ret;
}

void helper_st_asi(CPUSPARCState *env, target_ulong addr, target_ulong val,
                   int asi, uint32_t memop)
{
    int size = 1 << (memop & MO_SIZE);
    SPARCCPU *cpu = sparc_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif

    asi &= 0xff;

    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || (cpu_has_hypervisor(env)
            && asi >= 0x30 && asi < 0x80
            && !(env->hpstate & HS_PRIV))) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    helper_check_align(env, addr, size - 1);
    addr = asi_address_mask(env, asi, addr);

    /* Convert to little endian */
    switch (asi) {
    case ASI_NL: /* Nucleus Little Endian (LE) */
    case ASI_AIUPL: /* As if user primary LE */
    case ASI_AIUSL: /* As if user secondary LE */
    case ASI_REAL_L: /* Bypass LE */
    case ASI_REAL_IO_L: /* Bypass, non-cacheable LE */
    case ASI_PL: /* Primary LE */
    case ASI_SL: /* Secondary LE */
        switch (size) {
        case 2:
            val = bswap16(val);
            break;
        case 4:
            val = bswap32(val);
            break;
        case 8:
            val = bswap64(val);
            break;
        default:
            break;
        }
    default:
        break;
    }

    switch (asi) {
    case ASI_AIUP:  /* As if user primary */
    case ASI_AIUS:  /* As if user secondary */
    case ASI_AIUPL: /* As if user primary LE */
    case ASI_AIUSL: /* As if user secondary LE */
    case ASI_P:  /* Primary */
    case ASI_S:  /* Secondary */
    case ASI_PL: /* Primary LE */
    case ASI_SL: /* Secondary LE */
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if (cpu_hypervisor_mode(env)) {
                switch (size) {
                case 1:
                    cpu_stb_hypv(env, addr, val);
                    break;
                case 2:
                    cpu_stw_hypv(env, addr, val);
                    break;
                case 4:
                    cpu_stl_hypv(env, addr, val);
                    break;
                case 8:
                default:
                    cpu_stq_hypv(env, addr, val);
                    break;
                }
            } else {
                /* secondary space access has lowest asi bit equal to 1 */
                if (asi & 1) {
                    switch (size) {
                    case 1:
                        cpu_stb_kernel_secondary(env, addr, val);
                        break;
                    case 2:
                        cpu_stw_kernel_secondary(env, addr, val);
                        break;
                    case 4:
                        cpu_stl_kernel_secondary(env, addr, val);
                        break;
                    case 8:
                    default:
                        cpu_stq_kernel_secondary(env, addr, val);
                        break;
                    }
                } else {
                    switch (size) {
                    case 1:
                        cpu_stb_kernel(env, addr, val);
                        break;
                    case 2:
                        cpu_stw_kernel(env, addr, val);
                        break;
                    case 4:
                        cpu_stl_kernel(env, addr, val);
                        break;
                    case 8:
                    default:
                        cpu_stq_kernel(env, addr, val);
                        break;
                    }
                }
            }
        } else {
            /* secondary space access has lowest asi bit equal to 1 */
            if (asi & 1) {
                switch (size) {
                case 1:
                    cpu_stb_user_secondary(env, addr, val);
                    break;
                case 2:
                    cpu_stw_user_secondary(env, addr, val);
                    break;
                case 4:
                    cpu_stl_user_secondary(env, addr, val);
                    break;
                case 8:
                default:
                    cpu_stq_user_secondary(env, addr, val);
                    break;
                }
            } else {
                switch (size) {
                case 1:
                    cpu_stb_user(env, addr, val);
                    break;
                case 2:
                    cpu_stw_user(env, addr, val);
                    break;
                case 4:
                    cpu_stl_user(env, addr, val);
                    break;
                case 8:
                default:
                    cpu_stq_user(env, addr, val);
                    break;
                }
            }
        }
        break;
    case ASI_REAL:      /* Bypass */
    case ASI_REAL_IO:   /* Bypass, non-cacheable */
    case ASI_REAL_L:    /* Bypass LE */
    case ASI_REAL_IO_L: /* Bypass, non-cacheable LE */
        {
            switch (size) {
            case 1:
                stb_phys(cs->as, addr, val);
                break;
            case 2:
                stw_phys(cs->as, addr, val);
                break;
            case 4:
                stl_phys(cs->as, addr, val);
                break;
            case 8:
            default:
                stq_phys(cs->as, addr, val);
                break;
            }
        }
        return;
    case ASI_N:  /* Nucleus */
    case ASI_NL: /* Nucleus Little Endian (LE) */
        {
            switch (size) {
            case 1:
                cpu_stb_nucleus(env, addr, val);
                break;
            case 2:
                cpu_stw_nucleus(env, addr, val);
                break;
            case 4:
                cpu_stl_nucleus(env, addr, val);
                break;
            default:
            case 8:
                cpu_stq_nucleus(env, addr, val);
                break;
            }
            break;
        }

    case ASI_UPA_CONFIG: /* UPA config */
        /* XXX */
        return;
    case ASI_LSU_CONTROL: /* LSU */
        {
            uint64_t oldreg;

            oldreg = env->lsu;
            env->lsu = val & (DMMU_E | IMMU_E);
            /* Mappings generated during D/I MMU disabled mode are
               invalid in normal mode */
            if (oldreg != env->lsu) {
                DPRINTF_MMU("LSU change: 0x%" PRIx64 " -> 0x%" PRIx64 "\n",
                            oldreg, env->lsu);
#ifdef DEBUG_MMU
                dump_mmu(stdout, fprintf, env);
#endif
                tlb_flush(CPU(cpu), 1);
            }
            return;
        }
    case ASI_IMMU: /* I-MMU regs */
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->immuregs[reg];
            switch (reg) {
            case 0: /* RO */
                return;
            case 1: /* Not in I-MMU */
            case 2:
                return;
            case 3: /* SFSR */
                if ((val & 1) == 0) {
                    val = 0; /* Clear SFSR */
                }
                env->immu.sfsr = val;
                break;
            case 4: /* RO */
                return;
            case 5: /* TSB access */
                DPRINTF_MMU("immu TSB write: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", env->immu.tsb, val);
                env->immu.tsb = val;
                break;
            case 6: /* Tag access */
                env->immu.tag_access = val;
                break;
            case 7:
            case 8:
                return;
            default:
                break;
            }

            if (oldreg != env->immuregs[reg]) {
                DPRINTF_MMU("immu change reg[%d]: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", reg, oldreg, env->immuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(stdout, fprintf, env);
#endif
            return;
        }
    case ASI_ITLB_DATA_IN: /* I-MMU data in */
        replace_tlb_1bit_lru(env->itlb, env->immu.tag_access, val, "immu", env);
        return;
    case ASI_ITLB_DATA_ACCESS: /* I-MMU data access */
        {
            /* TODO: auto demap */

            unsigned int i = (addr >> 3) & 0x3f;

            replace_tlb_entry(&env->itlb[i], env->immu.tag_access, val, env);

#ifdef DEBUG_MMU
            DPRINTF_MMU("immu data access replaced entry [%i]\n", i);
            dump_mmu(stdout, fprintf, env);
#endif
            return;
        }
    case ASI_IMMU_DEMAP: /* I-MMU demap */
        demap_tlb(env->itlb, addr, "immu", env);
        return;
    case ASI_DMMU: /* D-MMU regs */
        {
            int reg = (addr >> 3) & 0xf;
            uint64_t oldreg;

            oldreg = env->dmmuregs[reg];
            switch (reg) {
            case 0: /* RO */
            case 4:
                return;
            case 3: /* SFSR */
                if ((val & 1) == 0) {
                    val = 0; /* Clear SFSR, Fault address */
                    env->dmmu.sfar = 0;
                }
                env->dmmu.sfsr = val;
                break;
            case 1: /* Primary context */
                env->dmmu.mmu_primary_context = val;
                /* can be optimized to only flush MMU_USER_IDX
                   and MMU_KERNEL_IDX entries */
                tlb_flush(CPU(cpu), 1);
                break;
            case 2: /* Secondary context */
                env->dmmu.mmu_secondary_context = val;
                /* can be optimized to only flush MMU_USER_SECONDARY_IDX
                   and MMU_KERNEL_SECONDARY_IDX entries */
                tlb_flush(CPU(cpu), 1);
                break;
            case 5: /* TSB access */
                DPRINTF_MMU("dmmu TSB write: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", env->dmmu.tsb, val);
                env->dmmu.tsb = val;
                break;
            case 6: /* Tag access */
                env->dmmu.tag_access = val;
                break;
            case 7: /* Virtual Watchpoint */
            case 8: /* Physical Watchpoint */
            default:
                env->dmmuregs[reg] = val;
                break;
            }

            if (oldreg != env->dmmuregs[reg]) {
                DPRINTF_MMU("dmmu change reg[%d]: 0x%016" PRIx64 " -> 0x%016"
                            PRIx64 "\n", reg, oldreg, env->dmmuregs[reg]);
            }
#ifdef DEBUG_MMU
            dump_mmu(stdout, fprintf, env);
#endif
            return;
        }
    case ASI_DTLB_DATA_IN: /* D-MMU data in */
        replace_tlb_1bit_lru(env->dtlb, env->dmmu.tag_access, val, "dmmu", env);
        return;
    case ASI_DTLB_DATA_ACCESS: /* D-MMU data access */
        {
            unsigned int i = (addr >> 3) & 0x3f;

            replace_tlb_entry(&env->dtlb[i], env->dmmu.tag_access, val, env);

#ifdef DEBUG_MMU
            DPRINTF_MMU("dmmu data access replaced entry [%i]\n", i);
            dump_mmu(stdout, fprintf, env);
#endif
            return;
        }
    case ASI_DMMU_DEMAP: /* D-MMU demap */
        demap_tlb(env->dtlb, addr, "dmmu", env);
        return;
    case ASI_INTR_RECEIVE: /* Interrupt data receive */
        env->ivec_status = val & 0x20;
        return;
    case ASI_NUCLEUS_QUAD_LDD:   /* Nucleus quad LDD 128 bit atomic */
    case ASI_NUCLEUS_QUAD_LDD_L: /* Nucleus quad LDD 128 bit atomic LE */
    case ASI_TWINX_AIUP:   /* As if user primary, twinx */
    case ASI_TWINX_AIUS:   /* As if user secondary, twinx */
    case ASI_TWINX_REAL:   /* Real address, twinx */
    case ASI_TWINX_AIUP_L: /* As if user primary, twinx, LE */
    case ASI_TWINX_AIUS_L: /* As if user secondary, twinx, LE */
    case ASI_TWINX_REAL_L: /* Real address, twinx, LE */
    case ASI_TWINX_N:  /* Nucleus, twinx */
    case ASI_TWINX_NL: /* Nucleus, twinx, LE */
    /* ??? From the UA2011 document; overlaps BLK_INIT_QUAD_LDD_* */
    case ASI_TWINX_P:  /* Primary, twinx */
    case ASI_TWINX_PL: /* Primary, twinx, LE */
    case ASI_TWINX_S:  /* Secondary, twinx */
    case ASI_TWINX_SL: /* Secondary, twinx, LE */
        /* Only stda allowed */
        helper_raise_exception(env, TT_ILL_INSN);
        return;
    case ASI_DCACHE_DATA: /* D-cache data */
    case ASI_DCACHE_TAG: /* D-cache tag access */
    case ASI_ESTATE_ERROR_EN: /* E-cache error enable */
    case ASI_AFSR: /* E-cache asynchronous fault status */
    case ASI_AFAR: /* E-cache asynchronous fault address */
    case ASI_EC_TAG_DATA: /* E-cache tag data */
    case ASI_IC_INSTR: /* I-cache instruction access */
    case ASI_IC_TAG: /* I-cache tag access */
    case ASI_IC_PRE_DECODE: /* I-cache predecode */
    case ASI_IC_NEXT_FIELD: /* I-cache LRU etc. */
    case ASI_EC_W: /* E-cache tag */
    case ASI_EC_R: /* E-cache tag */
        return;
    case ASI_IMMU_TSB_8KB_PTR: /* I-MMU 8k TSB pointer, RO */
    case ASI_IMMU_TSB_64KB_PTR: /* I-MMU 64k TSB pointer, RO */
    case ASI_ITLB_TAG_READ: /* I-MMU tag read, RO */
    case ASI_DMMU_TSB_8KB_PTR: /* D-MMU 8k TSB pointer, RO */
    case ASI_DMMU_TSB_64KB_PTR: /* D-MMU 64k TSB pointer, RO */
    case ASI_DMMU_TSB_DIRECT_PTR: /* D-MMU data pointer, RO */
    case ASI_DTLB_TAG_READ: /* D-MMU tag read, RO */
    case ASI_INTR_DISPATCH_STAT: /* Interrupt dispatch, RO */
    case ASI_INTR_R: /* Incoming interrupt vector, RO */
    case ASI_PNF: /* Primary no-fault, RO */
    case ASI_SNF: /* Secondary no-fault, RO */
    case ASI_PNFL: /* Primary no-fault LE, RO */
    case ASI_SNFL: /* Secondary no-fault LE, RO */
    default:
        cpu_unassigned_access(cs, addr, true, false, 1, size);
        return;
    }
}
#endif /* CONFIG_USER_ONLY */

/* 128-bit LDDA; result returned in QT0.  */
void helper_ldda_asi(CPUSPARCState *env, target_ulong addr, int asi)
{
    uint64_t h, l;

    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || (cpu_has_hypervisor(env)
            && asi >= 0x30 && asi < 0x80
            && !(env->hpstate & HS_PRIV))) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
#if !defined(CONFIG_USER_ONLY)
    case ASI_TWINX_AIUP:   /* As if user primary, twinx */
    case ASI_TWINX_AIUP_L: /* As if user primary, twinx, LE */
        helper_check_align(env, addr, 0xf);
        h = cpu_ldq_user(env, addr);
        l = cpu_ldq_user(env, addr + 8);
        break;
    case ASI_TWINX_AIUS:   /* As if user secondary, twinx */
    case ASI_TWINX_AIUS_L: /* As if user secondary, twinx, LE */
        helper_check_align(env, addr, 0xf);
        h = cpu_ldq_user_secondary(env, addr);
        l = cpu_ldq_user_secondary(env, addr + 8);
        break;
    case ASI_TWINX_REAL:   /* Real address, twinx */
    case ASI_TWINX_REAL_L: /* Real address, twinx, LE */
        helper_check_align(env, addr, 0xf);
        {
            CPUState *cs = CPU(sparc_env_get_cpu(env));
            h = ldq_phys(cs->as, addr);
            l = ldq_phys(cs->as, addr + 8);
        }
        break;
    case ASI_NUCLEUS_QUAD_LDD:
    case ASI_NUCLEUS_QUAD_LDD_L:
    case ASI_TWINX_N:  /* Nucleus, twinx */
    case ASI_TWINX_NL: /* Nucleus, twinx, LE */
        helper_check_align(env, addr, 0xf);
        h = cpu_ldq_nucleus(env, addr);
        l = cpu_ldq_nucleus(env, addr + 8);
        break;
    case ASI_TWINX_S: /* Secondary, twinx */
    case ASI_TWINX_SL: /* Secondary, twinx, LE */
        if (!cpu_hypervisor_mode(env)) {
            helper_check_align(env, addr, 0xf);
            if (env->pstate & PS_PRIV) {
                h = cpu_ldq_kernel_secondary(env, addr);
                l = cpu_ldq_kernel_secondary(env, addr + 8);
            } else {
                h = cpu_ldq_user_secondary(env, addr);
                l = cpu_ldq_user_secondary(env, addr + 8);
            }
            break;
        }
        /* fallthru */
    case ASI_TWINX_P:  /* Primary, twinx */
    case ASI_TWINX_PL: /* Primary, twinx, LE */
        helper_check_align(env, addr, 0xf);
        h = cpu_ldq_data(env, addr);
        l = cpu_ldq_data(env, addr + 8);
        break;
#else
    case ASI_TWINX_P:  /* Primary, twinx */
    case ASI_TWINX_PL: /* Primary, twinx, LE */
    case ASI_TWINX_S:  /* Primary, twinx */
    case ASI_TWINX_SL: /* Primary, twinx, LE */
        /* ??? Should be available, but we need to implement
           an atomic 128-bit load.  */
        helper_raise_exception(env, TT_PRIV_ACT);
#endif
    default:
        /* Non-twinx asi, so this is the legacy ldda insn, which
           performs two word sized operations.  */
        /* ??? The UA2011 manual recommends emulating this with
           a single 64-bit load.  However, LE asis *are* treated
           as two 32-bit loads individually byte swapped.  */
        helper_check_align(env, addr, 0x7);
        QT0.high = (uint32_t)helper_ld_asi(env, addr, asi, MO_UL);
        QT0.low = (uint32_t)helper_ld_asi(env, addr + 4, asi, MO_UL);
        return;
    }

    if (asi & 8) {
        h = bswap64(h);
        l = bswap64(l);
    }
    QT0.high = h;
    QT0.low = l;
}

target_ulong helper_casx_asi(CPUSPARCState *env, target_ulong addr,
                             target_ulong val1, target_ulong val2,
                             uint32_t asi)
{
    target_ulong ret;

    ret = helper_ld_asi(env, addr, asi, MO_Q);
    if (val2 == ret) {
        helper_st_asi(env, addr, val1, asi, MO_Q);
    }
    return ret;
}
#endif /* TARGET_SPARC64 */

#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
target_ulong helper_cas_asi(CPUSPARCState *env, target_ulong addr,
                            target_ulong val1, target_ulong val2, uint32_t asi)
{
    target_ulong ret;

    val2 &= 0xffffffffUL;
    ret = helper_ld_asi(env, addr, asi, MO_UL);
    ret &= 0xffffffffUL;
    if (val2 == ret) {
        helper_st_asi(env, addr, val1 & 0xffffffffUL, asi, MO_UL);
    }
    return ret;
}
#endif /* !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64) */

void helper_ldqf(CPUSPARCState *env, target_ulong addr, int mem_idx)
{
    /* XXX add 128 bit load */
    CPU_QuadU u;

    helper_check_align(env, addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case MMU_USER_IDX:
        u.ll.upper = cpu_ldq_user(env, addr);
        u.ll.lower = cpu_ldq_user(env, addr + 8);
        QT0 = u.q;
        break;
    case MMU_KERNEL_IDX:
        u.ll.upper = cpu_ldq_kernel(env, addr);
        u.ll.lower = cpu_ldq_kernel(env, addr + 8);
        QT0 = u.q;
        break;
#ifdef TARGET_SPARC64
    case MMU_HYPV_IDX:
        u.ll.upper = cpu_ldq_hypv(env, addr);
        u.ll.lower = cpu_ldq_hypv(env, addr + 8);
        QT0 = u.q;
        break;
#endif
    default:
        DPRINTF_MMU("helper_ldqf: need to check MMU idx %d\n", mem_idx);
        break;
    }
#else
    u.ll.upper = cpu_ldq_data(env, address_mask(env, addr));
    u.ll.lower = cpu_ldq_data(env, address_mask(env, addr + 8));
    QT0 = u.q;
#endif
}

void helper_stqf(CPUSPARCState *env, target_ulong addr, int mem_idx)
{
    /* XXX add 128 bit store */
    CPU_QuadU u;

    helper_check_align(env, addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case MMU_USER_IDX:
        u.q = QT0;
        cpu_stq_user(env, addr, u.ll.upper);
        cpu_stq_user(env, addr + 8, u.ll.lower);
        break;
    case MMU_KERNEL_IDX:
        u.q = QT0;
        cpu_stq_kernel(env, addr, u.ll.upper);
        cpu_stq_kernel(env, addr + 8, u.ll.lower);
        break;
#ifdef TARGET_SPARC64
    case MMU_HYPV_IDX:
        u.q = QT0;
        cpu_stq_hypv(env, addr, u.ll.upper);
        cpu_stq_hypv(env, addr + 8, u.ll.lower);
        break;
#endif
    default:
        DPRINTF_MMU("helper_stqf: need to check MMU idx %d\n", mem_idx);
        break;
    }
#else
    u.q = QT0;
    cpu_stq_data(env, address_mask(env, addr), u.ll.upper);
    cpu_stq_data(env, address_mask(env, addr + 8), u.ll.lower);
#endif
}

#if !defined(CONFIG_USER_ONLY)
#ifndef TARGET_SPARC64
void sparc_cpu_unassigned_access(CPUState *cs, hwaddr addr,
                                 bool is_write, bool is_exec, int is_asi,
                                 unsigned size)
{
    SPARCCPU *cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;
    int fault_type;

#ifdef DEBUG_UNASSIGNED
    if (is_asi) {
        printf("Unassigned mem %s access of %d byte%s to " TARGET_FMT_plx
               " asi 0x%02x from " TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", size,
               size == 1 ? "" : "s", addr, is_asi, env->pc);
    } else {
        printf("Unassigned mem %s access of %d byte%s to " TARGET_FMT_plx
               " from " TARGET_FMT_lx "\n",
               is_exec ? "exec" : is_write ? "write" : "read", size,
               size == 1 ? "" : "s", addr, env->pc);
    }
#endif
    /* Don't overwrite translation and access faults */
    fault_type = (env->mmuregs[3] & 0x1c) >> 2;
    if ((fault_type > 4) || (fault_type == 0)) {
        env->mmuregs[3] = 0; /* Fault status register */
        if (is_asi) {
            env->mmuregs[3] |= 1 << 16;
        }
        if (env->psrs) {
            env->mmuregs[3] |= 1 << 5;
        }
        if (is_exec) {
            env->mmuregs[3] |= 1 << 6;
        }
        if (is_write) {
            env->mmuregs[3] |= 1 << 7;
        }
        env->mmuregs[3] |= (5 << 2) | 2;
        /* SuperSPARC will never place instruction fault addresses in the FAR */
        if (!is_exec) {
            env->mmuregs[4] = addr; /* Fault address register */
        }
    }
    /* overflow (same type fault was not read before another fault) */
    if (fault_type == ((env->mmuregs[3] & 0x1c)) >> 2) {
        env->mmuregs[3] |= 1;
    }

    if ((env->mmuregs[0] & MMU_E) && !(env->mmuregs[0] & MMU_NF)) {
        if (is_exec) {
            helper_raise_exception(env, TT_CODE_ACCESS);
        } else {
            helper_raise_exception(env, TT_DATA_ACCESS);
        }
    }

    /* flush neverland mappings created during no-fault mode,
       so the sequential MMU faults report proper fault types */
    if (env->mmuregs[0] & MMU_NF) {
        tlb_flush(cs, 1);
    }
}
#else
void sparc_cpu_unassigned_access(CPUState *cs, hwaddr addr,
                                 bool is_write, bool is_exec, int is_asi,
                                 unsigned size)
{
    SPARCCPU *cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;

#ifdef DEBUG_UNASSIGNED
    printf("Unassigned mem access to " TARGET_FMT_plx " from " TARGET_FMT_lx
           "\n", addr, env->pc);
#endif

    if (is_exec) {
        helper_raise_exception(env, TT_CODE_ACCESS);
    } else {
        helper_raise_exception(env, TT_DATA_ACCESS);
    }
}
#endif
#endif

#if !defined(CONFIG_USER_ONLY)
void QEMU_NORETURN sparc_cpu_do_unaligned_access(CPUState *cs, vaddr addr,
                                                 MMUAccessType access_type,
                                                 int mmu_idx,
                                                 uintptr_t retaddr)
{
    SPARCCPU *cpu = SPARC_CPU(cs);
    CPUSPARCState *env = &cpu->env;

#ifdef DEBUG_UNALIGNED
    printf("Unaligned access to 0x" TARGET_FMT_lx " from 0x" TARGET_FMT_lx
           "\n", addr, env->pc);
#endif
    if (retaddr) {
        cpu_restore_state(CPU(cpu), retaddr);
    }
    helper_raise_exception(env, TT_UNALIGNED);
}

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *cs, target_ulong addr, MMUAccessType access_type,
              int mmu_idx, uintptr_t retaddr)
{
    int ret;

    ret = sparc_cpu_handle_mmu_fault(cs, addr, access_type, mmu_idx);
    if (ret) {
        if (retaddr) {
            cpu_restore_state(cs, retaddr);
        }
        cpu_loop_exit(cs);
    }
}
#endif
