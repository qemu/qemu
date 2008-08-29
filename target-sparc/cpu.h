#ifndef CPU_SPARC_H
#define CPU_SPARC_H

#include "config.h"

#if !defined(TARGET_SPARC64)
#define TARGET_LONG_BITS 32
#define TARGET_FPREGS 32
#define TARGET_PAGE_BITS 12 /* 4k */
#else
#define TARGET_LONG_BITS 64
#define TARGET_FPREGS 64
#define TARGET_PAGE_BITS 13 /* 8k */
#endif

#define TARGET_PHYS_ADDR_BITS 64

#include "cpu-defs.h"

#include "softfloat.h"

#define TARGET_HAS_ICE 1

#if !defined(TARGET_SPARC64)
#define ELF_MACHINE     EM_SPARC
#else
#define ELF_MACHINE     EM_SPARCV9
#endif

/*#define EXCP_INTERRUPT 0x100*/

/* trap definitions */
#ifndef TARGET_SPARC64
#define TT_TFAULT   0x01
#define TT_ILL_INSN 0x02
#define TT_PRIV_INSN 0x03
#define TT_NFPU_INSN 0x04
#define TT_WIN_OVF  0x05
#define TT_WIN_UNF  0x06
#define TT_UNALIGNED 0x07
#define TT_FP_EXCP  0x08
#define TT_DFAULT   0x09
#define TT_TOVF     0x0a
#define TT_EXTINT   0x10
#define TT_CODE_ACCESS 0x21
#define TT_UNIMP_FLUSH 0x25
#define TT_DATA_ACCESS 0x29
#define TT_DIV_ZERO 0x2a
#define TT_NCP_INSN 0x24
#define TT_TRAP     0x80
#else
#define TT_TFAULT   0x08
#define TT_CODE_ACCESS 0x0a
#define TT_ILL_INSN 0x10
#define TT_UNIMP_FLUSH TT_ILL_INSN
#define TT_PRIV_INSN 0x11
#define TT_NFPU_INSN 0x20
#define TT_FP_EXCP  0x21
#define TT_TOVF     0x23
#define TT_CLRWIN   0x24
#define TT_DIV_ZERO 0x28
#define TT_DFAULT   0x30
#define TT_DATA_ACCESS 0x32
#define TT_UNALIGNED 0x34
#define TT_PRIV_ACT 0x37
#define TT_EXTINT   0x40
#define TT_IVEC     0x60
#define TT_TMISS    0x64
#define TT_DMISS    0x68
#define TT_DPROT    0x6c
#define TT_SPILL    0x80
#define TT_FILL     0xc0
#define TT_WOTHER   0x10
#define TT_TRAP     0x100
#endif

#define PSR_NEG_SHIFT 23
#define PSR_NEG   (1 << PSR_NEG_SHIFT)
#define PSR_ZERO_SHIFT 22
#define PSR_ZERO  (1 << PSR_ZERO_SHIFT)
#define PSR_OVF_SHIFT 21
#define PSR_OVF   (1 << PSR_OVF_SHIFT)
#define PSR_CARRY_SHIFT 20
#define PSR_CARRY (1 << PSR_CARRY_SHIFT)
#define PSR_ICC   (PSR_NEG|PSR_ZERO|PSR_OVF|PSR_CARRY)
#define PSR_EF    (1<<12)
#define PSR_PIL   0xf00
#define PSR_S     (1<<7)
#define PSR_PS    (1<<6)
#define PSR_ET    (1<<5)
#define PSR_CWP   0x1f

/* Trap base register */
#define TBR_BASE_MASK 0xfffff000

#if defined(TARGET_SPARC64)
#define PS_IG    (1<<11)
#define PS_MG    (1<<10)
#define PS_RMO   (1<<7)
#define PS_RED   (1<<5)
#define PS_PEF   (1<<4)
#define PS_AM    (1<<3)
#define PS_PRIV  (1<<2)
#define PS_IE    (1<<1)
#define PS_AG    (1<<0)

