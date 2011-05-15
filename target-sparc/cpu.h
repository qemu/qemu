#ifndef CPU_SPARC_H
#define CPU_SPARC_H

#include "config.h"
#include "qemu-common.h"

#if !defined(TARGET_SPARC64)
#define TARGET_LONG_BITS 32
#define TARGET_FPREGS 32
#define TARGET_PAGE_BITS 12 /* 4k */
#define TARGET_PHYS_ADDR_SPACE_BITS 36
#define TARGET_VIRT_ADDR_SPACE_BITS 32
#else
#define TARGET_LONG_BITS 64
#define TARGET_FPREGS 64
#define TARGET_PAGE_BITS 13 /* 8k */
#define TARGET_PHYS_ADDR_SPACE_BITS 41
# ifdef TARGET_ABI32
#  define TARGET_VIRT_ADDR_SPACE_BITS 32
# else
#  define TARGET_VIRT_ADDR_SPACE_BITS 44
# endif
#endif

#define CPUState struct CPUSPARCState

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
#define TT_POWER_ON_RESET 0x01
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
#define TT_WOTHER   (1 << 5)
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
#if !defined(TARGET_SPARC64)
#define PSR_EF    (1<<12)
#define PSR_PIL   0xf00
#define PSR_S     (1<<7)
#define PSR_PS    (1<<6)
#define PSR_ET    (1<<5)
#define PSR_CWP   0x1f
#endif

#define CC_SRC (env->cc_src)
#define CC_SRC2 (env->cc_src2)
#define CC_DST (env->cc_dst)
#define CC_OP  (env->cc_op)

enum {
    CC_OP_DYNAMIC, /* must use dynamic code to get cc_op */
    CC_OP_FLAGS,   /* all cc are back in status register */
    CC_OP_DIV,     /* modify N, Z and V, C = 0*/
    CC_OP_ADD,     /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_ADDX,    /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_TADD,    /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_TADDTV,  /* modify all flags except V, CC_DST = res, CC_SRC = src1 */
    CC_OP_SUB,     /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_SUBX,    /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_TSUB,    /* modify all flags, CC_DST = res, CC_SRC = src1 */
    CC_OP_TSUBTV,  /* modify all flags except V, CC_DST = res, CC_SRC = src1 */
    CC_OP_LOGIC,   /* modify N and Z, C = V = 0, CC_DST = res */
    CC_OP_NB,
};

/* Trap base register */
#define TBR_BASE_MASK 0xfffff000

#if defined(TARGET_SPARC64)
#define PS_TCT   (1<<12) /* UA2007, impl.dep. trap on control transfer */
#define PS_IG    (1<<11) /* v9, zero on UA2007 */
#define PS_MG    (1<<10) /* v9, zero on UA2007 */
#define PS_CLE   (1<<9) /* UA2007 */
#define PS_TLE   (1<<8) /* UA2007 */
#define PS_RMO   (1<<7)
#define PS_RED   (1<<5) /* v9, zero on UA2007 */
#define PS_PEF   (1<<4) /* enable fpu */
#define PS_AM    (1<<3) /* address mask */
#define PS_PRIV  (1<<2)
#define PS_IE    (1<<1)
#define PS_AG    (1<<0) /* v9, zero on UA2007 */

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
//gcc warns about constant overflow for ~FSR_FTT_MASK
//#define FSR_FTT_MASK (FSR_FTT2 | FSR_FTT1 | FSR_FTT0)
#ifdef TARGET_SPARC64
#define FSR_FTT_NMASK      0xfffffffffffe3fffULL
#define FSR_FTT_CEXC_NMASK 0xfffffffffffe3fe0ULL
#define FSR_LDFSR_OLDMASK  0x0000003f000fc000ULL
#define FSR_LDXFSR_MASK    0x0000003fcfc00fffULL
#define FSR_LDXFSR_OLDMASK 0x00000000000fc000ULL
#else
#define FSR_FTT_NMASK      0xfffe3fffULL
#define FSR_FTT_CEXC_NMASK 0xfffe3fe0ULL
#define FSR_LDFSR_OLDMASK  0x000fc000ULL
#endif
#define FSR_LDFSR_MASK     0xcfc00fffULL
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
#define NB_MMU_MODES 6
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
    uint32_t mxcc_version;
    uint32_t features;
    uint32_t nwindows;
    uint32_t maxtl;
} sparc_def_t;

