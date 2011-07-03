#include "cpu.h"
#include "dyngen-exec.h"
#include "helper.h"

#if !defined(CONFIG_USER_ONLY)
#include "softmmu_exec.h"
#endif

//#define DEBUG_MMU
//#define DEBUG_MXCC
//#define DEBUG_UNALIGNED
//#define DEBUG_UNASSIGNED
//#define DEBUG_ASI
//#define DEBUG_PSTATE
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

#ifdef DEBUG_PSTATE
#define DPRINTF_PSTATE(fmt, ...)                                \
    do { printf("PSTATE: " fmt , ## __VA_ARGS__); } while (0)
#else
#define DPRINTF_PSTATE(fmt, ...) do {} while (0)
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

#define DT0 (env->dt0)
#define DT1 (env->dt1)
#define QT0 (env->qt0)
#define QT1 (env->qt1)

/* Leon3 cache control */

/* Cache control: emulate the behavior of cache control registers but without
   any effect on the emulated */

#define CACHE_STATE_MASK 0x3
#define CACHE_DISABLED   0x0
#define CACHE_FROZEN     0x1
#define CACHE_ENABLED    0x3

/* Cache Control register fields */

#define CACHE_CTRL_IF (1 <<  4)  /* Instruction Cache Freeze on Interrupt */
#define CACHE_CTRL_DF (1 <<  5)  /* Data Cache Freeze on Interrupt */
#define CACHE_CTRL_DP (1 << 14)  /* Data cache flush pending */
#define CACHE_CTRL_IP (1 << 15)  /* Instruction cache flush pending */
#define CACHE_CTRL_IB (1 << 16)  /* Instruction burst fetch */
#define CACHE_CTRL_FI (1 << 21)  /* Flush Instruction cache (Write only) */
#define CACHE_CTRL_FD (1 << 22)  /* Flush Data cache (Write only) */
#define CACHE_CTRL_DS (1 << 23)  /* Data cache snoop enable */

#if !defined(CONFIG_USER_ONLY)
static void do_unassigned_access(target_phys_addr_t addr, int is_write,
                                 int is_exec, int is_asi, int size);
#else
#ifdef TARGET_SPARC64
static void do_unassigned_access(target_ulong addr, int is_write, int is_exec,
                                 int is_asi, int size);
#endif
#endif

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
                              CPUState *env1)
{
    target_ulong mask, size, va, offset;

    /* flush page range if translation is valid */
    if (TTE_IS_VALID(tlb->tte)) {

        mask = 0xffffffffffffe000ULL;
        mask <<= 3 * ((tlb->tte >> 61) & 3);
        size = ~mask + 1;

        va = tlb->tag & mask;

        for (offset = 0; offset < size; offset += TARGET_PAGE_SIZE) {
            tlb_flush_page(env1, va + offset);
        }
    }

    tlb->tag = tlb_tag;
    tlb->tte = tlb_tte;
}

static void demap_tlb(SparcTLBEntry *tlb, target_ulong demap_addr,
                      const char *strmmu, CPUState *env1)
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
                                 const char *strmmu, CPUState *env1)
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

static inline target_ulong address_mask(CPUState *env1, target_ulong addr)
{
#ifdef TARGET_SPARC64
    if (AM_CHECK(env1)) {
        addr &= 0xffffffffULL;
    }
#endif
    return addr;
}

/* returns true if access using this ASI is to have address translated by MMU
   otherwise access is to raw physical address */
static inline int is_translating_asi(int asi)
{
#ifdef TARGET_SPARC64
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
#else
    /* TODO: check sparc32 bits */
    return 0;
#endif
}

static inline target_ulong asi_address_mask(CPUState *env1,
                                            int asi, target_ulong addr)
{
    if (is_translating_asi(asi)) {
        return address_mask(env, addr);
    } else {
        return addr;
    }
}

void helper_check_align(target_ulong addr, uint32_t align)
{
    if (addr & align) {
#ifdef DEBUG_UNALIGNED
        printf("Unaligned access to 0x" TARGET_FMT_lx " from 0x" TARGET_FMT_lx
               "\n", addr, env->pc);
#endif
        helper_raise_exception(env, TT_UNALIGNED);
    }
}

static inline void memcpy32(target_ulong *dst, const target_ulong *src)
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

static void set_cwp(int new_cwp)
{
    /* put the modified wrap registers at their proper location */
    if (env->cwp == env->nwindows - 1) {
        memcpy32(env->regbase, env->regbase + env->nwindows * 16);
    }
    env->cwp = new_cwp;

    /* put the wrap registers at their temporary location */
    if (new_cwp == env->nwindows - 1) {
        memcpy32(env->regbase + env->nwindows * 16, env->regbase);
    }
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

static target_ulong get_psr(void)
{
    helper_compute_psr(env);

#if !defined (TARGET_SPARC64)
    return env->version | (env->psr & PSR_ICC) |
        (env->psref ? PSR_EF : 0) |
        (env->psrpil << 8) |
        (env->psrs ? PSR_S : 0) |
        (env->psrps ? PSR_PS : 0) |
        (env->psret ? PSR_ET : 0) | env->cwp;
#else
    return env->psr & PSR_ICC;
#endif
}

target_ulong cpu_get_psr(CPUState *env1)
{
    CPUState *saved_env;
    target_ulong ret;

    saved_env = env;
    env = env1;
    ret = get_psr();
    env = saved_env;
    return ret;
}

static void put_psr(target_ulong val)
{
    env->psr = val & PSR_ICC;
#if !defined (TARGET_SPARC64)
    env->psref = (val & PSR_EF) ? 1 : 0;
    env->psrpil = (val & PSR_PIL) >> 8;
#endif
#if ((!defined (TARGET_SPARC64)) && !defined(CONFIG_USER_ONLY))
    cpu_check_irqs(env);
#endif
#if !defined (TARGET_SPARC64)
    env->psrs = (val & PSR_S) ? 1 : 0;
    env->psrps = (val & PSR_PS) ? 1 : 0;
    env->psret = (val & PSR_ET) ? 1 : 0;
    set_cwp(val & PSR_CWP);
#endif
    env->cc_op = CC_OP_FLAGS;
}

void cpu_put_psr(CPUState *env1, target_ulong val)
{
    CPUState *saved_env;

    saved_env = env;
    env = env1;
    put_psr(val);
    env = saved_env;
}

static int cwp_inc(int cwp)
{
    if (unlikely(cwp >= env->nwindows)) {
        cwp -= env->nwindows;
    }
    return cwp;
}

int cpu_cwp_inc(CPUState *env1, int cwp)
{
    CPUState *saved_env;
    target_ulong ret;

    saved_env = env;
    env = env1;
    ret = cwp_inc(cwp);
    env = saved_env;
    return ret;
}

static int cwp_dec(int cwp)
{
    if (unlikely(cwp < 0)) {
        cwp += env->nwindows;
    }
    return cwp;
}

int cpu_cwp_dec(CPUState *env1, int cwp)
{
    CPUState *saved_env;
    target_ulong ret;

    saved_env = env;
    env = env1;
    ret = cwp_dec(cwp);
    env = saved_env;
    return ret;
}

#if !defined(TARGET_SPARC64) && !defined(CONFIG_USER_ONLY) &&   \
    defined(DEBUG_MXCC)
static void dump_mxcc(CPUState *env)
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

static void leon3_cache_control_int(void)
{
    uint32_t state = 0;

    if (env->cache_control & CACHE_CTRL_IF) {
        /* Instruction cache state */
        state = env->cache_control & CACHE_STATE_MASK;
        if (state == CACHE_ENABLED) {
            state = CACHE_FROZEN;
            DPRINTF_CACHE_CONTROL("Instruction cache: freeze\n");
        }

        env->cache_control &= ~CACHE_STATE_MASK;
        env->cache_control |= state;
    }

    if (env->cache_control & CACHE_CTRL_DF) {
        /* Data cache state */
        state = (env->cache_control >> 2) & CACHE_STATE_MASK;
        if (state == CACHE_ENABLED) {
            state = CACHE_FROZEN;
            DPRINTF_CACHE_CONTROL("Data cache: freeze\n");
        }

        env->cache_control &= ~(CACHE_STATE_MASK << 2);
        env->cache_control |= (state << 2);
    }
}

static void leon3_cache_control_st(target_ulong addr, uint64_t val, int size)
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

static uint64_t leon3_cache_control_ld(target_ulong addr, int size)
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

void leon3_irq_manager(void *irq_manager, int intno)
{
    leon3_irq_ack(irq_manager, intno);
    leon3_cache_control_int();
}

uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_MXCC) || defined(DEBUG_ASI)
    uint32_t last_addr = addr;