#define FPRS_FEF (1<<2)

#define HS_PRIV  (1<<2)
#endif

/* Fcc */
#define FSR_RD1        (1ULL << 31)
#define FSR_RD0        (1ULL << 30)
#define FSR_RD_MASK    (FSR_RD1 | FSR_RD0)
#define FSR_RD_NEAREST 0
#define FSR_RD_ZERO    FSR_RD0
#define FSR_RD_POS     FSR_RD1
#define FSR_RD_NEG     (FSR_RD1 | FSR_RD0)

#define FSR_NVM   (1ULL << 27)
#define FSR_OFM   (1ULL << 26)
#define FSR_UFM   (1ULL << 25)
#define FSR_DZM   (1ULL << 24)
#define FSR_NXM   (1ULL << 23)
#define FSR_TEM_MASK (FSR_NVM | FSR_OFM | FSR_UFM | FSR_DZM | FSR_NXM)

#define FSR_NVA   (1ULL << 9)
#define FSR_OFA   (1ULL << 8)
#define FSR_UFA   (1ULL << 7)
#define FSR_DZA   (1ULL << 6)
#define FSR_NXA   (1ULL << 5)
#define FSR_AEXC_MASK (FSR_NVA | FSR_OFA | FSR_UFA | FSR_DZA | FSR_NXA)

#define FSR_NVC   (1ULL << 4)
#define FSR_OFC   (1ULL << 3)
#define FSR_UFC   (1ULL << 2)
#define FSR_DZC   (1ULL << 1)
#define FSR_NXC   (1ULL << 0)
#define FSR_CEXC_MASK (FSR_NVC | FSR_OFC | FSR_UFC | FSR_DZC | FSR_NXC)

#define FSR_FTT2   (1ULL << 16)
#define FSR_FTT1   (1ULL << 15)
#define FSR_FTT0   (1ULL << 14)
#define FSR_FTT_MASK (FSR_FTT2 | FSR_FTT1 | FSR_FTT0)
#define FSR_FTT_IEEE_EXCP (1ULL << 14)
#define FSR_FTT_UNIMPFPOP (3ULL << 14)
#define FSR_FTT_SEQ_ERROR (4ULL << 14)
#define FSR_FTT_INVAL_FPR (6ULL << 14)

#define FSR_FCC1_SHIFT 11
#define FSR_FCC1  (1ULL << FSR_FCC1_SHIFT)
#define FSR_FCC0_SHIFT 10
#define FSR_FCC0  (1ULL << FSR_FCC0_SHIFT)

/* MMU */
#define MMU_E     (1<<0)
#define MMU_NF    (1<<1)

#define PTE_ENTRYTYPE_MASK 3
#define PTE_ACCESS_MASK    0x1c
#define PTE_ACCESS_SHIFT   2
#define PTE_PPN_SHIFT      7
#define PTE_ADDR_MASK      0xffffff00

#define PG_ACCESSED_BIT 5
#define PG_MODIFIED_BIT 6
#define PG_CACHE_BIT    7

#define PG_ACCESSED_MASK (1 << PG_ACCESSED_BIT)
#define PG_MODIFIED_MASK (1 << PG_MODIFIED_BIT)
#define PG_CACHE_MASK    (1 << PG_CACHE_BIT)

/* 3 <= NWINDOWS <= 32. */
#define MIN_NWINDOWS 3
#define MAX_NWINDOWS 32

#if !defined(TARGET_SPARC64)
#define NB_MMU_MODES 2
#else
#define NB_MMU_MODES 3
typedef struct trap_state {
    uint64_t tpc;
    uint64_t tnpc;
    uint64_t tstate;
    uint32_t tt;
} trap_state;
#endif

typedef struct sparc_def_t {
    const char *name;
    target_ulong iu_version;
    uint32_t fpu_version;
    uint32_t mmu_version;
    uint32_t mmu_bm;
    uint32_t mmu_ctpr_mask;
    uint32_t mmu_cxr_mask;
    uint32_t mmu_sfsr_mask;
    uint32_t mmu_trcr_mask;
    uint32_t features;
    uint32_t nwindows;
    uint32_t maxtl;
} sparc_def_t;