#define CPU_FEATURE_FLOAT        (1 << 0)
#define CPU_FEATURE_FLOAT128     (1 << 1)
#define CPU_FEATURE_SWAP         (1 << 2)
#define CPU_FEATURE_MUL          (1 << 3)
#define CPU_FEATURE_DIV          (1 << 4)
#define CPU_FEATURE_FLUSH        (1 << 5)
#define CPU_FEATURE_FSQRT        (1 << 6)
#define CPU_FEATURE_FMUL         (1 << 7)
#define CPU_FEATURE_VIS1         (1 << 8)
#define CPU_FEATURE_VIS2         (1 << 9)
#define CPU_FEATURE_FSMULD       (1 << 10)
#define CPU_FEATURE_HYPV         (1 << 11)
#define CPU_FEATURE_CMT          (1 << 12)
#define CPU_FEATURE_GL           (1 << 13)
#define CPU_FEATURE_TA0_SHUTDOWN (1 << 14) /* Shutdown on "ta 0x0" */
#define CPU_FEATURE_ASR17        (1 << 15)
#define CPU_FEATURE_CACHE_CTRL   (1 << 16)

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

#define TTE_VALID_BIT       (1ULL << 63)
#define TTE_USED_BIT        (1ULL << 41)
#define TTE_LOCKED_BIT      (1ULL <<  6)
#define TTE_GLOBAL_BIT      (1ULL <<  0)

#define TTE_IS_VALID(tte)   ((tte) & TTE_VALID_BIT)
#define TTE_IS_USED(tte)    ((tte) & TTE_USED_BIT)
#define TTE_IS_LOCKED(tte)  ((tte) & TTE_LOCKED_BIT)
#define TTE_IS_GLOBAL(tte)  ((tte) & TTE_GLOBAL_BIT)

#define TTE_SET_USED(tte)   ((tte) |= TTE_USED_BIT)
#define TTE_SET_UNUSED(tte) ((tte) &= ~TTE_USED_BIT)

typedef struct SparcTLBEntry {
    uint64_t tag;
    uint64_t tte;
} SparcTLBEntry;

struct CPUTimer
{
    const char *name;
    uint32_t    frequency;
    uint32_t    disabled;
    uint64_t    disabled_mask;
    int64_t     clock_offset;
    struct QEMUTimer  *qtimer;
};

typedef struct CPUTimer CPUTimer;

struct QEMUFile;
void cpu_put_timer(struct QEMUFile *f, CPUTimer *s);
void cpu_get_timer(struct QEMUFile *f, CPUTimer *s);

typedef struct CPUSPARCState {
    target_ulong gregs[8]; /* general registers */
    target_ulong *regwptr; /* pointer to current register window */
    target_ulong pc;       /* program counter */
    target_ulong npc;      /* next program counter */
    target_ulong y;        /* multiply/divide register */

    /* emulator internal flags handling */
    target_ulong cc_src, cc_src2;
    target_ulong cc_dst;
    uint32_t cc_op;

    target_ulong t0, t1; /* temporaries live across basic blocks */
    target_ulong cond; /* conditional branch result (XXX: save it in a
                          temporary register when possible) */

    uint32_t psr;      /* processor state register */
    target_ulong fsr;      /* FPU state register */
    float32 fpr[TARGET_FPREGS];  /* floating point registers */
    uint32_t cwp;      /* index of current register window (extracted
                          from PSR) */
#if !defined(TARGET_SPARC64) || defined(TARGET_ABI32)
    uint32_t wim;      /* window invalid mask */
#endif
    target_ulong tbr;  /* trap base register */
#if !defined(TARGET_SPARC64)
    int      psrs;     /* supervisor mode (extracted from PSR) */
    int      psrps;    /* previous supervisor mode */
    int      psret;    /* enable traps */
#endif
    uint32_t psrpil;   /* interrupt blocking level */
    uint32_t pil_in;   /* incoming interrupt level bitmap */
#if !defined(TARGET_SPARC64)
    int      psref;    /* enable fpu */
#endif
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
    //typedef struct SparcMMU
    union {
        uint64_t immuregs[16];
        struct {
            uint64_t tsb_tag_target;
            uint64_t unused_mmu_primary_context;   // use DMMU
            uint64_t unused_mmu_secondary_context; // use DMMU
            uint64_t sfsr;
            uint64_t sfar;
            uint64_t tsb;
            uint64_t tag_access;
        } immu;
    };
    union {
        uint64_t dmmuregs[16];
        struct {
            uint64_t tsb_tag_target;
            uint64_t mmu_primary_context;
            uint64_t mmu_secondary_context;
            uint64_t sfsr;
            uint64_t sfar;
            uint64_t tsb;
            uint64_t tag_access;
        } dmmu;
    };
    SparcTLBEntry itlb[64];
    SparcTLBEntry dtlb[64];
    uint32_t mmu_version;
#else
    uint32_t mmuregs[32];
    uint64_t mxccdata[4];
    uint64_t mxccregs[8];
    uint32_t mmubpctrv, mmubpctrc, mmubpctrs;
    uint64_t mmubpaction;
    uint64_t mmubpregs[4];
    uint64_t prom_addr;
#endif
    /* temporary float registers */
    float64 dt0, dt1;
    float128 qt0, qt1;
    float_status fp_status;
#if defined(TARGET_SPARC64)
#define MAXTL_MAX 8
#define MAXTL_MASK (MAXTL_MAX - 1)
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
    CPUTimer *tick, *stick;
#define TICK_NPT_MASK        0x8000000000000000ULL
#define TICK_INT_DIS         0x8000000000000000ULL
    uint64_t gsr;
    uint32_t gl; // UA2005
    /* UA 2005 hyperprivileged registers */
    uint64_t hpstate, htstate[MAXTL_MAX], hintp, htba, hver, hstick_cmpr, ssr;
    CPUTimer *hstick; // UA 2005
    uint32_t softint;
#define SOFTINT_TIMER   1
#define SOFTINT_STIMER  (1 << 16)
#define SOFTINT_INTRMASK (0xFFFE)
#define SOFTINT_REG_MASK (SOFTINT_STIMER|SOFTINT_INTRMASK|SOFTINT_TIMER)
#endif
    sparc_def_t *def;

    void *irq_manager;
    void (*qemu_irq_ack) (void *irq_manager, int intno);

    /* Leon3 cache control */
    uint32_t cache_control;
} CPUSPARCState;