#endif

    helper_check_align(addr, size - 1);
    switch (asi) {
    case 2: /* SuperSparc MXCC registers and Leon3 cache control */
        switch (addr) {
        case 0x00:          /* Leon3 Cache Control */
        case 0x08:          /* Leon3 Instruction Cache config */
        case 0x0C:          /* Leon3 Date Cache config */
            if (env->def->features & CPU_FEATURE_CACHE_CTRL) {
                ret = leon3_cache_control_ld(addr, size);
            }
            break;
        case 0x01c00a00: /* MXCC control register */
            if (size == 8) {
                ret = env->mxccregs[3];
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4) {
                ret = env->mxccregs[3];
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00c00: /* Module reset register */
            if (size == 8) {
                ret = env->mxccregs[5];
                /* should we do something here? */
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8) {
                ret = env->mxccregs[7];
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        default:
            DPRINTF_MXCC("%08x: unimplemented address, size: %d\n", addr,
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
    case 3: /* MMU probe */
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
    case 4: /* read MMU regs */
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
    case 5: /* Turbosparc ITLB Diagnostic */
    case 6: /* Turbosparc DTLB Diagnostic */
    case 7: /* Turbosparc IOTLB Diagnostic */
        break;
    case 9: /* Supervisor code access */
        switch (size) {
        case 1:
            ret = ldub_code(addr);
            break;
        case 2:
            ret = lduw_code(addr);
            break;
        default:
        case 4:
            ret = ldl_code(addr);
            break;
        case 8:
            ret = ldq_code(addr);
            break;
        }
        break;
    case 0xa: /* User data access */
        switch (size) {
        case 1:
            ret = ldub_user(addr);
            break;
        case 2:
            ret = lduw_user(addr);
            break;
        default:
        case 4:
            ret = ldl_user(addr);
            break;
        case 8:
            ret = ldq_user(addr);
            break;
        }
        break;
    case 0xb: /* Supervisor data access */
        switch (size) {
        case 1:
            ret = ldub_kernel(addr);
            break;
        case 2:
            ret = lduw_kernel(addr);
            break;
        default:
        case 4:
            ret = ldl_kernel(addr);
            break;
        case 8:
            ret = ldq_kernel(addr);
            break;
        }
        break;
    case 0xc: /* I-cache tag */
    case 0xd: /* I-cache data */
    case 0xe: /* D-cache tag */
    case 0xf: /* D-cache data */
        break;
    case 0x20: /* MMU passthrough */
        switch (size) {
        case 1:
            ret = ldub_phys(addr);
            break;
        case 2:
            ret = lduw_phys(addr);
            break;
        default:
        case 4:
            ret = ldl_phys(addr);
            break;
        case 8:
            ret = ldq_phys(addr);
            break;
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        switch (size) {
        case 1:
            ret = ldub_phys((target_phys_addr_t)addr
                            | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        case 2:
            ret = lduw_phys((target_phys_addr_t)addr
                            | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        default:
        case 4:
            ret = ldl_phys((target_phys_addr_t)addr
                           | ((target_phys_addr_t)(asi & 0xf) << 32));
            break;
        case 8:
            ret = ldq_phys((target_phys_addr_t)addr
                           | ((target_phys_addr_t)(asi & 0xf) << 32));
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
    case 8: /* User code access, XXX */
    default:
        do_unassigned_access(addr, 0, 0, asi, size);
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

void helper_st_asi(target_ulong addr, uint64_t val, int asi, int size)
{
    helper_check_align(addr, size - 1);
    switch (asi) {
    case 2: /* SuperSparc MXCC registers and Leon3 cache control */
        switch (addr) {
        case 0x00:          /* Leon3 Cache Control */
        case 0x08:          /* Leon3 Instruction Cache config */
        case 0x0C:          /* Leon3 Date Cache config */
            if (env->def->features & CPU_FEATURE_CACHE_CTRL) {
                leon3_cache_control_st(addr, val, size);
            }
            break;

        case 0x01c00000: /* MXCC stream data register 0 */
            if (size == 8) {
                env->mxccdata[0] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00008: /* MXCC stream data register 1 */
            if (size == 8) {
                env->mxccdata[1] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00010: /* MXCC stream data register 2 */
            if (size == 8) {
                env->mxccdata[2] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00018: /* MXCC stream data register 3 */
            if (size == 8) {
                env->mxccdata[3] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00100: /* MXCC stream source */
            if (size == 8) {
                env->mxccregs[0] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            env->mxccdata[0] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        0);
            env->mxccdata[1] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        8);
            env->mxccdata[2] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        16);
            env->mxccdata[3] = ldq_phys((env->mxccregs[0] & 0xffffffffULL) +
                                        24);
            break;
        case 0x01c00200: /* MXCC stream destination */
            if (size == 8) {
                env->mxccregs[1] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            stq_phys((env->mxccregs[1] & 0xffffffffULL) +  0,
                     env->mxccdata[0]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) +  8,
                     env->mxccdata[1]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) + 16,
                     env->mxccdata[2]);
            stq_phys((env->mxccregs[1] & 0xffffffffULL) + 24,
                     env->mxccdata[3]);
            break;
        case 0x01c00a00: /* MXCC control register */
            if (size == 8) {
                env->mxccregs[3] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00a04: /* MXCC control register */
            if (size == 4) {
                env->mxccregs[3] = (env->mxccregs[3] & 0xffffffff00000000ULL)
                    | val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00e00: /* MXCC error register  */
            /* writing a 1 bit clears the error */
            if (size == 8) {
                env->mxccregs[6] &= ~val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        case 0x01c00f00: /* MBus port address register */
            if (size == 8) {
                env->mxccregs[7] = val;
            } else {
                DPRINTF_MXCC("%08x: unimplemented access size: %d\n", addr,
                             size);
            }
            break;
        default:
            DPRINTF_MXCC("%08x: unimplemented address, size: %d\n", addr,
                         size);
            break;
        }
        DPRINTF_MXCC("asi = %d, size = %d, addr = %08x, val = %" PRIx64 "\n",
                     asi, size, addr, val);
#ifdef DEBUG_MXCC
        dump_mxcc(env);
#endif
        break;
    case 3: /* MMU flush */
        {
            int mmulev;

            mmulev = (addr >> 8) & 15;
            DPRINTF_MMU("mmu flush level %d\n", mmulev);
            switch (mmulev) {
            case 0: /* flush page */
                tlb_flush_page(env, addr & 0xfffff000);
                break;
            case 1: /* flush segment (256k) */
            case 2: /* flush region (16M) */
            case 3: /* flush context (4G) */
            case 4: /* flush entire */
                tlb_flush(env, 1);
                break;
            default:
                break;
            }
#ifdef DEBUG_MMU
            dump_mmu(stdout, fprintf, env);
#endif
        }
        break;
    case 4: /* write MMU regs */
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
                    tlb_flush(env, 1);
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
                    tlb_flush(env, 1);
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
    case 5: /* Turbosparc ITLB Diagnostic */
    case 6: /* Turbosparc DTLB Diagnostic */
    case 7: /* Turbosparc IOTLB Diagnostic */
        break;
    case 0xa: /* User data access */
        switch (size) {
        case 1:
            stb_user(addr, val);
            break;
        case 2:
            stw_user(addr, val);
            break;
        default:
        case 4:
            stl_user(addr, val);
            break;
        case 8:
            stq_user(addr, val);
            break;
        }
        break;
    case 0xb: /* Supervisor data access */
        switch (size) {
        case 1:
            stb_kernel(addr, val);
            break;
        case 2:
            stw_kernel(addr, val);
            break;
        default:
        case 4:
            stl_kernel(addr, val);
            break;
        case 8:
            stq_kernel(addr, val);
            break;
        }
        break;
    case 0xc: /* I-cache tag */
    case 0xd: /* I-cache data */
    case 0xe: /* D-cache tag */
    case 0xf: /* D-cache data */
    case 0x10: /* I/D-cache flush page */
    case 0x11: /* I/D-cache flush segment */
    case 0x12: /* I/D-cache flush region */
    case 0x13: /* I/D-cache flush context */
    case 0x14: /* I/D-cache flush user */
        break;
    case 0x17: /* Block copy, sta access */
        {
            /* val = src
               addr = dst
               copy 32 bytes */
            unsigned int i;
            uint32_t src = val & ~3, dst = addr & ~3, temp;

            for (i = 0; i < 32; i += 4, src += 4, dst += 4) {
                temp = ldl_kernel(src);
                stl_kernel(dst, temp);
            }
        }
        break;
    case 0x1f: /* Block fill, stda access */
        {
            /* addr = dst
               fill 32 bytes with val */
            unsigned int i;
            uint32_t dst = addr & 7;

            for (i = 0; i < 32; i += 8, dst += 8) {
                stq_kernel(dst, val);
            }
        }
        break;
    case 0x20: /* MMU passthrough */
        {
            switch (size) {
            case 1:
                stb_phys(addr, val);
                break;
            case 2:
                stw_phys(addr, val);
                break;
            case 4:
            default:
                stl_phys(addr, val);
                break;
            case 8:
                stq_phys(addr, val);
                break;
            }
        }
        break;
    case 0x21 ... 0x2f: /* MMU passthrough, 0x100000000 to 0xfffffffff */
        {
            switch (size) {
            case 1:
                stb_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 2:
                stw_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 4:
            default:
                stl_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
                break;
            case 8:
                stq_phys((target_phys_addr_t)addr
                         | ((target_phys_addr_t)(asi & 0xf) << 32), val);
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
    case 8: /* User code access, XXX */
    case 9: /* Supervisor code access, XXX */
    default:
        do_unassigned_access(addr, 1, 0, asi, size);
        break;
    }
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
}

#endif /* CONFIG_USER_ONLY */
#else /* TARGET_SPARC64 */

#ifdef CONFIG_USER_ONLY
uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
    uint64_t ret = 0;
#if defined(DEBUG_ASI)
    target_ulong last_addr = addr;
#endif

    if (asi < 0x80) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    helper_check_align(addr, size - 1);
    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
    case 0x82: /* Primary no-fault */
    case 0x8a: /* Primary no-fault LE */
        if (page_check_range(addr, size, PAGE_READ) == -1) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        /* Fall through */
    case 0x80: /* Primary */
    case 0x88: /* Primary LE */
        {
            switch (size) {
            case 1:
                ret = ldub_raw(addr);
                break;
            case 2:
                ret = lduw_raw(addr);
                break;
            case 4:
                ret = ldl_raw(addr);
                break;
            default:
            case 8:
                ret = ldq_raw(addr);
                break;
            }
        }
        break;
    case 0x83: /* Secondary no-fault */
    case 0x8b: /* Secondary no-fault LE */
        if (page_check_range(addr, size, PAGE_READ) == -1) {
#ifdef DEBUG_ASI
            dump_asi("read ", last_addr, asi, size, ret);
#endif
            return 0;
        }
        /* Fall through */
    case 0x81: /* Secondary */
    case 0x89: /* Secondary LE */
        /* XXX */
        break;
    default:
        break;
    }

    /* Convert from little endian */
    switch (asi) {
    case 0x88: /* Primary LE */
    case 0x89: /* Secondary LE */
    case 0x8a: /* Primary no-fault LE */
    case 0x8b: /* Secondary no-fault LE */
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

void helper_st_asi(target_ulong addr, target_ulong val, int asi, int size)
{
#ifdef DEBUG_ASI
    dump_asi("write", addr, asi, size, val);
#endif
    if (asi < 0x80) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    helper_check_align(addr, size - 1);
    addr = asi_address_mask(env, asi, addr);

    /* Convert to little endian */
    switch (asi) {
    case 0x88: /* Primary LE */
    case 0x89: /* Secondary LE */
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
    case 0x80: /* Primary */
    case 0x88: /* Primary LE */
        {
            switch (size) {
            case 1:
                stb_raw(addr, val);
                break;
            case 2:
                stw_raw(addr, val);
                break;
            case 4:
                stl_raw(addr, val);
                break;
            case 8:
            default:
                stq_raw(addr, val);
                break;
            }
        }
        break;
    case 0x81: /* Secondary */
    case 0x89: /* Secondary LE */
        /* XXX */
        return;

    case 0x82: /* Primary no-fault, RO */
    case 0x83: /* Secondary no-fault, RO */
    case 0x8a: /* Primary no-fault LE, RO */
    case 0x8b: /* Secondary no-fault LE, RO */
    default:
        do_unassigned_access(addr, 1, 0, 1, size);
        return;
    }
}

#else /* CONFIG_USER_ONLY */

uint64_t helper_ld_asi(target_ulong addr, int asi, int size, int sign)
{
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

    helper_check_align(addr, size - 1);
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
            helper_raise_exception(env, env->exception_index);
        }

        /* convert nonfaulting load ASIs to normal load ASIs */
        asi &= ~0x02;
    }

    switch (asi) {
    case 0x10: /* As if user primary */
    case 0x11: /* As if user secondary */
    case 0x18: /* As if user primary LE */
    case 0x19: /* As if user secondary LE */
    case 0x80: /* Primary */
    case 0x81: /* Secondary */
    case 0x88: /* Primary LE */
    case 0x89: /* Secondary LE */
    case 0xe2: /* UA2007 Primary block init */
    case 0xe3: /* UA2007 Secondary block init */
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if (cpu_hypervisor_mode(env)) {
                switch (size) {
                case 1:
                    ret = ldub_hypv(addr);
                    break;
                case 2:
                    ret = lduw_hypv(addr);
                    break;
                case 4:
                    ret = ldl_hypv(addr);
                    break;
                default:
                case 8:
                    ret = ldq_hypv(addr);
                    break;
                }
            } else {
                /* secondary space access has lowest asi bit equal to 1 */
                if (asi & 1) {
                    switch (size) {
                    case 1:
                        ret = ldub_kernel_secondary(addr);
                        break;
                    case 2:
                        ret = lduw_kernel_secondary(addr);
                        break;
                    case 4:
                        ret = ldl_kernel_secondary(addr);
                        break;
                    default:
                    case 8:
                        ret = ldq_kernel_secondary(addr);
                        break;
                    }
                } else {
                    switch (size) {
                    case 1:
                        ret = ldub_kernel(addr);
                        break;
                    case 2:
                        ret = lduw_kernel(addr);
                        break;
                    case 4:
                        ret = ldl_kernel(addr);
                        break;
                    default:
                    case 8:
                        ret = ldq_kernel(addr);
                        break;
                    }
                }
            }
        } else {
            /* secondary space access has lowest asi bit equal to 1 */
            if (asi & 1) {
                switch (size) {
                case 1:
                    ret = ldub_user_secondary(addr);
                    break;
                case 2:
                    ret = lduw_user_secondary(addr);
                    break;
                case 4:
                    ret = ldl_user_secondary(addr);
                    break;
                default:
                case 8:
                    ret = ldq_user_secondary(addr);
                    break;
                }
            } else {
                switch (size) {
                case 1:
                    ret = ldub_user(addr);
                    break;
                case 2:
                    ret = lduw_user(addr);
                    break;
                case 4:
                    ret = ldl_user(addr);
                    break;
                default:
                case 8:
                    ret = ldq_user(addr);
                    break;
                }
            }
        }
        break;
    case 0x14: /* Bypass */
    case 0x15: /* Bypass, non-cacheable */
    case 0x1c: /* Bypass LE */
    case 0x1d: /* Bypass, non-cacheable LE */
        {
            switch (size) {
            case 1:
                ret = ldub_phys(addr);
                break;
            case 2:
                ret = lduw_phys(addr);
                break;
            case 4:
                ret = ldl_phys(addr);
                break;
            default:
            case 8:
                ret = ldq_phys(addr);
                break;
            }
            break;
        }
    case 0x24: /* Nucleus quad LDD 128 bit atomic */
    case 0x2c: /* Nucleus quad LDD 128 bit atomic LE
                  Only ldda allowed */
        helper_raise_exception(env, TT_ILL_INSN);
        return 0;
    case 0x04: /* Nucleus */
    case 0x0c: /* Nucleus Little Endian (LE) */
        {
            switch (size) {
            case 1:
                ret = ldub_nucleus(addr);
                break;
            case 2:
                ret = lduw_nucleus(addr);
                break;
            case 4:
                ret = ldl_nucleus(addr);
                break;
            default:
            case 8:
                ret = ldq_nucleus(addr);
                break;
            }
            break;
        }
    case 0x4a: /* UPA config */
        /* XXX */
        break;
    case 0x45: /* LSU */
        ret = env->lsu;
        break;
    case 0x50: /* I-MMU regs */
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
    case 0x51: /* I-MMU 8k TSB pointer */
        {
            /* env->immuregs[5] holds I-MMU TSB register value
               env->immuregs[6] holds I-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->immu.tsb, env->immu.tag_access,
                                         8*1024);
            break;
        }
    case 0x52: /* I-MMU 64k TSB pointer */
        {
            /* env->immuregs[5] holds I-MMU TSB register value
               env->immuregs[6] holds I-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->immu.tsb, env->immu.tag_access,
                                         64*1024);
            break;
        }
    case 0x55: /* I-MMU data access */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb[reg].tte;
            break;
        }
    case 0x56: /* I-MMU tag read */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->itlb[reg].tag;
            break;
        }
    case 0x58: /* D-MMU regs */
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
    case 0x59: /* D-MMU 8k TSB pointer */
        {
            /* env->dmmuregs[5] holds D-MMU TSB register value
               env->dmmuregs[6] holds D-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->dmmu.tsb, env->dmmu.tag_access,
                                         8*1024);
            break;
        }
    case 0x5a: /* D-MMU 64k TSB pointer */
        {
            /* env->dmmuregs[5] holds D-MMU TSB register value
               env->dmmuregs[6] holds D-MMU Tag Access register value */
            ret = ultrasparc_tsb_pointer(env->dmmu.tsb, env->dmmu.tag_access,
                                         64*1024);
            break;
        }
    case 0x5d: /* D-MMU data access */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb[reg].tte;
            break;
        }
    case 0x5e: /* D-MMU tag read */
        {
            int reg = (addr >> 3) & 0x3f;

            ret = env->dtlb[reg].tag;
            break;
        }
    case 0x46: /* D-cache data */
    case 0x47: /* D-cache tag access */
    case 0x4b: /* E-cache error enable */
    case 0x4c: /* E-cache asynchronous fault status */
    case 0x4d: /* E-cache asynchronous fault address */
    case 0x4e: /* E-cache tag data */
    case 0x66: /* I-cache instruction access */
    case 0x67: /* I-cache tag access */
    case 0x6e: /* I-cache predecode */
    case 0x6f: /* I-cache LRU etc. */
    case 0x76: /* E-cache tag */
    case 0x7e: /* E-cache tag */
        break;
    case 0x5b: /* D-MMU data pointer */
    case 0x48: /* Interrupt dispatch, RO */
    case 0x49: /* Interrupt data receive */
    case 0x7f: /* Incoming interrupt vector, RO */
        /* XXX */
        break;
    case 0x54: /* I-MMU data in, WO */
    case 0x57: /* I-MMU demap, WO */
    case 0x5c: /* D-MMU data in, WO */
    case 0x5f: /* D-MMU demap, WO */
    case 0x77: /* Interrupt vector, WO */
    default:
        do_unassigned_access(addr, 0, 0, 1, size);
        ret = 0;
        break;
    }

    /* Convert from little endian */
    switch (asi) {
    case 0x0c: /* Nucleus Little Endian (LE) */
    case 0x18: /* As if user primary LE */
    case 0x19: /* As if user secondary LE */
    case 0x1c: /* Bypass LE */
    case 0x1d: /* Bypass, non-cacheable LE */
    case 0x88: /* Primary LE */
    case 0x89: /* Secondary LE */
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

void helper_st_asi(target_ulong addr, target_ulong val, int asi, int size)
{
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

    helper_check_align(addr, size - 1);
    addr = asi_address_mask(env, asi, addr);

    /* Convert to little endian */
    switch (asi) {
    case 0x0c: /* Nucleus Little Endian (LE) */
    case 0x18: /* As if user primary LE */
    case 0x19: /* As if user secondary LE */
    case 0x1c: /* Bypass LE */
    case 0x1d: /* Bypass, non-cacheable LE */
    case 0x88: /* Primary LE */
    case 0x89: /* Secondary LE */
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
    case 0x10: /* As if user primary */
    case 0x11: /* As if user secondary */
    case 0x18: /* As if user primary LE */
    case 0x19: /* As if user secondary LE */
    case 0x80: /* Primary */
    case 0x81: /* Secondary */
    case 0x88: /* Primary LE */
    case 0x89: /* Secondary LE */
    case 0xe2: /* UA2007 Primary block init */
    case 0xe3: /* UA2007 Secondary block init */
        if ((asi & 0x80) && (env->pstate & PS_PRIV)) {
            if (cpu_hypervisor_mode(env)) {
                switch (size) {
                case 1:
                    stb_hypv(addr, val);
                    break;
                case 2:
                    stw_hypv(addr, val);
                    break;
                case 4:
                    stl_hypv(addr, val);
                    break;
                case 8:
                default:
                    stq_hypv(addr, val);
                    break;
                }
            } else {
                /* secondary space access has lowest asi bit equal to 1 */
                if (asi & 1) {
                    switch (size) {
                    case 1:
                        stb_kernel_secondary(addr, val);
                        break;
                    case 2:
                        stw_kernel_secondary(addr, val);
                        break;
                    case 4:
                        stl_kernel_secondary(addr, val);
                        break;
                    case 8:
                    default:
                        stq_kernel_secondary(addr, val);
                        break;
                    }
                } else {
                    switch (size) {
                    case 1:
                        stb_kernel(addr, val);
                        break;
                    case 2:
                        stw_kernel(addr, val);
                        break;
                    case 4:
                        stl_kernel(addr, val);
                        break;
                    case 8:
                    default:
                        stq_kernel(addr, val);
                        break;
                    }
                }
            }
        } else {
            /* secondary space access has lowest asi bit equal to 1 */
            if (asi & 1) {
                switch (size) {
                case 1:
                    stb_user_secondary(addr, val);
                    break;
                case 2:
                    stw_user_secondary(addr, val);
                    break;
                case 4:
                    stl_user_secondary(addr, val);
                    break;
                case 8:
                default:
                    stq_user_secondary(addr, val);
                    break;
                }
            } else {
                switch (size) {
                case 1:
                    stb_user(addr, val);
                    break;
                case 2:
                    stw_user(addr, val);
                    break;
                case 4:
                    stl_user(addr, val);
                    break;
                case 8:
                default:
                    stq_user(addr, val);
                    break;
                }
            }
        }
        break;
    case 0x14: /* Bypass */
    case 0x15: /* Bypass, non-cacheable */
    case 0x1c: /* Bypass LE */
    case 0x1d: /* Bypass, non-cacheable LE */
        {
            switch (size) {
            case 1:
                stb_phys(addr, val);
                break;
            case 2:
                stw_phys(addr, val);
                break;
            case 4:
                stl_phys(addr, val);
                break;
            case 8:
            default:
                stq_phys(addr, val);
                break;
            }
        }
        return;
    case 0x24: /* Nucleus quad LDD 128 bit atomic */
    case 0x2c: /* Nucleus quad LDD 128 bit atomic LE
                  Only ldda allowed */
        helper_raise_exception(env, TT_ILL_INSN);
        return;
    case 0x04: /* Nucleus */
    case 0x0c: /* Nucleus Little Endian (LE) */
        {
            switch (size) {
            case 1:
                stb_nucleus(addr, val);
                break;
            case 2:
                stw_nucleus(addr, val);
                break;
            case 4:
                stl_nucleus(addr, val);
                break;
            default:
            case 8:
                stq_nucleus(addr, val);
                break;
            }
            break;
        }

    case 0x4a: /* UPA config */
        /* XXX */
        return;
    case 0x45: /* LSU */
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
                dump_mmu(stdout, fprintf, env1);
#endif
                tlb_flush(env, 1);
            }
            return;
        }
    case 0x50: /* I-MMU regs */
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
    case 0x54: /* I-MMU data in */
        replace_tlb_1bit_lru(env->itlb, env->immu.tag_access, val, "immu", env);
        return;
    case 0x55: /* I-MMU data access */
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
    case 0x57: /* I-MMU demap */
        demap_tlb(env->itlb, addr, "immu", env);
        return;
    case 0x58: /* D-MMU regs */
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
                tlb_flush(env, 1);
                break;
            case 2: /* Secondary context */
                env->dmmu.mmu_secondary_context = val;
                /* can be optimized to only flush MMU_USER_SECONDARY_IDX
                   and MMU_KERNEL_SECONDARY_IDX entries */
                tlb_flush(env, 1);
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
    case 0x5c: /* D-MMU data in */
        replace_tlb_1bit_lru(env->dtlb, env->dmmu.tag_access, val, "dmmu", env);
        return;
    case 0x5d: /* D-MMU data access */
        {
            unsigned int i = (addr >> 3) & 0x3f;

            replace_tlb_entry(&env->dtlb[i], env->dmmu.tag_access, val, env);

#ifdef DEBUG_MMU
            DPRINTF_MMU("dmmu data access replaced entry [%i]\n", i);
            dump_mmu(stdout, fprintf, env);
#endif
            return;
        }
    case 0x5f: /* D-MMU demap */
        demap_tlb(env->dtlb, addr, "dmmu", env);
        return;
    case 0x49: /* Interrupt data receive */
        /* XXX */
        return;
    case 0x46: /* D-cache data */
    case 0x47: /* D-cache tag access */
    case 0x4b: /* E-cache error enable */
    case 0x4c: /* E-cache asynchronous fault status */
    case 0x4d: /* E-cache asynchronous fault address */
    case 0x4e: /* E-cache tag data */
    case 0x66: /* I-cache instruction access */
    case 0x67: /* I-cache tag access */
    case 0x6e: /* I-cache predecode */
    case 0x6f: /* I-cache LRU etc. */
    case 0x76: /* E-cache tag */
    case 0x7e: /* E-cache tag */
        return;
    case 0x51: /* I-MMU 8k TSB pointer, RO */
    case 0x52: /* I-MMU 64k TSB pointer, RO */
    case 0x56: /* I-MMU tag read, RO */
    case 0x59: /* D-MMU 8k TSB pointer, RO */
    case 0x5a: /* D-MMU 64k TSB pointer, RO */
    case 0x5b: /* D-MMU data pointer, RO */
    case 0x5e: /* D-MMU tag read, RO */
    case 0x48: /* Interrupt dispatch, RO */
    case 0x7f: /* Incoming interrupt vector, RO */
    case 0x82: /* Primary no-fault, RO */
    case 0x83: /* Secondary no-fault, RO */
    case 0x8a: /* Primary no-fault LE, RO */
    case 0x8b: /* Secondary no-fault LE, RO */
    default:
        do_unassigned_access(addr, 1, 0, 1, size);
        return;
    }
}
#endif /* CONFIG_USER_ONLY */

void helper_ldda_asi(target_ulong addr, int asi, int rd)
{
    if ((asi < 0x80 && (env->pstate & PS_PRIV) == 0)
        || (cpu_has_hypervisor(env)
            && asi >= 0x30 && asi < 0x80
            && !(env->hpstate & HS_PRIV))) {
        helper_raise_exception(env, TT_PRIV_ACT);
    }

    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
#if !defined(CONFIG_USER_ONLY)
    case 0x24: /* Nucleus quad LDD 128 bit atomic */
    case 0x2c: /* Nucleus quad LDD 128 bit atomic LE */
        helper_check_align(addr, 0xf);
        if (rd == 0) {
            env->gregs[1] = ldq_nucleus(addr + 8);
            if (asi == 0x2c) {
                bswap64s(&env->gregs[1]);
            }
        } else if (rd < 8) {
            env->gregs[rd] = ldq_nucleus(addr);
            env->gregs[rd + 1] = ldq_nucleus(addr + 8);
            if (asi == 0x2c) {
                bswap64s(&env->gregs[rd]);
                bswap64s(&env->gregs[rd + 1]);
            }
        } else {
            env->regwptr[rd] = ldq_nucleus(addr);
            env->regwptr[rd + 1] = ldq_nucleus(addr + 8);
            if (asi == 0x2c) {
                bswap64s(&env->regwptr[rd]);
                bswap64s(&env->regwptr[rd + 1]);
            }
        }
        break;
#endif
    default:
        helper_check_align(addr, 0x3);
        if (rd == 0) {
            env->gregs[1] = helper_ld_asi(addr + 4, asi, 4, 0);
        } else if (rd < 8) {
            env->gregs[rd] = helper_ld_asi(addr, asi, 4, 0);
            env->gregs[rd + 1] = helper_ld_asi(addr + 4, asi, 4, 0);
        } else {
            env->regwptr[rd] = helper_ld_asi(addr, asi, 4, 0);
            env->regwptr[rd + 1] = helper_ld_asi(addr + 4, asi, 4, 0);
        }
        break;
    }
}

void helper_ldf_asi(target_ulong addr, int asi, int size, int rd)
{
    unsigned int i;
    CPU_DoubleU u;

    helper_check_align(addr, 3);
    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
    case 0xf0: /* UA2007/JPS1 Block load primary */
    case 0xf1: /* UA2007/JPS1 Block load secondary */
    case 0xf8: /* UA2007/JPS1 Block load primary LE */
    case 0xf9: /* UA2007/JPS1 Block load secondary LE */
        if (rd & 7) {
            helper_raise_exception(env, TT_ILL_INSN);
            return;
        }
        helper_check_align(addr, 0x3f);
        for (i = 0; i < 16; i++) {
            *(uint32_t *)&env->fpr[rd++] = helper_ld_asi(addr, asi & 0x8f, 4,
                                                         0);
            addr += 4;
        }

        return;
    case 0x16: /* UA2007 Block load primary, user privilege */
    case 0x17: /* UA2007 Block load secondary, user privilege */
    case 0x1e: /* UA2007 Block load primary LE, user privilege */
    case 0x1f: /* UA2007 Block load secondary LE, user privilege */
    case 0x70: /* JPS1 Block load primary, user privilege */
    case 0x71: /* JPS1 Block load secondary, user privilege */
    case 0x78: /* JPS1 Block load primary LE, user privilege */
    case 0x79: /* JPS1 Block load secondary LE, user privilege */
        if (rd & 7) {
            helper_raise_exception(env, TT_ILL_INSN);
            return;
        }
        helper_check_align(addr, 0x3f);
        for (i = 0; i < 16; i++) {
            *(uint32_t *)&env->fpr[rd++] = helper_ld_asi(addr, asi & 0x19, 4,
                                                         0);
            addr += 4;
        }

        return;
    default:
        break;
    }

    switch (size) {
    default:
    case 4:
        *((uint32_t *)&env->fpr[rd]) = helper_ld_asi(addr, asi, size, 0);
        break;
    case 8:
        u.ll = helper_ld_asi(addr, asi, size, 0);
        *((uint32_t *)&env->fpr[rd++]) = u.l.upper;
        *((uint32_t *)&env->fpr[rd++]) = u.l.lower;
        break;
    case 16:
        u.ll = helper_ld_asi(addr, asi, 8, 0);
        *((uint32_t *)&env->fpr[rd++]) = u.l.upper;
        *((uint32_t *)&env->fpr[rd++]) = u.l.lower;
        u.ll = helper_ld_asi(addr + 8, asi, 8, 0);
        *((uint32_t *)&env->fpr[rd++]) = u.l.upper;
        *((uint32_t *)&env->fpr[rd++]) = u.l.lower;
        break;
    }
}

void helper_stf_asi(target_ulong addr, int asi, int size, int rd)
{
    unsigned int i;
    target_ulong val = 0;
    CPU_DoubleU u;

    helper_check_align(addr, 3);
    addr = asi_address_mask(env, asi, addr);

    switch (asi) {
    case 0xe0: /* UA2007/JPS1 Block commit store primary (cache flush) */
    case 0xe1: /* UA2007/JPS1 Block commit store secondary (cache flush) */
    case 0xf0: /* UA2007/JPS1 Block store primary */
    case 0xf1: /* UA2007/JPS1 Block store secondary */
    case 0xf8: /* UA2007/JPS1 Block store primary LE */
    case 0xf9: /* UA2007/JPS1 Block store secondary LE */
        if (rd & 7) {
            helper_raise_exception(env, TT_ILL_INSN);
            return;
        }
        helper_check_align(addr, 0x3f);
        for (i = 0; i < 16; i++) {
            val = *(uint32_t *)&env->fpr[rd++];
            helper_st_asi(addr, val, asi & 0x8f, 4);
            addr += 4;
        }

        return;
    case 0x16: /* UA2007 Block load primary, user privilege */
    case 0x17: /* UA2007 Block load secondary, user privilege */
    case 0x1e: /* UA2007 Block load primary LE, user privilege */
    case 0x1f: /* UA2007 Block load secondary LE, user privilege */
    case 0x70: /* JPS1 Block store primary, user privilege */
    case 0x71: /* JPS1 Block store secondary, user privilege */
    case 0x78: /* JPS1 Block load primary LE, user privilege */
    case 0x79: /* JPS1 Block load secondary LE, user privilege */
        if (rd & 7) {
            helper_raise_exception(env, TT_ILL_INSN);
            return;
        }
        helper_check_align(addr, 0x3f);
        for (i = 0; i < 16; i++) {
            val = *(uint32_t *)&env->fpr[rd++];
            helper_st_asi(addr, val, asi & 0x19, 4);
            addr += 4;
        }

        return;
    default:
        break;
    }

    switch (size) {
    default:
    case 4:
        helper_st_asi(addr, *(uint32_t *)&env->fpr[rd], asi, size);
        break;
    case 8:
        u.l.upper = *(uint32_t *)&env->fpr[rd++];
        u.l.lower = *(uint32_t *)&env->fpr[rd++];
        helper_st_asi(addr, u.ll, asi, size);
        break;
    case 16:
        u.l.upper = *(uint32_t *)&env->fpr[rd++];
        u.l.lower = *(uint32_t *)&env->fpr[rd++];
        helper_st_asi(addr, u.ll, asi, 8);
        u.l.upper = *(uint32_t *)&env->fpr[rd++];
        u.l.lower = *(uint32_t *)&env->fpr[rd++];
        helper_st_asi(addr + 8, u.ll, asi, 8);
        break;
    }
}

target_ulong helper_cas_asi(target_ulong addr, target_ulong val1,
                            target_ulong val2, uint32_t asi)
{
    target_ulong ret;

    val2 &= 0xffffffffUL;
    ret = helper_ld_asi(addr, asi, 4, 0);
    ret &= 0xffffffffUL;
    if (val2 == ret) {
        helper_st_asi(addr, val1 & 0xffffffffUL, asi, 4);
    }
    return ret;
}

target_ulong helper_casx_asi(target_ulong addr, target_ulong val1,
                             target_ulong val2, uint32_t asi)
{
    target_ulong ret;

    ret = helper_ld_asi(addr, asi, 8, 0);
    if (val2 == ret) {
        helper_st_asi(addr, val1, asi, 8);
    }
    return ret;
}
#endif /* TARGET_SPARC64 */

#ifndef TARGET_SPARC64
void helper_rett(void)
{
    unsigned int cwp;

    if (env->psret == 1) {
        helper_raise_exception(env, TT_ILL_INSN);
    }

    env->psret = 1;
    cwp = cwp_inc(env->cwp + 1) ;
    if (env->wim & (1 << cwp)) {
        helper_raise_exception(env, TT_WIN_UNF);
    }
    set_cwp(cwp);
    env->psrs = env->psrps;
}
#endif

static target_ulong helper_udiv_common(target_ulong a, target_ulong b, int cc)
{
    int overflow = 0;
    uint64_t x0;
    uint32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = (b & 0xffffffff);

    if (x1 == 0) {
        helper_raise_exception(env, TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if (x0 > 0xffffffff) {
        x0 = 0xffffffff;
        overflow = 1;
    }

    if (cc) {
        env->cc_dst = x0;
        env->cc_src2 = overflow;
        env->cc_op = CC_OP_DIV;
    }
    return x0;
}

target_ulong helper_udiv(target_ulong a, target_ulong b)
{
    return helper_udiv_common(a, b, 0);
}

target_ulong helper_udiv_cc(target_ulong a, target_ulong b)
{
    return helper_udiv_common(a, b, 1);
}

static target_ulong helper_sdiv_common(target_ulong a, target_ulong b, int cc)
{
    int overflow = 0;
    int64_t x0;
    int32_t x1;

    x0 = (a & 0xffffffff) | ((int64_t) (env->y) << 32);
    x1 = (b & 0xffffffff);

    if (x1 == 0) {
        helper_raise_exception(env, TT_DIV_ZERO);
    }

    x0 = x0 / x1;
    if ((int32_t) x0 != x0) {
        x0 = x0 < 0 ? 0x80000000 : 0x7fffffff;
        overflow = 1;
    }

    if (cc) {
        env->cc_dst = x0;
        env->cc_src2 = overflow;
        env->cc_op = CC_OP_DIV;
    }
    return x0;
}

target_ulong helper_sdiv(target_ulong a, target_ulong b)
{
    return helper_sdiv_common(a, b, 0);
}

target_ulong helper_sdiv_cc(target_ulong a, target_ulong b)
{
    return helper_sdiv_common(a, b, 1);
}

void helper_stdf(target_ulong addr, int mem_idx)
{
    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case MMU_USER_IDX:
        stfq_user(addr, DT0);
        break;
    case MMU_KERNEL_IDX:
        stfq_kernel(addr, DT0);
        break;
#ifdef TARGET_SPARC64
    case MMU_HYPV_IDX:
        stfq_hypv(addr, DT0);
        break;
#endif
    default:
        DPRINTF_MMU("helper_stdf: need to check MMU idx %d\n", mem_idx);
        break;
    }
#else
    stfq_raw(address_mask(env, addr), DT0);
#endif
}

void helper_lddf(target_ulong addr, int mem_idx)
{
    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case MMU_USER_IDX:
        DT0 = ldfq_user(addr);
        break;
    case MMU_KERNEL_IDX:
        DT0 = ldfq_kernel(addr);
        break;
#ifdef TARGET_SPARC64
    case MMU_HYPV_IDX:
        DT0 = ldfq_hypv(addr);
        break;
#endif
    default:
        DPRINTF_MMU("helper_lddf: need to check MMU idx %d\n", mem_idx);
        break;
    }
#else
    DT0 = ldfq_raw(address_mask(env, addr));
#endif
}

void helper_ldqf(target_ulong addr, int mem_idx)
{
    /* XXX add 128 bit load */
    CPU_QuadU u;

    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case MMU_USER_IDX:
        u.ll.upper = ldq_user(addr);
        u.ll.lower = ldq_user(addr + 8);
        QT0 = u.q;
        break;
    case MMU_KERNEL_IDX:
        u.ll.upper = ldq_kernel(addr);
        u.ll.lower = ldq_kernel(addr + 8);
        QT0 = u.q;
        break;
#ifdef TARGET_SPARC64
    case MMU_HYPV_IDX:
        u.ll.upper = ldq_hypv(addr);
        u.ll.lower = ldq_hypv(addr + 8);
        QT0 = u.q;
        break;
#endif
    default:
        DPRINTF_MMU("helper_ldqf: need to check MMU idx %d\n", mem_idx);
        break;
    }
#else
    u.ll.upper = ldq_raw(address_mask(env, addr));
    u.ll.lower = ldq_raw(address_mask(env, addr + 8));
    QT0 = u.q;
#endif
}

void helper_stqf(target_ulong addr, int mem_idx)
{
    /* XXX add 128 bit store */
    CPU_QuadU u;

    helper_check_align(addr, 7);
#if !defined(CONFIG_USER_ONLY)
    switch (mem_idx) {
    case MMU_USER_IDX:
        u.q = QT0;
        stq_user(addr, u.ll.upper);
        stq_user(addr + 8, u.ll.lower);
        break;
    case MMU_KERNEL_IDX:
        u.q = QT0;
        stq_kernel(addr, u.ll.upper);
        stq_kernel(addr + 8, u.ll.lower);
        break;
#ifdef TARGET_SPARC64
    case MMU_HYPV_IDX:
        u.q = QT0;
        stq_hypv(addr, u.ll.upper);
        stq_hypv(addr + 8, u.ll.lower);
        break;
#endif
    default:
        DPRINTF_MMU("helper_stqf: need to check MMU idx %d\n", mem_idx);
        break;
    }
#else
    u.q = QT0;
    stq_raw(address_mask(env, addr), u.ll.upper);
    stq_raw(address_mask(env, addr + 8), u.ll.lower);
#endif
}

#ifndef TARGET_SPARC64
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void helper_save(void)
{
    uint32_t cwp;

    cwp = cwp_dec(env->cwp - 1);
    if (env->wim & (1 << cwp)) {
        helper_raise_exception(env, TT_WIN_OVF);
    }
    set_cwp(cwp);
}

void helper_restore(void)
{
    uint32_t cwp;

    cwp = cwp_inc(env->cwp + 1);
    if (env->wim & (1 << cwp)) {
        helper_raise_exception(env, TT_WIN_UNF);
    }
    set_cwp(cwp);
}

void helper_wrpsr(target_ulong new_psr)
{
    if ((new_psr & PSR_CWP) >= env->nwindows) {
        helper_raise_exception(env, TT_ILL_INSN);
    } else {
        cpu_put_psr(env, new_psr);
    }
}

target_ulong helper_rdpsr(void)
{
    return get_psr();
}

#else
/* XXX: use another pointer for %iN registers to avoid slow wrapping
   handling ? */
void helper_save(void)
{
    uint32_t cwp;

    cwp = cwp_dec(env->cwp - 1);
    if (env->cansave == 0) {
        helper_raise_exception(env, TT_SPILL | (env->otherwin != 0 ?
                                                (TT_WOTHER |
                                                 ((env->wstate & 0x38) >> 1)) :
                                                ((env->wstate & 0x7) << 2)));
    } else {
        if (env->cleanwin - env->canrestore == 0) {
            /* XXX Clean windows without trap */
            helper_raise_exception(env, TT_CLRWIN);
        } else {
            env->cansave--;
            env->canrestore++;
            set_cwp(cwp);
        }
    }
}

void helper_restore(void)
{
    uint32_t cwp;

    cwp = cwp_inc(env->cwp + 1);
    if (env->canrestore == 0) {
        helper_raise_exception(env, TT_FILL | (env->otherwin != 0 ?
                                               (TT_WOTHER |
                                                ((env->wstate & 0x38) >> 1)) :
                                               ((env->wstate & 0x7) << 2)));
    } else {
        env->cansave++;
        env->canrestore--;
        set_cwp(cwp);
    }
}

void helper_flushw(void)
{
    if (env->cansave != env->nwindows - 2) {
        helper_raise_exception(env, TT_SPILL | (env->otherwin != 0 ?
                                                (TT_WOTHER |
                                                 ((env->wstate & 0x38) >> 1)) :
                                                ((env->wstate & 0x7) << 2)));
    }
}

void helper_saved(void)
{
    env->cansave++;
    if (env->otherwin == 0) {
        env->canrestore--;
    } else {
        env->otherwin--;
    }
}

void helper_restored(void)
{
    env->canrestore++;
    if (env->cleanwin < env->nwindows - 1) {
        env->cleanwin++;
    }
    if (env->otherwin == 0) {
        env->cansave--;
    } else {
        env->otherwin--;
    }
}

static target_ulong get_ccr(void)
{
    target_ulong psr;

    psr = get_psr();

    return ((env->xcc >> 20) << 4) | ((psr & PSR_ICC) >> 20);
}

target_ulong cpu_get_ccr(CPUState *env1)
{
    CPUState *saved_env;
    target_ulong ret;

    saved_env = env;
    env = env1;
    ret = get_ccr();
    env = saved_env;
    return ret;
}

static void put_ccr(target_ulong val)
{
    env->xcc = (val >> 4) << 20;
    env->psr = (val & 0xf) << 20;
    CC_OP = CC_OP_FLAGS;
}

void cpu_put_ccr(CPUState *env1, target_ulong val)
{
    CPUState *saved_env;

    saved_env = env;
    env = env1;
    put_ccr(val);
    env = saved_env;
}

static target_ulong get_cwp64(void)
{
    return env->nwindows - 1 - env->cwp;
}

target_ulong cpu_get_cwp64(CPUState *env1)
{
    CPUState *saved_env;
    target_ulong ret;

    saved_env = env;
    env = env1;
    ret = get_cwp64();
    env = saved_env;
    return ret;
}

static void put_cwp64(int cwp)
{
    if (unlikely(cwp >= env->nwindows || cwp < 0)) {
        cwp %= env->nwindows;
    }
    set_cwp(env->nwindows - 1 - cwp);
}

void cpu_put_cwp64(CPUState *env1, int cwp)
{
    CPUState *saved_env;

    saved_env = env;
    env = env1;
    put_cwp64(cwp);
    env = saved_env;
}

target_ulong helper_rdccr(void)
{
    return get_ccr();
}

void helper_wrccr(target_ulong new_ccr)
{
    put_ccr(new_ccr);
}

/* CWP handling is reversed in V9, but we still use the V8 register
   order. */
target_ulong helper_rdcwp(void)
{
    return get_cwp64();
}

void helper_wrcwp(target_ulong new_cwp)
{
    put_cwp64(new_cwp);
}

static inline uint64_t *get_gregset(uint32_t pstate)
{
    switch (pstate) {
    default:
        DPRINTF_PSTATE("ERROR in get_gregset: active pstate bits=%x%s%s%s\n",
                       pstate,
                       (pstate & PS_IG) ? " IG" : "",
                       (pstate & PS_MG) ? " MG" : "",
                       (pstate & PS_AG) ? " AG" : "");
        /* pass through to normal set of global registers */
    case 0:
        return env->bgregs;
    case PS_AG:
        return env->agregs;
    case PS_MG:
        return env->mgregs;
    case PS_IG:
        return env->igregs;
    }
}

static inline void change_pstate(uint32_t new_pstate)
{
    uint32_t pstate_regs, new_pstate_regs;
    uint64_t *src, *dst;

    if (env->def->features & CPU_FEATURE_GL) {
        /* PS_AG is not implemented in this case */
        new_pstate &= ~PS_AG;
    }

    pstate_regs = env->pstate & 0xc01;
    new_pstate_regs = new_pstate & 0xc01;

    if (new_pstate_regs != pstate_regs) {
        DPRINTF_PSTATE("change_pstate: switching regs old=%x new=%x\n",
                       pstate_regs, new_pstate_regs);
        /* Switch global register bank */
        src = get_gregset(new_pstate_regs);
        dst = get_gregset(pstate_regs);
        memcpy32(dst, env->gregs);
        memcpy32(env->gregs, src);
    } else {
        DPRINTF_PSTATE("change_pstate: regs new=%x (unchanged)\n",
                       new_pstate_regs);
    }
    env->pstate = new_pstate;
}

void helper_wrpstate(target_ulong new_state)
{
    change_pstate(new_state & 0xf3f);

#if !defined(CONFIG_USER_ONLY)
    if (cpu_interrupts_enabled(env)) {
        cpu_check_irqs(env);
    }
#endif
}

void cpu_change_pstate(CPUState *env1, uint32_t new_pstate)
{
    CPUState *saved_env;

    saved_env = env;
    env = env1;
    change_pstate(new_pstate);
    env = saved_env;
}

void helper_wrpil(target_ulong new_pil)
{
#if !defined(CONFIG_USER_ONLY)
    DPRINTF_PSTATE("helper_wrpil old=%x new=%x\n",
                   env->psrpil, (uint32_t)new_pil);

    env->psrpil = new_pil;

    if (cpu_interrupts_enabled(env)) {
        cpu_check_irqs(env);
    }
#endif
}

void helper_done(void)
{
    trap_state *tsptr = cpu_tsptr(env);

    env->pc = tsptr->tnpc;
    env->npc = tsptr->tnpc + 4;
    put_ccr(tsptr->tstate >> 32);
    env->asi = (tsptr->tstate >> 24) & 0xff;
    change_pstate((tsptr->tstate >> 8) & 0xf3f);
    put_cwp64(tsptr->tstate & 0xff);
    env->tl--;

    DPRINTF_PSTATE("... helper_done tl=%d\n", env->tl);

#if !defined(CONFIG_USER_ONLY)
    if (cpu_interrupts_enabled(env)) {
        cpu_check_irqs(env);
    }
#endif
}

void helper_retry(void)
{
    trap_state *tsptr = cpu_tsptr(env);

    env->pc = tsptr->tpc;
    env->npc = tsptr->tnpc;
    put_ccr(tsptr->tstate >> 32);
    env->asi = (tsptr->tstate >> 24) & 0xff;
    change_pstate((tsptr->tstate >> 8) & 0xf3f);
    put_cwp64(tsptr->tstate & 0xff);
    env->tl--;

    DPRINTF_PSTATE("... helper_retry tl=%d\n", env->tl);

#if !defined(CONFIG_USER_ONLY)
    if (cpu_interrupts_enabled(env)) {
        cpu_check_irqs(env);
    }
#endif
}

static void do_modify_softint(const char *operation, uint32_t value)
{
    if (env->softint != value) {
        env->softint = value;
        DPRINTF_PSTATE(": %s new %08x\n", operation, env->softint);
#if !defined(CONFIG_USER_ONLY)
        if (cpu_interrupts_enabled(env)) {
            cpu_check_irqs(env);
        }
#endif
    }
}

void helper_set_softint(uint64_t value)
{
    do_modify_softint("helper_set_softint", env->softint | (uint32_t)value);
}

void helper_clear_softint(uint64_t value)
{
    do_modify_softint("helper_clear_softint", env->softint & (uint32_t)~value);
}

void helper_write_softint(uint64_t value)
{
    do_modify_softint("helper_write_softint", (uint32_t)value);
}
#endif

#if !defined(CONFIG_USER_ONLY)

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr);

#define MMUSUFFIX _mmu
#define ALIGNED_ONLY

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* XXX: make it generic ? */
static void cpu_restore_state2(void *retaddr)
{
    TranslationBlock *tb;
    unsigned long pc;

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
}

static void do_unaligned_access(target_ulong addr, int is_write, int is_user,
                                void *retaddr)
{
#ifdef DEBUG_UNALIGNED
    printf("Unaligned access to 0x" TARGET_FMT_lx " from 0x" TARGET_FMT_lx
           "\n", addr, env->pc);
#endif
    cpu_restore_state2(retaddr);
    helper_raise_exception(env, TT_UNALIGNED);
}

/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *env1, target_ulong addr, int is_write, int mmu_idx,
              void *retaddr)
{
    int ret;
    CPUState *saved_env;

    saved_env = env;
    env = env1;

    ret = cpu_sparc_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (ret) {
        cpu_restore_state2(retaddr);
        cpu_loop_exit(env);
    }
    env = saved_env;
}

#endif /* !CONFIG_USER_ONLY */

#ifndef TARGET_SPARC64
#if !defined(CONFIG_USER_ONLY)
static void do_unassigned_access(target_phys_addr_t addr, int is_write,
                                 int is_exec, int is_asi, int size)
{
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
        tlb_flush(env, 1);
    }
}
#endif
#else
#if defined(CONFIG_USER_ONLY)
static void do_unassigned_access(target_ulong addr, int is_write, int is_exec,
                                 int is_asi, int size)
#else
static void do_unassigned_access(target_phys_addr_t addr, int is_write,
                                 int is_exec, int is_asi, int size)
#endif
{
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

#if !defined(CONFIG_USER_ONLY)
void cpu_unassigned_access(CPUState *env1, target_phys_addr_t addr,
                           int is_write, int is_exec, int is_asi, int size)
{
    CPUState *saved_env;

    saved_env = env;
    env = env1;
    /* Ignore unassigned accesses outside of CPU context */
    if (env1) {
        do_unassigned_access(addr, is_write, is_exec, is_asi, size);
    }
    env = saved_env;
}
#endif