#define CPU_FEATURE_FLOAT    (1 << 0)
#define CPU_FEATURE_FLOAT128 (1 << 1)
#define CPU_FEATURE_SWAP     (1 << 2)
#define CPU_FEATURE_MUL      (1 << 3)
#define CPU_FEATURE_DIV      (1 << 4)
#define CPU_FEATURE_FLUSH    (1 << 5)
#define CPU_FEATURE_FSQRT    (1 << 6)
#define CPU_FEATURE_FMUL     (1 << 7)
#define CPU_FEATURE_VIS1     (1 << 8)
#define CPU_FEATURE_VIS2     (1 << 9)
#define CPU_FEATURE_FSMULD   (1 << 10)
#define CPU_FEATURE_HYPV     (1 << 11)
#define CPU_FEATURE_CMT      (1 << 12)
#define CPU_FEATURE_GL       (1 << 13)
#ifndef TARGET_SPARC64
#define CPU_DEFAULT_FEATURES (CPU_FEATURE_FLOAT | CPU_FEATURE_SWAP |  \
                              CPU_FEATURE_MUL | CPU_FEATURE_DIV |     \
                              CPU_FEATURE_FLUSH | CPU_FEATURE_FSQRT | \
                              CPU_FEATURE_FMUL | CPU_FEATURE_FSMULD)
#else
#define CPU_DEFAULT_FEATURES (CPU_FEATURE_FLOAT | CPU_FEATURE_SWAP |  \
                              CPU_FEATURE_MUL | CPU_FEATURE_DIV |     \
                              CPU_FEATURE_FLUSH | CPU_FEATURE_FSQRT | \
                              CPU_FEATURE_FMUL | CPU_FEATURE_VIS1 |   \
                              CPU_FEATURE_VIS2 | CPU_FEATURE_FSMULD)
enum {
    mmu_us_12, // Ultrasparc < III (64 entry TLB)
    mmu_us_3,  // Ultrasparc III (512 entry TLB)
    mmu_us_4,  // Ultrasparc IV (several TLBs, 32 and 256MB pages)
    mmu_sun4v, // T1, T2
};
#endif