#ifndef NO_CPU_IO_DEFS
/* helper.c */
CPUSPARCState *cpu_sparc_init(const char *cpu_model);
void cpu_sparc_set_id(CPUSPARCState *env, unsigned int cpu);
void sparc_cpu_list(FILE *f, fprintf_function cpu_fprintf);
int cpu_sparc_handle_mmu_fault(CPUSPARCState *env1, target_ulong address, int rw,
                               int mmu_idx, int is_softmmu);
#define cpu_handle_mmu_fault cpu_sparc_handle_mmu_fault
target_ulong mmu_probe(CPUSPARCState *env, target_ulong address, int mmulev);
void dump_mmu(FILE *f, fprintf_function cpu_fprintf, CPUState *env);

/* translate.c */
void gen_intermediate_code_init(CPUSPARCState *env);

/* cpu-exec.c */
int cpu_sparc_exec(CPUSPARCState *s);

/* op_helper.c */
target_ulong cpu_get_psr(CPUState *env1);
void cpu_put_psr(CPUState *env1, target_ulong val);
#ifdef TARGET_SPARC64
target_ulong cpu_get_ccr(CPUState *env1);
void cpu_put_ccr(CPUState *env1, target_ulong val);
target_ulong cpu_get_cwp64(CPUState *env1);
void cpu_put_cwp64(CPUState *env1, int cwp);
void cpu_change_pstate(CPUState *env1, uint32_t new_pstate);
#endif
int cpu_cwp_inc(CPUState *env1, int cwp);
int cpu_cwp_dec(CPUState *env1, int cwp);
void cpu_set_cwp(CPUState *env1, int new_cwp);
void leon3_irq_manager(void *irq_manager, int intno);

/* sun4m.c, sun4u.c */
void cpu_check_irqs(CPUSPARCState *env);

/* leon3.c */
void leon3_irq_ack(void *irq_manager, int intno);

#if defined (TARGET_SPARC64)

static inline int compare_masked(uint64_t x, uint64_t y, uint64_t mask)
{
    return (x & mask) == (y & mask);
}

#define MMU_CONTEXT_BITS 13
#define MMU_CONTEXT_MASK ((1 << MMU_CONTEXT_BITS) - 1)

static inline int tlb_compare_context(const SparcTLBEntry *tlb,
                                      uint64_t context)
{
    return compare_masked(context, tlb->tag, MMU_CONTEXT_MASK);
}

#endif
#endif

/* cpu-exec.c */
#if !defined(CONFIG_USER_ONLY)
void do_unassigned_access(target_phys_addr_t addr, int is_write, int is_exec,
                          int is_asi, int size);