typedef struct CPUSPARCState {
    target_ulong gregs[8]; /* general registers */
    target_ulong *regwptr; /* pointer to current register window */
    target_ulong pc;       /* program counter */
    target_ulong npc;      /* next program counter */
    target_ulong y;        /* multiply/divide register */

    /* emulator internal flags handling */
    target_ulong cc_src, cc_src2;
    target_ulong cc_dst;

    target_ulong t0, t1; /* temporaries live across basic blocks */
    target_ulong cond; /* conditional branch result (XXX: save it in a
                          temporary register when possible) */

    uint32_t psr;      /* processor state register */
    target_ulong fsr;      /* FPU state register */
    float32 fpr[TARGET_FPREGS];  /* floating point registers */
    uint32_t cwp;      /* index of current register window (extracted
                          from PSR) */
    uint32_t wim;      /* window invalid mask */
    target_ulong tbr;  /* trap base register */
    int      psrs;     /* supervisor mode (extracted from PSR) */
    int      psrps;    /* previous supervisor mode */
    int      psret;    /* enable traps */
    uint32_t psrpil;   /* interrupt blocking level */
    uint32_t pil_in;   /* incoming interrupt level bitmap */
    int      psref;    /* enable fpu */
    target_ulong version;
    int interrupt_index;
    uint32_t nwindows;
    /* NOTE: we allow 8 more registers to handle wrapping */
    target_ulong regbase[MAX_NWINDOWS * 16 + 8];

    CPU_COMMON

    /* MMU regs */
#if defined(TARGET_SPARC64)
    uint64_t lsu;
#define DMMU_E 0x8
#define IMMU_E 0x4
    uint64_t immuregs[16];
    uint64_t dmmuregs[16];
    uint64_t itlb_tag[64];
    uint64_t itlb_tte[64];
    uint64_t dtlb_tag[64];
    uint64_t dtlb_tte[64];
    uint32_t mmu_version;
#else
    uint32_t mmuregs[32];
    uint64_t mxccdata[4];
    uint64_t mxccregs[8];
    uint64_t prom_addr;
#endif
    /* temporary float registers */
    float32 ft0, ft1;
    float64 dt0, dt1;
    float128 qt0, qt1;
    float_status fp_status;
#if defined(TARGET_SPARC64)
#define MAXTL_MAX 8
#define MAXTL_MASK (MAXTL_MAX - 1)
    trap_state *tsptr;
    trap_state ts[MAXTL_MAX];
    uint32_t xcc;               /* Extended integer condition codes */
    uint32_t asi;
    uint32_t pstate;
    uint32_t tl;
    uint32_t maxtl;
    uint32_t cansave, canrestore, otherwin, wstate, cleanwin;
    uint64_t agregs[8]; /* alternate general registers */
    uint64_t bgregs[8]; /* backup for normal global registers */
    uint64_t igregs[8]; /* interrupt general registers */
    uint64_t mgregs[8]; /* mmu general registers */
    uint64_t fprs;
    uint64_t tick_cmpr, stick_cmpr;
    void *tick, *stick;
    uint64_t gsr;
    uint32_t gl; // UA2005
    /* UA 2005 hyperprivileged registers */
    uint64_t hpstate, htstate[MAXTL_MAX], hintp, htba, hver, hstick_cmpr, ssr;
    void *hstick; // UA 2005
#endif
    sparc_def_t *def;
} CPUSPARCState;

#if defined(TARGET_SPARC64)
#define GET_FSR32(env) (env->fsr & 0xcfc1ffff)
#define PUT_FSR32(env, val) do { uint32_t _tmp = val;                   \
        env->fsr = (_tmp & 0xcfc1c3ff) | (env->fsr & 0x3f00000000ULL);  \
    } while (0)
#define GET_FSR64(env) (env->fsr & 0x3fcfc1ffffULL)
#define PUT_FSR64(env, val) do { uint64_t _tmp = val;   \
        env->fsr = _tmp & 0x3fcfc1c3ffULL;              \
    } while (0)
#else
#define GET_FSR32(env) (env->fsr)
#define PUT_FSR32(env, val) do { uint32_t _tmp = val;                   \
        env->fsr = (_tmp & 0xcfc1dfff) | (env->fsr & 0x000e0000);       \
    } while (0)
#endif

/* helper.c */
CPUSPARCState *cpu_sparc_init(const char *cpu_model);
void cpu_sparc_set_id(CPUSPARCState *env, unsigned int cpu);
void sparc_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt,
                                                 ...));

/* translate.c */
void gen_intermediate_code_init(CPUSPARCState *env);

/* cpu-exec.c */
int cpu_sparc_exec(CPUSPARCState *s);

#define GET_PSR(env) (env->version | (env->psr & PSR_ICC) |             \
                      (env->psref? PSR_EF : 0) |                        \
                      (env->psrpil << 8) |                              \
                      (env->psrs? PSR_S : 0) |                          \
                      (env->psrps? PSR_PS : 0) |                        \
                      (env->psret? PSR_ET : 0) | env->cwp)

#ifndef NO_CPU_IO_DEFS
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

static inline void cpu_set_cwp(CPUSPARCState *env1, int new_cwp)
{
    /* put the modified wrap registers at their proper location */
    if (env1->cwp == env1->nwindows - 1)
        memcpy32(env1->regbase, env1->regbase + env1->nwindows * 16);
    env1->cwp = new_cwp;
    /* put the wrap registers at their temporary location */
    if (new_cwp == env1->nwindows - 1)
        memcpy32(env1->regbase + env1->nwindows * 16, env1->regbase);
    env1->regwptr = env1->regbase + (new_cwp * 16);
}

static inline int cpu_cwp_inc(CPUSPARCState *env1, int cwp)
{
    if (unlikely(cwp >= env1->nwindows))
        cwp -= env1->nwindows;
    return cwp;
}

static inline int cpu_cwp_dec(CPUSPARCState *env1, int cwp)
{
    if (unlikely(cwp < 0))
        cwp += env1->nwindows;
    return cwp;
}
#endif

#define PUT_PSR(env, val) do { int _tmp = val;                          \
        env->psr = _tmp & PSR_ICC;                                      \
        env->psref = (_tmp & PSR_EF)? 1 : 0;                            \
        env->psrpil = (_tmp & PSR_PIL) >> 8;                            \
        env->psrs = (_tmp & PSR_S)? 1 : 0;                              \
        env->psrps = (_tmp & PSR_PS)? 1 : 0;                            \
        env->psret = (_tmp & PSR_ET)? 1 : 0;                            \
        cpu_set_cwp(env, _tmp & PSR_CWP);                               \
    } while (0)

#ifdef TARGET_SPARC64
#define GET_CCR(env) (((env->xcc >> 20) << 4) | ((env->psr & PSR_ICC) >> 20))
#define PUT_CCR(env, val) do { int _tmp = val;                          \
        env->xcc = (_tmp >> 4) << 20;                                   \
        env->psr = (_tmp & 0xf) << 20;                                  \
    } while (0)
#define GET_CWP64(env) (env->nwindows - 1 - (env)->cwp)

#ifndef NO_CPU_IO_DEFS
static inline void PUT_CWP64(CPUSPARCState *env1, int cwp)
{
    if (unlikely(cwp >= env1->nwindows || cwp < 0))
        cwp = 0;
    cpu_set_cwp(env1, env1->nwindows - 1 - cwp);
}
#endif
#endif

/* cpu-exec.c */
void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi);

#define CPUState CPUSPARCState
#define cpu_init cpu_sparc_init
#define cpu_exec cpu_sparc_exec
#define cpu_gen_code cpu_sparc_gen_code
#define cpu_signal_handler cpu_sparc_signal_handler
#define cpu_list sparc_cpu_list

#define CPU_SAVE_VERSION 5

/* MMU modes definitions */
#define MMU_MODE0_SUFFIX _user
#define MMU_MODE1_SUFFIX _kernel
#ifdef TARGET_SPARC64
#define MMU_MODE2_SUFFIX _hypv
#endif
#define MMU_USER_IDX   0
#define MMU_KERNEL_IDX 1
#define MMU_HYPV_IDX   2

static inline int cpu_mmu_index(CPUState *env1)
{
#if defined(CONFIG_USER_ONLY)
    return MMU_USER_IDX;
#elif !defined(TARGET_SPARC64)
    return env1->psrs;
#else
    if (!env1->psrs)
        return MMU_USER_IDX;
    else if ((env1->hpstate & HS_PRIV) == 0)
        return MMU_KERNEL_IDX;
    else
        return MMU_HYPV_IDX;
#endif
}

static inline int cpu_fpu_enabled(CPUState *env1)
{
#if defined(CONFIG_USER_ONLY)
    return 1;
#elif !defined(TARGET_SPARC64)
    return env1->psref;
#else
    return ((env1->pstate & PS_PEF) != 0) && ((env1->fprs & FPRS_FEF) != 0);
#endif
}

#if defined(CONFIG_USER_ONLY)
static inline void cpu_clone_regs(CPUState *env, target_ulong newsp)
{
    if (newsp)
        env->regwptr[22] = newsp;
    env->regwptr[0] = 0;
    /* FIXME: Do we also need to clear CF?  */
    /* XXXXX */
    printf ("HELPME: %s:%d\n", __FILE__, __LINE__);
}
#endif

#define CPU_PC_FROM_TB(env, tb) do { \
    env->pc = tb->pc; \
    env->npc = tb->cs_base; \
    } while(0)

#include "cpu-all.h"

#endif