target_phys_addr_t cpu_get_phys_page_nofault(CPUState *env, target_ulong addr,
                                           int mmu_idx);

#endif
int cpu_sparc_signal_handler(int host_signum, void *pinfo, void *puc);

#define cpu_init cpu_sparc_init
#define cpu_exec cpu_sparc_exec
#define cpu_gen_code cpu_sparc_gen_code
#define cpu_signal_handler cpu_sparc_signal_handler
#define cpu_list sparc_cpu_list

#define CPU_SAVE_VERSION 7

/* MMU modes definitions */
#if defined (TARGET_SPARC64)
#define MMU_USER_IDX   0
#define MMU_MODE0_SUFFIX _user
#define MMU_USER_SECONDARY_IDX   1
#define MMU_MODE1_SUFFIX _user_secondary
#define MMU_KERNEL_IDX 2
#define MMU_MODE2_SUFFIX _kernel
#define MMU_KERNEL_SECONDARY_IDX 3
#define MMU_MODE3_SUFFIX _kernel_secondary
#define MMU_NUCLEUS_IDX 4
#define MMU_MODE4_SUFFIX _nucleus
#define MMU_HYPV_IDX   5
#define MMU_MODE5_SUFFIX _hypv
#else
#define MMU_USER_IDX   0
#define MMU_MODE0_SUFFIX _user
#define MMU_KERNEL_IDX 1
#define MMU_MODE1_SUFFIX _kernel
#endif

#if defined (TARGET_SPARC64)
static inline int cpu_has_hypervisor(CPUState *env1)
{
    return env1->def->features & CPU_FEATURE_HYPV;
}

static inline int cpu_hypervisor_mode(CPUState *env1)
{
    return cpu_has_hypervisor(env1) && (env1->hpstate & HS_PRIV);
}

static inline int cpu_supervisor_mode(CPUState *env1)
{
    return env1->pstate & PS_PRIV;
}
#endif

static inline int cpu_mmu_index(CPUState *env1)
{
#if defined(CONFIG_USER_ONLY)
    return MMU_USER_IDX;
#elif !defined(TARGET_SPARC64)
    return env1->psrs;
#else
    if (env1->tl > 0) {
        return MMU_NUCLEUS_IDX;
    } else if (cpu_hypervisor_mode(env1)) {
        return MMU_HYPV_IDX;
    } else if (cpu_supervisor_mode(env1)) {
        return MMU_KERNEL_IDX;
    } else {
        return MMU_USER_IDX;
    }
#endif
}

static inline int cpu_interrupts_enabled(CPUState *env1)
{
#if !defined (TARGET_SPARC64)
    if (env1->psret != 0)
        return 1;
#else
    if (env1->pstate & PS_IE)
        return 1;
#endif

    return 0;
}

static inline int cpu_pil_allowed(CPUState *env1, int pil)
{
#if !defined(TARGET_SPARC64)
    /* level 15 is non-maskable on sparc v8 */
    return pil == 15 || pil > env1->psrpil;
#else
    return pil > env1->psrpil;
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

#include "cpu-all.h"

#ifdef TARGET_SPARC64
/* sun4u.c */
void cpu_tick_set_count(CPUTimer *timer, uint64_t count);
uint64_t cpu_tick_get_count(CPUTimer *timer);
void cpu_tick_set_limit(CPUTimer *timer, uint64_t limit);
trap_state* cpu_tsptr(CPUState* env);
#endif

static inline void cpu_get_tb_cpu_state(CPUState *env, target_ulong *pc,
                                        target_ulong *cs_base, int *flags)
{
    *pc = env->pc;
    *cs_base = env->npc;
#ifdef TARGET_SPARC64
    // AM . Combined FPU enable bits . PRIV . DMMU enabled . IMMU enabled
    *flags = ((env->pstate & PS_AM) << 2)          /* 5 */
        | (((env->pstate & PS_PEF) >> 1)           /* 3 */
        | ((env->fprs & FPRS_FEF) << 2))           /* 4 */
        | (env->pstate & PS_PRIV)                  /* 2 */
        | ((env->lsu & (DMMU_E | IMMU_E)) >> 2)    /* 1, 0 */
        | ((env->tl & 0xff) << 8)
        | (env->dmmu.mmu_primary_context << 16);   /* 16... */
#else
    // FPU enable . Supervisor
    *flags = (env->psref << 4) | env->psrs;
#endif
}

/* helper.c */
void do_interrupt(CPUState *env);

#endif
